/**
 * @file main_can_sim.c
 * @brief can_sim: simulated light control ECU on the IoTwin CAN bus.
 *
 * Threads:
 *  - main thread: cyclic control loop (optionally real-time), owns the socket
 *  - console thread: reads commands from stdin (not real-time)
 *
 * The control loop never blocks on the network while connected: it reads
 * whatever arrived, runs the ECU step, sends the outputs and sleeps until
 * the next absolute deadline.
 */

#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "can_bus.h"
#include "can_frame.h"
#include "cli.h"
#include "cycle_timer.h"
#include "light_ecu.h"
#include "line_reader.h"
#include "log.h"
#include "net.h"
#include "pal_rt.h"
#include "pal_time.h"

#define RECONNECT_INTERVAL_MS 1000
#define SEND_TIMEOUT_MS 50

typedef struct {
  const char* host;
  uint16_t port;
  uint32_t tick_ms;
  uint32_t cycle_ms;
  uint32_t blink_ms;
  uint32_t stats_interval_s;
  bool interactive;
  PalRtConfig rt;
} SimOptions;

typedef struct {
  pthread_mutex_t lock; /* protects ecu */
  LightEcu ecu;
  CanBus rx_queue; /* frames from the network, drained by the control loop */
  CanBus tx_queue; /* frames typed on the console, sent by the control loop */
  atomic_bool connected;
  atomic_uint_fast64_t frames_sent;
  atomic_uint_fast64_t frames_received;
} SimShared;

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int signal_number) {
  (void)signal_number;
  g_stop = 1;
}

/* ------------------------------------------------------------------------- */
/* Console                                                                    */
/* ------------------------------------------------------------------------- */

static void print_console_help(void) {
  printf("Commands:\n"
         "  lamp on|off|toggle   set the lamp command\n"
         "  blink <ms>|off       let the ECU blink the lamp\n"
         "  send <frame>         put any frame on the bus, e.g. send 123#DEADBEEF\n"
         "  status               show the current state\n"
         "  help                 show this help\n"
         "  quit                 stop the simulator\n");
  fflush(stdout);
}

static char* trim(char* text) {
  while (*text == ' ' || *text == '\t') {
    text++;
  }
  size_t length = strlen(text);
  while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r' ||
                        text[length - 1] == ' ' || text[length - 1] == '\t')) {
    text[--length] = '\0';
  }
  return text;
}

static void handle_command(SimShared* shared, char* line) {
  char* command = trim(line);
  if (*command == '\0') {
    return;
  }
  char* argument = strchr(command, ' ');
  if (argument != NULL) {
    *argument++ = '\0';
    argument = trim(argument);
  }

  if (strcmp(command, "help") == 0) {
    print_console_help();
  } else if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) {
    g_stop = 1;
  } else if (strcmp(command, "lamp") == 0 && argument != NULL) {
    pthread_mutex_lock(&shared->lock);
    if (strcmp(argument, "on") == 0) {
      light_ecu_set_lamp(&shared->ecu, true);
    } else if (strcmp(argument, "off") == 0) {
      light_ecu_set_lamp(&shared->ecu, false);
    } else if (strcmp(argument, "toggle") == 0) {
      light_ecu_set_lamp(&shared->ecu, !shared->ecu.lamp_on);
    } else {
      printf("usage: lamp on|off|toggle\n");
    }
    pthread_mutex_unlock(&shared->lock);
  } else if (strcmp(command, "blink") == 0 && argument != NULL) {
    long period = 0;
    if (strcmp(argument, "off") == 0) {
      period = 0;
    } else if (!cli_parse_long(argument, 50, 600000, &period)) {
      printf("usage: blink <50..600000 ms>|off\n");
      fflush(stdout);
      return;
    }
    pthread_mutex_lock(&shared->lock);
    light_ecu_set_blink(&shared->ecu, (uint32_t)period);
    pthread_mutex_unlock(&shared->lock);
  } else if (strcmp(command, "send") == 0 && argument != NULL) {
    CanFrame frame;
    if (!can_frame_from_text(argument, strlen(argument), &frame)) {
      printf("invalid frame '%s' (format: 123#DEADBEEF, 1F334455#11, 123#R)\n", argument);
    } else if (!can_bus_send(&shared->tx_queue, &frame)) {
      printf("send queue full, frame dropped\n");
    }
  } else if (strcmp(command, "status") == 0) {
    pthread_mutex_lock(&shared->lock);
    bool lamp = shared->ecu.lamp_on;
    uint32_t blink = shared->ecu.blink_period_ms;
    pthread_mutex_unlock(&shared->lock);
    printf("lamp: %s%s | bus: %s | sent: %llu | received: %llu\n", lamp ? "ON" : "OFF",
           blink ? " (blinking)" : "", atomic_load(&shared->connected) ? "connected" : "offline",
           (unsigned long long)atomic_load(&shared->frames_sent),
           (unsigned long long)atomic_load(&shared->frames_received));
  } else {
    printf("unknown command '%s' (type 'help')\n", command);
  }
  fflush(stdout);
}

static void* console_thread(void* argument) {
  SimShared* shared = (SimShared*)argument;
  char line[256];
  print_console_help();
  while (!g_stop && fgets(line, sizeof(line), stdin) != NULL) {
    handle_command(shared, line);
  }
  return NULL;
}

/* ------------------------------------------------------------------------- */
/* Network                                                                    */
/* ------------------------------------------------------------------------- */

static void on_line(const char* line, size_t length, void* user_data) {
  SimShared* shared = (SimShared*)user_data;
  CanFrame frame;
  if (!can_frame_from_text(line, length, &frame)) {
    log_warn("ignoring invalid line from bus: '%.*s'", (int)(length > 40 ? 40 : length), line);
    return;
  }
  if (!can_bus_send(&shared->rx_queue, &frame)) {
    log_warn("receive queue full, frame dropped");
  }
}

static bool send_frame(net_socket_t socket, SimShared* shared, const CanFrame* frame) {
  char text[CAN_FRAME_TEXT_MAX + 1];
  int length = can_frame_to_text(frame, text, CAN_FRAME_TEXT_MAX);
  if (length < 0) {
    return true; /* invalid frame, nothing to send */
  }
  text[length++] = '\n';
  if (net_send_all(socket, text, (size_t)length, SEND_TIMEOUT_MS) != 0) {
    return false;
  }
  atomic_fetch_add(&shared->frames_sent, 1);
  return true;
}

/* ------------------------------------------------------------------------- */
/* Main                                                                       */
/* ------------------------------------------------------------------------- */

static void print_usage(const char* program) {
  printf("Usage: %s [options]\n\n"
         "Simulated light control ECU. Connects to iotwin-bus, sends LAMP_CMD (0x100)\n"
         "and follows the wall switch SWITCH_STATE (0x200).\n\n"
         "Options:\n"
         "  --host HOST           bus server host (default 127.0.0.1)\n"
         "  --port PORT           bus server port (default 8080)\n"
         "  --tick-ms N           control loop period in ms (default 10)\n"
         "  --cycle-ms N          period of the cyclic LAMP_CMD in ms (default %u, 0 = off)\n"
         "  --blink-ms N          start blinking with this toggle period\n"
         "  --stats N             log loop timing statistics every N seconds\n"
         "  --no-console          do not read commands from stdin\n"
         "%s"
         "  --help                show this help\n",
         program, LIGHT_ECU_DEFAULT_CYCLE_MS, cli_common_usage());
}

static int parse_options(int argc, char** argv, SimOptions* options) {
  options->host = "127.0.0.1";
  options->port = 8080;
  options->tick_ms = 10;
  options->cycle_ms = LIGHT_ECU_DEFAULT_CYCLE_MS;
  options->blink_ms = 0;
  options->stats_interval_s = 0;
  options->interactive = true;
  pal_rt_default_config(&options->rt);

  for (int i = 1; i < argc; i++) {
    const char* option = argv[i];
    const char* value_text = i + 1 < argc ? argv[i + 1] : NULL;
    long value = 0;
    int common = cli_parse_common_option(argc, argv, &i, &options->rt);
    if (common < 0) {
      return 2;
    }
    if (common > 0) {
      continue;
    }
    if (strcmp(option, "--help") == 0 || strcmp(option, "-h") == 0) {
      print_usage(argv[0]);
      return 1;
    } else if (strcmp(option, "--host") == 0 && value_text) {
      options->host = value_text;
      i++;
    } else if (strcmp(option, "--port") == 0 && cli_parse_long(value_text, 1, 65535, &value)) {
      options->port = (uint16_t)value;
      i++;
    } else if (strcmp(option, "--tick-ms") == 0 && cli_parse_long(value_text, 1, 1000, &value)) {
      options->tick_ms = (uint32_t)value;
      i++;
    } else if (strcmp(option, "--cycle-ms") == 0 &&
               cli_parse_long(value_text, 0, 600000, &value)) {
      options->cycle_ms = (uint32_t)value;
      i++;
    } else if (strcmp(option, "--blink-ms") == 0 &&
               cli_parse_long(value_text, 50, 600000, &value)) {
      options->blink_ms = (uint32_t)value;
      i++;
    } else if (strcmp(option, "--stats") == 0 && cli_parse_long(value_text, 1, 3600, &value)) {
      options->stats_interval_s = (uint32_t)value;
      i++;
    } else if (strcmp(option, "--no-console") == 0) {
      options->interactive = false;
    } else {
      fprintf(stderr, "Unknown or incomplete option '%s' (see --help)\n", option);
      return 2;
    }
  }
  return 0;
}

static void log_cycle_stats(const CycleTimer* timer, uint32_t tick_ms) {
  const CycleStats* stats = &timer->stats;
  if (stats->samples == 0) {
    return;
  }
  log_info("loop %u ms: %llu cycles, wake-up latency min/avg/max = %.3f/%.3f/%.3f ms, "
           "overruns: %llu",
           tick_ms, (unsigned long long)stats->samples, (double)stats->min_latency_ns / 1e6,
           cycle_stats_mean_ns(stats) / 1e6, (double)stats->max_latency_ns / 1e6,
           (unsigned long long)stats->overruns);
}

int main(int argc, char** argv) {
  SimOptions options;
  int parse_result = parse_options(argc, argv, &options);
  if (parse_result != 0) {
    return parse_result == 1 ? 0 : parse_result;
  }

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  pal_time_init();
  if (!net_init()) {
    log_error("socket library initialisation failed: %s", net_last_error());
    return 1;
  }

  SimShared shared;
  pthread_mutex_init(&shared.lock, NULL);
  light_ecu_init(&shared.ecu, options.cycle_ms);
  if (options.blink_ms > 0) {
    light_ecu_set_blink(&shared.ecu, options.blink_ms);
  }
  can_bus_init(&shared.rx_queue);
  can_bus_init(&shared.tx_queue);
  atomic_init(&shared.connected, false);
  atomic_init(&shared.frames_sent, 0);
  atomic_init(&shared.frames_received, 0);

  log_info("can_sim (light ECU) starting, bus %s:%u", options.host, (unsigned)options.port);

  /* Start the console before the real-time setup so it does not inherit the RT priority. */
  if (options.interactive) {
    pthread_t console;
    if (pthread_create(&console, NULL, console_thread, &shared) == 0) {
      pthread_detach(console);
    } else {
      log_warn("console thread could not be started, running without console");
    }
  }
  pal_rt_setup_and_log(&options.rt);

  net_socket_t socket = NET_INVALID_SOCKET;
  LineReader reader;
  uint64_t next_connect_ms = 0;
  bool offline_reported = false;
  bool last_reported_lamp = false;
  bool lamp_reported = false;

  CycleTimer timer;
  cycle_timer_start(&timer, (uint64_t)options.tick_ms * PAL_NS_PER_MS);
  uint64_t next_stats_ms = pal_time_monotonic_ms() + options.stats_interval_s * 1000ULL;

  while (!g_stop) {
    const uint64_t now_ms = pal_time_monotonic_ms();

    /* --- connection handling --- */
    if (socket == NET_INVALID_SOCKET && now_ms >= next_connect_ms) {
      socket = net_connect_tcp(options.host, options.port);
      if (socket != NET_INVALID_SOCKET) {
        net_set_nonblocking(socket, true);
        line_reader_init(&reader);
        atomic_store(&shared.connected, true);
        offline_reported = false;
        log_info("connected to bus %s:%u", options.host, (unsigned)options.port);
        pthread_mutex_lock(&shared.lock);
        light_ecu_request_send(&shared.ecu);
        pthread_mutex_unlock(&shared.lock);
        /* Connecting blocks; restart the timing so the stats only cover the control loop. */
        cycle_timer_start(&timer, (uint64_t)options.tick_ms * PAL_NS_PER_MS);
      } else {
        if (!offline_reported) {
          log_warn("bus %s:%u not reachable (%s), retrying every %d ms", options.host,
                   (unsigned)options.port, net_last_error(), RECONNECT_INTERVAL_MS);
          offline_reported = true;
        }
        next_connect_ms = now_ms + RECONNECT_INTERVAL_MS;
      }
    }

    /* --- receive --- */
    if (socket != NET_INVALID_SOCKET) {
      char buffer[1024];
      for (;;) {
        int received = net_recv(socket, buffer, sizeof(buffer));
        if (received > 0) {
          line_reader_feed(&reader, buffer, (size_t)received, on_line, &shared);
          continue;
        }
        if (received == NET_RECV_WOULD_BLOCK) {
          break;
        }
        log_warn("connection to bus lost%s%s", received == NET_RECV_ERROR ? ": " : "",
                 received == NET_RECV_ERROR ? net_last_error() : "");
        net_close(socket);
        socket = NET_INVALID_SOCKET;
        atomic_store(&shared.connected, false);
        next_connect_ms = now_ms + RECONNECT_INTERVAL_MS;
        break;
      }
    }

    /* --- control step --- */
    CanFrame frame;
    CanFrame output;
    bool send_output = false;
    bool state_changed = false;
    pthread_mutex_lock(&shared.lock);
    while (can_bus_receive(&shared.rx_queue, &frame)) {
      atomic_fetch_add(&shared.frames_received, 1);
      char text[CAN_FRAME_TEXT_MAX];
      can_frame_to_text(&frame, text, sizeof(text));
      if (light_ecu_on_frame(&shared.ecu, &frame)) {
        log_info("RX %-12s switch -> %s", text, (frame.data[0] & 1u) ? "ON" : "OFF");
      } else {
        log_debug("RX %s", text);
      }
    }
    send_output = light_ecu_step(&shared.ecu, now_ms, &output, &state_changed);
    const bool lamp_on = shared.ecu.lamp_on;
    pthread_mutex_unlock(&shared.lock);

    /* --- send --- */
    if (send_output) {
      char text[CAN_FRAME_TEXT_MAX];
      can_frame_to_text(&output, text, sizeof(text));
      if (!lamp_reported || lamp_on != last_reported_lamp) {
        log_info("TX %-12s lamp %s", text, lamp_on ? "ON" : "OFF");
        last_reported_lamp = lamp_on;
        lamp_reported = true;
      } else {
        log_debug("TX %s", text);
      }
    }
    if (socket != NET_INVALID_SOCKET) {
      bool ok = !send_output || send_frame(socket, &shared, &output);
      while (ok && can_bus_receive(&shared.tx_queue, &frame)) {
        char text[CAN_FRAME_TEXT_MAX];
        can_frame_to_text(&frame, text, sizeof(text));
        log_info("TX %s (console)", text);
        ok = send_frame(socket, &shared, &frame);
      }
      if (!ok) {
        log_warn("sending to bus failed (%s), reconnecting", net_last_error());
        net_close(socket);
        socket = NET_INVALID_SOCKET;
        atomic_store(&shared.connected, false);
        next_connect_ms = now_ms + RECONNECT_INTERVAL_MS;
      }
    }
    (void)state_changed;

    /* --- statistics --- */
    if (options.stats_interval_s > 0 && now_ms >= next_stats_ms) {
      log_cycle_stats(&timer, options.tick_ms);
      cycle_stats_reset(&timer.stats);
      next_stats_ms = now_ms + options.stats_interval_s * 1000ULL;
    }

    cycle_timer_wait(&timer);
  }

  log_info("can_sim stopping");
  if (options.stats_interval_s > 0) {
    log_cycle_stats(&timer, options.tick_ms);
  }
  net_close(socket);
  net_cleanup();
  can_bus_destroy(&shared.rx_queue);
  can_bus_destroy(&shared.tx_queue);
  /* The console thread may still block in fgets(); exiting the process ends it. */
  return 0;
}
