/**
 * @file main_bus.c
 * @brief iotwin-bus: virtual CAN bus server (command line front end).
 */

#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "bus_server.h"
#include "cli.h"
#include "log.h"
#include "pal_rt.h"
#include "pal_time.h"

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int signal_number) {
  (void)signal_number;
  g_stop = 1;
}

static void print_usage(const char* program) {
  printf("Usage: %s [options]\n\n"
         "Virtual CAN bus: forwards every frame received from one TCP client to all others.\n\n"
         "Options:\n"
         "  --bind ADDRESS        interface to listen on (default 127.0.0.1, all: 0.0.0.0)\n"
         "  --port PORT           TCP port (default %d)\n"
         "  --max-clients N       connection limit (default 32, max %d)\n"
         "  --echo                also send frames back to their sender\n"
         "  --log-frames          log every routed frame\n"
         "%s"
         "  --help                show this help\n",
         program, BUS_SERVER_DEFAULT_PORT, BUS_SERVER_MAX_CLIENTS, cli_common_usage());
}

int main(int argc, char** argv) {
  BusServerConfig config;
  bus_server_default_config(&config);
  PalRtConfig rt;
  pal_rt_default_config(&rt);

  for (int i = 1; i < argc; i++) {
    const char* option = argv[i];
    long value = 0;
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
    }
    if (strcmp(option, "--bind") == 0 && i + 1 < argc) {
      config.bind_address = argv[++i];
    } else if (strcmp(option, "--port") == 0 && i + 1 < argc &&
               cli_parse_long(argv[i + 1], 0, 65535, &value)) {
      config.port = (uint16_t)value;
      i++;
    } else if (strcmp(option, "--max-clients") == 0 && i + 1 < argc &&
               cli_parse_long(argv[i + 1], 1, BUS_SERVER_MAX_CLIENTS, &value)) {
      config.max_clients = (size_t)value;
      i++;
    } else if (strcmp(option, "--echo") == 0) {
      config.echo = true;
    } else if (strcmp(option, "--log-frames") == 0) {
      config.log_frames = true;
    } else {
      fprintf(stderr, "Unknown or incomplete option '%s' (see --help)\n", option);
      return 2;
    }
  }

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  pal_time_init();

  log_info("iotwin-bus starting");
  pal_rt_setup_and_log(&rt);

  BusServer* server = bus_server_create(&config);
  if (server == NULL) {
    return 1;
  }

  int exit_code = 0;
  while (!g_stop) {
    if (bus_server_poll(server, 200) != 0) {
      exit_code = 1;
      break;
    }
  }

  BusServerStats stats = bus_server_get_stats(server);
  log_info("iotwin-bus stopping: %llu frames routed, %llu rejected, %llu connections",
           (unsigned long long)stats.frames_routed, (unsigned long long)stats.frames_rejected,
           (unsigned long long)stats.clients_accepted);
  bus_server_destroy(server);
  return exit_code;
}
