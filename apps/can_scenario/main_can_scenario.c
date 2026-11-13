/**
 * @file main_can_scenario.c
 * @brief can_scenario: runs scenario scripts against the nodes on iotwin-bus.
 *
 *   can_scenario data/scenarios/light-demo/switch-controls-lamp.scn
 *   can_scenario -e "bus drop 20" -e "bus delay 30 10"      (one-off steps)
 *
 * Exit code 0 when every scenario passed, 1 otherwise; usable as test.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_link.h"
#include "cli.h"
#include "isotp.h"
#include "log.h"
#include "net.h"
#include "pal_time.h"
#include "scenario.h"
#include "uds.h"

#define RECENT_FRAMES 6
#define HISTORY_FRAMES 512
#define COMMAND_TIMEOUT_MS 1000

/*
 * Frames are collected from the start of the last action (send, bench, bus, uds),
 * so a reaction that arrives while the action is still running (e.g. during a UDS
 * request) is seen by the next expect. Every expect consumes the frames up to its
 * match; "wait" discards what arrived during the wait.
 */
typedef struct {
  BusLink link;
  bool verbose;
  CanFrame history[HISTORY_FRAMES];
  size_t history_count;
  size_t consumed;      /* frames already used by expect steps */
  size_t total_frames;  /* since the last action, incl. frames beyond the history */
  /* the current expect step */
  const FramePattern* pattern;
  bool matched;
  /* UDS */
  IsoTpLink isotp;
  uint8_t uds_buffer[ISOTP_MAX_LENGTH];
  bool uds_active;
} Runner;

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int signal_number) {
  (void)signal_number;
  g_stop = 1;
}

static uint64_t now_ms(void) { return pal_time_monotonic_ms(); }

static void on_frame(void* context, const CanFrame* frame) {
  Runner* runner = (Runner*)context;
  if (runner->verbose) {
    char text[CAN_FRAME_TEXT_MAX];
    can_frame_to_text(frame, text, sizeof(text));
    printf("        rx %s\n", text);
  }
  if (runner->uds_active) {
    isotp_on_frame(&runner->isotp, frame, pal_time_monotonic_ns() / 1000u);
  }
  runner->total_frames++;
  if (runner->history_count < HISTORY_FRAMES) {
    runner->history[runner->history_count++] = *frame;
  }
  if (runner->pattern != NULL && !runner->matched && frame_pattern_matches(runner->pattern, frame)) {
    runner->matched = true;
    runner->consumed = runner->history_count;
  }
}

static void clear_history(Runner* runner) {
  runner->history_count = 0;
  runner->consumed = 0;
  runner->total_frames = 0;
}

/* Looks for the pattern among the frames not consumed yet. */
static bool match_history(Runner* runner, const FramePattern* pattern) {
  for (size_t i = runner->consumed; i < runner->history_count; i++) {
    if (frame_pattern_matches(pattern, &runner->history[i])) {
      runner->consumed = i + 1;
      return true;
    }
  }
  return false;
}

static void on_control(void* context, const char* line, size_t length) {
  Runner* runner = (Runner*)context;
  if (runner->verbose) {
    printf("        bus %.*s\n", (int)length, line);
  }
}

static const BusLinkHandlers* handlers(Runner* runner) {
  static BusLinkHandlers h;
  h.on_frame = on_frame;
  h.on_control = on_control;
  h.context = runner;
  return &h;
}

/* Start of an action: everything that arrived so far is history no expect should see. */
static bool drain(Runner* runner) {
  runner->pattern = NULL;
  const bool ok = bus_link_poll(&runner->link, 0, handlers(runner));
  clear_history(runner);
  return ok;
}

/* Runs the bus for duration_ms or until a pattern matched. */
static bool run_for(Runner* runner, uint32_t duration_ms) {
  const uint64_t end = now_ms() + duration_ms;
  while (!g_stop && !runner->matched) {
    const uint64_t now = now_ms();
    if (now >= end) {
      break;
    }
    const uint64_t left = end - now;
    if (!bus_link_poll(&runner->link, left > 10 ? 10 : (int)left, handlers(runner))) {
      return false;
    }
  }
  return true;
}

static void describe_recent(const Runner* runner, size_t from, char* out, size_t size) {
  out[0] = '\0';
  if (runner->history_count <= from) {
    snprintf(out, size, "no frames at all");
    return;
  }
  size_t used = (size_t)snprintf(out, size, "last frames:");
  const size_t available = runner->history_count - from;
  const size_t shown = available < RECENT_FRAMES ? available : RECENT_FRAMES;
  for (size_t i = runner->history_count - shown; i < runner->history_count && used < size; i++) {
    char text[CAN_FRAME_TEXT_MAX];
    can_frame_to_text(&runner->history[i], text, sizeof(text));
    used += (size_t)snprintf(out + used, size - used, " %s", text);
  }
}

static void hex_text(const uint8_t* data, size_t length, char* out, size_t size) {
  out[0] = '\0';
  size_t used = 0;
  for (size_t i = 0; i < length && used + 3 < size; i++) {
    used += (size_t)snprintf(out + used, size - used, "%02X", data[i]);
  }
}

static bool isotp_send_frame(void* context, const CanFrame* frame) {
  return bus_link_send_frame((BusLink*)context, frame);
}

/* Sends a UDS request; returns the response length or -1 (message in error). */
static int uds_request(Runner* runner, const ScenarioStep* step, char* error, size_t error_size) {
  IsoTpConfig config;
  isotp_default_config(&config, step->uds_tx, step->uds_rx);
  config.send = isotp_send_frame;
  config.context = &runner->link;
  isotp_init(&runner->isotp, &config, runner->uds_buffer, sizeof(runner->uds_buffer));
  runner->uds_active = true;

  int result = -1;
  if (isotp_send(&runner->isotp, step->uds_request, step->uds_request_length, pal_time_monotonic_ns() / 1000u) !=
      ISOTP_OK) {
    snprintf(error, error_size, "sending the request failed");
  } else {
    uint64_t deadline = now_ms() + COMMAND_TIMEOUT_MS;
    while (!g_stop && result < 0) {
      if (!bus_link_poll(&runner->link, 5, handlers(runner))) {
        snprintf(error, error_size, "connection to the bus lost");
        break;
      }
      isotp_poll(&runner->isotp, pal_time_monotonic_ns() / 1000u);
      size_t length = 0;
      if (isotp_receive(&runner->isotp, &length)) {
        if (length >= 3 && runner->uds_buffer[0] == UDS_SID_NEGATIVE_RESPONSE &&
            runner->uds_buffer[2] == UDS_NRC_RESPONSE_PENDING) {
          deadline = now_ms() + UDS_DEFAULT_P2_STAR_MS;
          continue;
        }
        result = (int)length;
        break;
      }
      if (now_ms() > deadline) {
        snprintf(error, error_size, "no response from 0x%03X within %u ms", (unsigned)step->uds_rx,
                 COMMAND_TIMEOUT_MS);
        break;
      }
    }
  }
  runner->uds_active = false;
  return result;
}

/* Executes one step; on failure writes the reason into error. */
static bool run_step(Runner* runner, const ScenarioStep* step, char* error, size_t error_size) {
  runner->pattern = NULL;
  runner->matched = false;

  switch (step->type) {
    case STEP_LOG:
      printf("        %s\n", step->text);
      return true;
    case STEP_WAIT:
      if (!run_for(runner, step->ms)) {
        snprintf(error, error_size, "connection to the bus lost");
        return false;
      }
      clear_history(runner); /* what happened during the wait does not count */
      return true;
    case STEP_SEND:
      if (!drain(runner) || !bus_link_send_frame(&runner->link, &step->frame)) {
        snprintf(error, error_size, "sending failed");
        return false;
      }
      return true;
    case STEP_BENCH:
      if (!drain(runner) || !bus_link_send_line(&runner->link, step->text)) {
        snprintf(error, error_size, "sending failed");
        return false;
      }
      return true;
    case STEP_BUS: {
      char answer[160];
      if (!drain(runner) ||
          !bus_link_command(&runner->link, step->text, answer, sizeof(answer), COMMAND_TIMEOUT_MS, handlers(runner))) {
        snprintf(error, error_size, "bus refused: %s", answer[0] ? answer : "no answer (old iotwin-bus?)");
        return false;
      }
      return true;
    }
    case STEP_EXPECT:
    case STEP_EXPECT_NOT: {
      const size_t first_unconsumed = runner->consumed;
      const uint64_t start = now_ms();
      runner->matched = match_history(runner, &step->pattern); /* arrived during the last action */
      runner->pattern = &step->pattern;
      if (!runner->matched && !run_for(runner, step->ms)) {
        snprintf(error, error_size, "connection to the bus lost");
        return false;
      }
      runner->pattern = NULL;
      if (step->type == STEP_EXPECT_NOT) {
        runner->consumed = runner->history_count;
      }
      if (step->type == STEP_EXPECT && !runner->matched) {
        char recent[200];
        describe_recent(runner, first_unconsumed, recent, sizeof(recent));
        snprintf(error, error_size, "not seen within %u ms (%s)", (unsigned)step->ms, recent);
        return false;
      }
      if (step->type == STEP_EXPECT_NOT && runner->matched) {
        snprintf(error, error_size, "seen after %llu ms", (unsigned long long)(now_ms() - start));
        return false;
      }
      if (runner->verbose && step->type == STEP_EXPECT) {
        printf("        matched after %llu ms\n", (unsigned long long)(now_ms() - start));
      }
      return true;
    }
    case STEP_UDS: {
      if (!drain(runner)) {
        snprintf(error, error_size, "connection to the bus lost");
        return false;
      }
      const int length = uds_request(runner, step, error, error_size);
      if (length < 0) {
        return false;
      }
      const uint8_t* response = runner->uds_buffer;
      char got[2 * SCENARIO_UDS_MAX + 1];
      hex_text(response, (size_t)length, got, sizeof(got));
      bool ok;
      if (step->uds_expect_given) {
        ok = step->uds_expect_prefix ? (size_t)length >= step->uds_expect_length
                                     : (size_t)length == step->uds_expect_length;
        ok = ok && memcmp(response, step->uds_expect, step->uds_expect_length) == 0;
      } else {
        ok = length >= 1 && response[0] == (uint8_t)(step->uds_request[0] + UDS_POSITIVE_RESPONSE_OFFSET);
      }
      if (!ok) {
        if (length >= 3 && response[0] == UDS_SID_NEGATIVE_RESPONSE) {
          snprintf(error, error_size, "got %s (%s)", got, uds_nrc_name(response[2]));
        } else {
          snprintf(error, error_size, "got %s", got);
        }
        return false;
      }
      if (runner->verbose) {
        printf("        response %s\n", got);
      }
      return true;
    }
  }
  return false;
}

static bool run_scenario(Runner* runner, const Scenario* scenario) {
  const uint64_t start = now_ms();
  printf("RUN  %s\n", scenario->name);
  fflush(stdout);
  for (size_t i = 0; i < scenario->count && !g_stop; i++) {
    const ScenarioStep* step = &scenario->steps[i];
    char error[320] = "";
    if (!run_step(runner, step, error, sizeof(error))) {
      printf("  FAIL line %d: %s\n       %s\n", step->line, step->text, error);
      printf("FAIL %s (%llu ms)\n", scenario->name, (unsigned long long)(now_ms() - start));
      fflush(stdout);
      return false;
    }
    if (runner->verbose && step->type != STEP_LOG) {
      printf("  ok   line %d: %s\n", step->line, step->text);
    }
    fflush(stdout);
  }
  if (g_stop) {
    printf("STOP %s\n", scenario->name);
    return false;
  }
  printf("PASS %s (%u steps, %llu ms)\n", scenario->name, (unsigned)scenario->count,
         (unsigned long long)(now_ms() - start));
  fflush(stdout);
  return true;
}

static void print_usage(const char* program) {
  printf("Usage: %s [options] FILE... | -e STEP...\n\n"
         "Runs scenario scripts against the nodes on iotwin-bus and checks their reactions.\n"
         "Exit code 0 if all scenarios passed. Script syntax: docs/dev/scenarios.md.\n\n"
         "Options:\n"
         "  --host HOST           bus server host (default 127.0.0.1)\n"
         "  --port PORT           bus server port (default 8080)\n"
         "  -e STEP               run one step, e.g. -e \"bus drop 20\" (repeatable)\n"
         "  --verbose             print every step and every frame\n"
         "  --help                show this help\n\n"
         "Steps: send <frame> | expect <pattern> [within <ms>] | expect-not <pattern> for <ms>\n"
         "       wait <ms> | bench <node> <command> | bus drop|delay|silence|seed|clear ...\n"
         "       uds <ecu|gateway|TX:RX> <request hex> [expect <hex>[*]] | log <text>\n",
         program);
}

int main(int argc, char** argv) {
  const char* host = "127.0.0.1";
  long port = 8080;
  bool verbose = false;
  static Scenario inline_steps;
  scenario_init(&inline_steps);
  snprintf(inline_steps.name, sizeof(inline_steps.name), "command line");
  const char* files[64];
  int file_count = 0;

  for (int i = 1; i < argc; i++) {
    const char* option = argv[i];
    const char* value = i + 1 < argc ? argv[i + 1] : NULL;
    if (strcmp(option, "--help") == 0 || strcmp(option, "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    } else if (strcmp(option, "--host") == 0 && value) {
      host = value;
      i++;
    } else if (strcmp(option, "--port") == 0 && cli_parse_long(value, 1, 65535, &port)) {
      i++;
    } else if (strcmp(option, "-e") == 0 && value) {
      char error[256];
      if (!scenario_parse_line(&inline_steps, value, (int)inline_steps.count + 1, error, sizeof(error))) {
        fprintf(stderr, "-e \"%s\": %s\n", value, error);
        return 2;
      }
      i++;
    } else if (strcmp(option, "--verbose") == 0) {
      verbose = true;
    } else if (option[0] != '-' && file_count < 64) {
      files[file_count++] = option;
    } else {
      fprintf(stderr, "Unknown or invalid option '%s' (see --help)\n", option);
      return 2;
    }
  }
  if (file_count == 0 && inline_steps.count == 0) {
    print_usage(argv[0]);
    return 2;
  }

  /* Load everything first: a syntax error should not leave a half-run test behind. */
  static Scenario scenarios[64];
  for (int i = 0; i < file_count; i++) {
    char error[256];
    if (!scenario_load(&scenarios[i], files[i], error, sizeof(error))) {
      fprintf(stderr, "%s: %s\n", files[i], error);
      return 2;
    }
  }

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  pal_time_init();
  if (!net_init()) {
    log_error("socket library initialisation failed: %s", net_last_error());
    return 1;
  }
  static Runner runner;
  memset(&runner, 0, sizeof(runner));
  runner.verbose = verbose;
  if (!bus_link_open(&runner.link, host, (uint16_t)port)) {
    log_error("bus %s:%ld not reachable: %s", host, port, net_last_error());
    net_cleanup();
    return 1;
  }
  bus_link_command(&runner.link, "name scenario", NULL, 0, COMMAND_TIMEOUT_MS, handlers(&runner));

  int failed = 0;
  int passed = 0;
  if (inline_steps.count > 0) {
    if (run_scenario(&runner, &inline_steps)) {
      passed++;
    } else {
      failed++;
    }
  }
  for (int i = 0; i < file_count && !g_stop; i++) {
    if (run_scenario(&runner, &scenarios[i])) {
      passed++;
    } else {
      failed++;
    }
  }
  if (file_count + (inline_steps.count > 0) > 1) {
    printf("\n%d passed, %d failed\n", passed, failed);
  }

  bus_link_close(&runner.link);
  net_cleanup();
  return failed == 0 && !g_stop ? 0 : 1;
}
