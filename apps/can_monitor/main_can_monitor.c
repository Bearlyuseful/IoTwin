/**
 * @file main_can_monitor.c
 * @brief can_monitor: prints the traffic of iotwin-bus, decoded with a DBC file.
 *
 *   17:21:22.229  200#01            SWITCH_STATE SwitchOn=1 (On)
 *   17:21:22.239  100#01            LAMP_CMD LampOn=1 (On)
 */

#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "can_frame.h"
#include "candump_log.h"
#include "cli.h"
#include "dbc.h"
#include "line_reader.h"
#include "log.h"
#include "net.h"
#include "pal_time.h"

#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

typedef struct {
  DbcDatabase dbc;
  bool have_dbc;
  bool changes_only;
  CanFrame last[64];
  size_t last_count;
  unsigned long long frames;
  FILE* log;   /* --log: every frame in candump -L format */
  bool quiet;  /* --quiet: no console output (recording only) */
} Monitor;

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int signal_number) {
  (void)signal_number;
  g_stop = 1;
}

static void timestamp(char* out, size_t size) {
#ifdef _WIN32
  SYSTEMTIME now;
  GetLocalTime(&now);
  snprintf(out, size, "%02u:%02u:%02u.%03u", (unsigned)now.wHour, (unsigned)now.wMinute,
           (unsigned)now.wSecond, (unsigned)now.wMilliseconds);
#else
  struct timespec now;
  struct tm local;
  clock_gettime(CLOCK_REALTIME, &now);
  localtime_r(&now.tv_sec, &local);
  snprintf(out, size, "%02d:%02d:%02d.%03ld", local.tm_hour, local.tm_min, local.tm_sec,
           now.tv_nsec / 1000000L);
#endif
}

static uint64_t wall_clock_us(void) {
  struct timespec now;
  timespec_get(&now, TIME_UTC);
  return (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
}

/* With --changes: true if the payload differs from the last frame with this id. */
static bool changed(Monitor* monitor, const CanFrame* frame) {
  for (size_t i = 0; i < monitor->last_count; i++) {
    CanFrame* last = &monitor->last[i];
    if (last->id == frame->id && last->extended == frame->extended) {
      const bool differs = !can_frame_equals(last, frame);
      *last = *frame;
      return differs;
    }
  }
  if (monitor->last_count < sizeof(monitor->last) / sizeof(monitor->last[0])) {
    monitor->last[monitor->last_count++] = *frame;
  }
  return true;
}

static void on_line(const char* line, size_t length, void* user_data) {
  Monitor* monitor = (Monitor*)user_data;
  CanFrame frame;
  if (!can_frame_from_text(line, length, &frame)) {
    return;
  }
  monitor->frames++;
  if (monitor->log != NULL) {
    char entry[CANDUMP_LOG_LINE_MAX];
    if (candump_log_format(&frame, wall_clock_us(), "iotwin", entry, sizeof(entry)) > 0) {
      fprintf(monitor->log, "%s\n", entry);
      fflush(monitor->log);
    }
  }
  if (monitor->quiet || (monitor->changes_only && !changed(monitor, &frame))) {
    return;
  }
  /* "HH:MM:SS.mmm" needs 13 bytes, but GCC's -Wformat-truncation (Release, -Werror)
     sizes the buffer for the full value range of the integer fields. */
  char time_text[32];
  char raw[CAN_FRAME_TEXT_MAX];
  char decoded[512] = "";
  timestamp(time_text, sizeof(time_text));
  can_frame_to_text(&frame, raw, sizeof(raw));
  if (monitor->have_dbc && dbc_describe_frame(&monitor->dbc, &frame, decoded, sizeof(decoded)) < 0) {
    snprintf(decoded, sizeof(decoded), "(unknown id)");
  }
  printf("%s  %-26s %s\n", time_text, raw, decoded);
  fflush(stdout);
}

static void print_usage(const char* program) {
  printf("Usage: %s [options]\n\n"
         "Prints all frames on iotwin-bus, decoded with a DBC file.\n\n"
         "Options:\n"
         "  --dbc FILE            DBC file for decoding (e.g. data/dbc/iotwin.dbc)\n"
         "  --host HOST           bus server host (default 127.0.0.1)\n"
         "  --port PORT           bus server port (default 8080)\n"
         "  --changes             only print frames whose payload changed (hides cyclic repeats)\n"
         "  --log FILE            record every frame in candump -L format (replay: can_replay FILE)\n"
         "  --quiet               no console output, e.g. with --log\n"
         "%s"
         "  --help                show this help\n",
         program, cli_common_usage());
}

int main(int argc, char** argv) {
  const char* host = "127.0.0.1";
  long port = 8080;
  const char* dbc_path = NULL;
  const char* log_path = NULL;
  Monitor monitor;
  memset(&monitor, 0, sizeof(monitor));
  PalRtConfig rt;
  pal_rt_default_config(&rt);

  for (int i = 1; i < argc; i++) {
    const char* option = argv[i];
    const char* value = i + 1 < argc ? argv[i + 1] : NULL;
    int common = cli_parse_common_option(argc, argv, &i, &rt);
    if (common < 0) {
      return 2;
    }
    if (common > 0) {
      continue;
    }
    if (strcmp(option, "--help") == 0 || strcmp(option, "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    } else if (strcmp(option, "--dbc") == 0 && value) {
      dbc_path = value;
      i++;
    } else if (strcmp(option, "--host") == 0 && value) {
      host = value;
      i++;
    } else if (strcmp(option, "--port") == 0 && cli_parse_long(value, 1, 65535, &port)) {
      i++;
    } else if (strcmp(option, "--changes") == 0) {
      monitor.changes_only = true;
    } else if (strcmp(option, "--log") == 0 && value) {
      log_path = value;
      i++;
    } else if (strcmp(option, "--quiet") == 0) {
      monitor.quiet = true;
    } else {
      fprintf(stderr, "Unknown or invalid option '%s' (see --help)\n", option);
      return 2;
    }
  }

  if (dbc_path != NULL) {
    char error[256];
    if (!dbc_load_file(dbc_path, &monitor.dbc, error, sizeof(error))) {
      log_error("DBC: %s", error);
      return 1;
    }
    monitor.have_dbc = true;
    log_info("DBC %s: %u messages", dbc_path, (unsigned)monitor.dbc.message_count);
  }

  if (log_path != NULL) {
    monitor.log = fopen(log_path, "w");
    if (monitor.log == NULL) {
      log_error("cannot write %s", log_path);
      return 1;
    }
    log_info("recording to %s", log_path);
  }

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  pal_time_init();
  if (!net_init()) {
    log_error("socket library initialisation failed: %s", net_last_error());
    return 1;
  }

  net_socket_t socket = NET_INVALID_SOCKET;
  LineReader reader;
  bool reported = false;
  while (!g_stop) {
    if (socket == NET_INVALID_SOCKET) {
      socket = net_connect_tcp(host, (uint16_t)port);
      if (socket == NET_INVALID_SOCKET) {
        if (!reported) {
          log_warn("bus %s:%ld not reachable (%s), retrying every second", host, port, net_last_error());
          reported = true;
        }
        pal_sleep_ms(1000);
        continue;
      }
      reported = false;
      line_reader_init(&reader);
      log_info("monitoring bus %s:%ld", host, port);
    }
    NetPollFd entry = {socket, NET_POLL_IN, 0};
    if (net_poll(&entry, 1, 200) <= 0) {
      continue;
    }
    char buffer[2048];
    int n = net_recv(socket, buffer, sizeof(buffer));
    if (n <= 0) {
      log_warn("connection to bus lost");
      net_close(socket);
      socket = NET_INVALID_SOCKET;
      continue;
    }
    line_reader_feed(&reader, buffer, (size_t)n, on_line, &monitor);
  }

  log_info("can_monitor stopping after %llu frames", monitor.frames);
  if (monitor.log != NULL) {
    fclose(monitor.log);
  }
  net_close(socket);
  net_cleanup();
  dbc_free(&monitor.dbc);
  return 0;
}
