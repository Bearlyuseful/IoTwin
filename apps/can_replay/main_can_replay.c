/**
 * @file main_can_replay.c
 * @brief can_replay: plays a recorded log (candump -L format) onto iotwin-bus.
 *
 *   can_monitor --log drive.log          record
 *   can_replay drive.log                 replay with the original timing
 *   can_replay --speed 4 --loop drive.log
 *
 * Logs of a real bus recorded with candump -L (can-utils) work as well.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bus_link.h"
#include "candump_log.h"
#include "cli.h"
#include "log.h"
#include "net.h"
#include "pal_time.h"

typedef struct {
  CanFrame frame;
  uint64_t offset_us; /* relative to the first frame */
} LogEntry;

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int signal_number) {
  (void)signal_number;
  g_stop = 1;
}

static void print_usage(const char* program) {
  printf("Usage: %s [options] FILE\n\n"
         "Plays a log in candump -L format onto iotwin-bus (e.g. recorded with\n"
         "can_monitor --log FILE). Lines without timestamp are sent --gap-ms apart.\n\n"
         "Options:\n"
         "  --host HOST           bus server host (default 127.0.0.1)\n"
         "  --port PORT           bus server port (default 8080)\n"
         "  --speed FACTOR        time factor, 2 = twice as fast (default 1)\n"
         "  --loop                start again at the end until Ctrl+C\n"
         "  --gap-ms N            spacing of lines without timestamp (default 10)\n"
         "  --filter ID[/MASK]    only frames with matching id (hex)\n"
         "  --verbose             print every frame sent\n"
         "  --help                show this help\n",
         program);
}

static bool parse_filter(const char* text, uint32_t* id, uint32_t* mask) {
  char* end = NULL;
  *id = (uint32_t)strtoul(text, &end, 16);
  *mask = CAN_EFF_MASK;
  if (end == text) {
    return false;
  }
  if (*end == '/') {
    const char* mask_text = end + 1;
    *mask = (uint32_t)strtoul(mask_text, &end, 16);
    if (end == mask_text) {
      return false;
    }
  }
  return *end == '\0';
}

/* Reads the whole log. Returns the number of entries or -1. */
static long load_log(const char* path, long gap_ms, bool filtered, uint32_t id, uint32_t mask, LogEntry** out) {
  FILE* file = fopen(path, "r");
  if (file == NULL) {
    log_error("cannot open %s", path);
    return -1;
  }
  size_t capacity = 1024;
  size_t count = 0;
  LogEntry* entries = malloc(capacity * sizeof(LogEntry));
  char line[256];
  int line_number = 0;
  bool have_first = false;
  uint64_t first_us = 0;
  uint64_t last_offset = 0;
  while (entries != NULL && fgets(line, sizeof(line), file) != NULL) {
    line_number++;
    CanFrame frame;
    uint64_t time_us = 0;
    bool has_time = false;
    const CandumpLineType type = candump_log_parse(line, strlen(line), &frame, &time_us, &has_time);
    if (type == CANDUMP_LINE_SKIP) {
      continue;
    }
    if (type == CANDUMP_LINE_ERROR) {
      log_warn("%s:%d: not a frame, skipped", path, line_number);
      continue;
    }
    uint64_t offset;
    if (has_time) {
      if (!have_first) {
        first_us = time_us;
        have_first = true;
      }
      offset = time_us >= first_us ? time_us - first_us : 0;
    } else {
      offset = count == 0 ? 0 : last_offset + (uint64_t)gap_ms * 1000u;
    }
    if (offset < last_offset) {
      offset = last_offset; /* never send backwards in time */
    }
    last_offset = offset;
    if (filtered && (frame.id & mask) != (id & mask)) {
      continue;
    }
    if (count == capacity) {
      capacity *= 2;
      LogEntry* grown = realloc(entries, capacity * sizeof(LogEntry));
      if (grown == NULL) {
        break;
      }
      entries = grown;
    }
    entries[count].frame = frame;
    entries[count].offset_us = offset;
    count++;
  }
  fclose(file);
  if (entries == NULL) {
    log_error("out of memory");
    return -1;
  }
  *out = entries;
  return (long)count;
}

int main(int argc, char** argv) {
  const char* host = "127.0.0.1";
  long port = 8080;
  double speed = 1.0;
  bool loop = false;
  bool verbose = false;
  long gap_ms = 10;
  bool filtered = false;
  uint32_t filter_id = 0;
  uint32_t filter_mask = CAN_EFF_MASK;
  const char* path = NULL;

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
    } else if (strcmp(option, "--speed") == 0 && value && (speed = strtod(value, NULL)) > 0.0) {
      i++;
    } else if (strcmp(option, "--gap-ms") == 0 && cli_parse_long(value, 0, 600000, &gap_ms)) {
      i++;
    } else if (strcmp(option, "--filter") == 0 && value && parse_filter(value, &filter_id, &filter_mask)) {
      filtered = true;
      i++;
    } else if (strcmp(option, "--loop") == 0) {
      loop = true;
    } else if (strcmp(option, "--verbose") == 0) {
      verbose = true;
    } else if (option[0] != '-' && path == NULL) {
      path = option;
    } else {
      fprintf(stderr, "Unknown or invalid option '%s' (see --help)\n", option);
      return 2;
    }
  }
  if (path == NULL) {
    print_usage(argv[0]);
    return 2;
  }

  LogEntry* entries = NULL;
  const long count = load_log(path, gap_ms, filtered, filter_id, filter_mask, &entries);
  if (count < 0) {
    return 1;
  }
  if (count == 0) {
    log_warn("%s contains no frames", path);
    free(entries);
    return 0;
  }

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  pal_time_init();
  if (!net_init()) {
    log_error("socket library initialisation failed: %s", net_last_error());
    free(entries);
    return 1;
  }
  BusLink link;
  if (!bus_link_open(&link, host, (uint16_t)port)) {
    log_error("bus %s:%ld not reachable: %s", host, port, net_last_error());
    net_cleanup();
    free(entries);
    return 1;
  }
  bus_link_command(&link, "name replay", NULL, 0, 500, NULL);

  const double duration_s = (double)entries[count - 1].offset_us / 1e6 / speed;
  log_info("replaying %ld frames from %s (%.1f s at speed %.2g)%s", count, path, duration_s, speed,
           loop ? ", looping" : "");
  unsigned long long sent = 0;
  int exit_code = 0;
  do {
    const uint64_t start_ns = pal_time_monotonic_ns();
    for (long i = 0; i < count && !g_stop; i++) {
      const uint64_t due_ns = start_ns + (uint64_t)((double)entries[i].offset_us * 1000.0 / speed);
      for (;;) {
        const uint64_t now = pal_time_monotonic_ns();
        if (now >= due_ns || g_stop) {
          break;
        }
        const uint64_t wait_ms = (due_ns - now) / 1000000u;
        /* Read (and discard) bus traffic while waiting so the socket buffer never fills. */
        if (!bus_link_poll(&link, wait_ms > 50 ? 50 : (int)wait_ms, NULL)) {
          log_error("connection to the bus lost");
          g_stop = 1;
          exit_code = 1;
        }
      }
      if (g_stop) {
        break;
      }
      if (!bus_link_send_frame(&link, &entries[i].frame)) {
        log_error("sending failed: %s", net_last_error());
        exit_code = 1;
        g_stop = 1;
        break;
      }
      sent++;
      if (verbose) {
        char text[CAN_FRAME_TEXT_MAX];
        can_frame_to_text(&entries[i].frame, text, sizeof(text));
        log_info("%10.3f  %s", (double)entries[i].offset_us / 1e6, text);
      }
    }
  } while (loop && !g_stop);

  log_info("can_replay sent %llu frames", sent);
  bus_link_close(&link);
  net_cleanup();
  free(entries);
  return exit_code;
}
