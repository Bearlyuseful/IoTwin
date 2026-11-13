/**
 * @file main_serial_bridge.c
 * @brief serial_bridge: connects an SLCAN adapter (e.g. the IoTwin STM32 gateway) to iotwin-bus.
 *
 *   SLCAN adapter <--serial--> serial_bridge <--TCP--> iotwin-bus <--> other nodes
 *
 * Single threaded: the loop alternates between the serial port and the bus
 * socket with short timeouts (works the same on Windows and Linux).
 * Reconnects to both sides automatically.
 */

#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "bridge_core.h"
#include "cli.h"
#include "line_reader.h"
#include "log.h"
#include "net.h"
#include "pal_time.h"
#include "serial_port.h"
#include "slcan.h"

#define RETRY_INTERVAL_MS 1000
#define SERIAL_POLL_MS 5
#define BUS_POLL_MS 5
#define COMMAND_TIMEOUT_MS 500
#define SEND_TIMEOUT_MS 100

typedef struct {
  const char* serial_name;
  uint32_t baudrate;
  const char* bus_host;
  uint16_t bus_port;
  uint32_t bitrate;
  BridgeMode mode;
  uint32_t stats_interval_s;
} Options;

typedef struct {
  Options options;
  SerialPort* serial;
  SlcanReader slcan;
  net_socket_t bus;
  LineReader bus_reader;
  uint64_t frames_to_bus;
  uint64_t frames_to_adapter;
  uint64_t adapter_errors;
  uint64_t dropped_offline;
} Bridge;

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int signal_number) {
  (void)signal_number;
  g_stop = 1;
}

/* ------------------------------------------------------------------------- */
/* Adapter side                                                               */
/* ------------------------------------------------------------------------- */

static void log_statistics_reply(const char* line, size_t length) {
  /* Q<tx 8><rx 8><echoes 8><tec 2><rec 2><flags 2> */
  if (length != 31) {
    log_warn("adapter: unexpected statistics reply '%.*s'", (int)length, line);
    return;
  }
  unsigned long tx = 0, rx = 0, echoes = 0;
  unsigned tec = 0, rec = 0, flags = 0;
  if (sscanf(line + 1, "%8lx%8lx%8lx%2x%2x%2x", &tx, &rx, &echoes, &tec, &rec, &flags) == 6) {
    log_info("adapter: CAN tx %lu, rx %lu (loopback echoes %lu), TEC %u, REC %u, flags 0x%02X", tx, rx,
             echoes, tec, rec, flags);
  }
}

static void on_adapter_line(const char* line, size_t length, void* user_data) {
  Bridge* bridge = (Bridge*)user_data;
  if (length == 0 || (length == 1 && (line[0] == 'z' || line[0] == 'Z'))) {
    return; /* ok / transmit acknowledged */
  }
  if (length == 1 && line[0] == '\a') {
    bridge->adapter_errors++;
    log_warn("adapter answered with an error (BEL)");
    return;
  }
  if (line[0] == 'Q') {
    log_statistics_reply(line, length);
    return;
  }
  if (line[0] == 'V' || line[0] == 'N' || line[0] == 'F') {
    log_info("adapter: %.*s", (int)length, line);
    return;
  }

  char text[CAN_FRAME_TEXT_MAX + 1];
  int text_length = bridge_adapter_to_bus(line, length, text, CAN_FRAME_TEXT_MAX);
  if (text_length < 0) {
    log_warn("adapter: ignoring unexpected line '%.*s'", (int)(length > 40 ? 40 : length), line);
    return;
  }
  if (bridge->bus == NET_INVALID_SOCKET) {
    bridge->dropped_offline++;
    return;
  }
  log_debug("adapter -> bus %s", text);
  text[text_length++] = '\n';
  if (net_send_all(bridge->bus, text, (size_t)text_length, SEND_TIMEOUT_MS) != 0) {
    log_warn("sending to bus failed (%s), reconnecting", net_last_error());
    net_close(bridge->bus);
    bridge->bus = NET_INVALID_SOCKET;
    return;
  }
  bridge->frames_to_bus++;
}

/* Sends a command and waits for its ok/error answer (other lines are processed normally). */
static bool adapter_command(Bridge* bridge, const char* command) {
  if (serial_write_all(bridge->serial, command, strlen(command)) != 0) {
    return false;
  }
  const uint64_t errors_before = bridge->adapter_errors;
  const uint64_t deadline = pal_time_monotonic_ms() + COMMAND_TIMEOUT_MS;
  char buffer[64];
  while (pal_time_monotonic_ms() < deadline) {
    int n = serial_read(bridge->serial, buffer, sizeof(buffer), 20);
    if (n < 0) {
      return false;
    }
    for (int i = 0; i < n; i++) {
      if (buffer[i] == '\r' || buffer[i] == '\a') {
        /* hand everything up to and including the answer to the reader */
        slcan_reader_feed(&bridge->slcan, buffer, (size_t)i + 1u, on_adapter_line, bridge);
        if (i + 1 < n) {
          slcan_reader_feed(&bridge->slcan, buffer + i + 1, (size_t)(n - i - 1), on_adapter_line, bridge);
        }
        return bridge->adapter_errors == errors_before;
      }
    }
    slcan_reader_feed(&bridge->slcan, buffer, (size_t)n, on_adapter_line, bridge);
  }
  log_warn("adapter: no answer to '%.*s'", (int)strcspn(command, "\r"), command);
  return false;
}

static bool configure_adapter(Bridge* bridge) {
  const char* bitrate_command = bridge_bitrate_command(bridge->options.bitrate);
  adapter_command(bridge, "C\r"); /* may fail if the channel is already closed */
  adapter_command(bridge, "V\r");
  if (!adapter_command(bridge, bitrate_command)) {
    log_error("adapter rejected bitrate %u", (unsigned)bridge->options.bitrate);
    return false;
  }
  if (!adapter_command(bridge, bridge_open_command(bridge->options.mode))) {
    log_error("adapter could not open the CAN channel");
    return false;
  }
  static const char* const mode_names[] = {"normal", "loopback", "listen-only"};
  log_info("adapter channel open: %u bit/s, %s mode", (unsigned)bridge->options.bitrate,
           mode_names[bridge->options.mode]);
  return true;
}

static void close_serial(Bridge* bridge) {
  if (bridge->serial != NULL) {
    serial_close(bridge->serial);
    bridge->serial = NULL;
  }
}

/* ------------------------------------------------------------------------- */
/* Bus side                                                                   */
/* ------------------------------------------------------------------------- */

static void on_bus_line(const char* line, size_t length, void* user_data) {
  Bridge* bridge = (Bridge*)user_data;
  char command[SLCAN_MAX_LINE];
  int command_length = bridge_bus_to_adapter(line, length, command, sizeof(command));
  if (command_length < 0) {
    log_warn("bus: ignoring invalid line '%.*s'", (int)(length > 40 ? 40 : length), line);
    return;
  }
  if (bridge->serial == NULL) {
    bridge->dropped_offline++;
    return;
  }
  log_debug("bus -> adapter %.*s", (int)length, line);
  if (serial_write_all(bridge->serial, command, (size_t)command_length) != 0) {
    log_warn("writing to the adapter failed (%s)", serial_last_error());
    close_serial(bridge);
    return;
  }
  bridge->frames_to_adapter++;
}

/* ------------------------------------------------------------------------- */
/* Main                                                                       */
/* ------------------------------------------------------------------------- */

static void print_usage(const char* program) {
  printf("Usage: %s --serial PORT [options]\n\n"
         "Connects an SLCAN adapter (e.g. the IoTwin NUCLEO-G0B1RE gateway) to iotwin-bus.\n\n"
         "Options:\n"
         "  --serial PORT         serial port, e.g. COM5 or /dev/ttyACM0 (required)\n"
         "  --baud N              serial baudrate (default 115200)\n"
         "  --host HOST           bus server host (default 127.0.0.1)\n"
         "  --port PORT           bus server port (default 8080)\n"
         "  --bitrate N           CAN bitrate in bit/s (default 500000; 10k..1M SLCAN steps)\n"
         "  --mode MODE           loopback (default, bare board) | normal (real bus) | listen\n"
         "  --stats N             log adapter statistics every N seconds\n"
         "%s"
         "  --help                show this help\n",
         program, cli_common_usage());
}

static int parse_options(int argc, char** argv, Options* options) {
  options->serial_name = NULL;
  options->baudrate = 115200;
  options->bus_host = "127.0.0.1";
  options->bus_port = 8080;
  options->bitrate = 500000;
  options->mode = BRIDGE_MODE_LOOPBACK;
  options->stats_interval_s = 0;
  PalRtConfig rt; /* accepted for consistency, not used */
  pal_rt_default_config(&rt);

  for (int i = 1; i < argc; i++) {
    const char* option = argv[i];
    const char* value = i + 1 < argc ? argv[i + 1] : NULL;
    long number = 0;
    int common = cli_parse_common_option(argc, argv, &i, &rt);
    if (common < 0) {
      return 2;
    }
    if (common > 0) {
      continue;
    }
    if (strcmp(option, "--help") == 0 || strcmp(option, "-h") == 0) {
      print_usage(argv[0]);
      return 1;
    } else if (strcmp(option, "--serial") == 0 && value) {
      options->serial_name = value;
      i++;
    } else if (strcmp(option, "--baud") == 0 && cli_parse_long(value, 1200, 4000000, &number)) {
      options->baudrate = (uint32_t)number;
      i++;
    } else if (strcmp(option, "--host") == 0 && value) {
      options->bus_host = value;
      i++;
    } else if (strcmp(option, "--port") == 0 && cli_parse_long(value, 1, 65535, &number)) {
      options->bus_port = (uint16_t)number;
      i++;
    } else if (strcmp(option, "--bitrate") == 0 && cli_parse_long(value, 1, 1000000, &number) &&
               bridge_bitrate_command((uint32_t)number) != NULL) {
      options->bitrate = (uint32_t)number;
      i++;
    } else if (strcmp(option, "--mode") == 0 && value && bridge_parse_mode(value, &options->mode)) {
      i++;
    } else if (strcmp(option, "--stats") == 0 && cli_parse_long(value, 1, 3600, &number)) {
      options->stats_interval_s = (uint32_t)number;
      i++;
    } else {
      fprintf(stderr, "Unknown or invalid option '%s' (see --help)\n", option);
      return 2;
    }
  }
  if (options->serial_name == NULL) {
    fprintf(stderr, "--serial PORT is required (see --help)\n");
    return 2;
  }
  return 0;
}

int main(int argc, char** argv) {
  Bridge bridge;
  memset(&bridge, 0, sizeof(bridge));
  int parse_result = parse_options(argc, argv, &bridge.options);
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

  bridge.bus = NET_INVALID_SOCKET;
  slcan_reader_init(&bridge.slcan);
  log_info("serial_bridge starting: %s @ %u baud <-> bus %s:%u", bridge.options.serial_name,
           (unsigned)bridge.options.baudrate, bridge.options.bus_host, (unsigned)bridge.options.bus_port);

  uint64_t next_serial_try = 0;
  uint64_t next_bus_try = 0;
  bool serial_reported = false;
  bool bus_reported = false;
  uint64_t next_stats = pal_time_monotonic_ms() + bridge.options.stats_interval_s * 1000ull;

  while (!g_stop) {
    const uint64_t now = pal_time_monotonic_ms();

    /* --- serial port --- */
    if (bridge.serial == NULL && now >= next_serial_try) {
      next_serial_try = now + RETRY_INTERVAL_MS;
      bridge.serial = serial_open(bridge.options.serial_name, bridge.options.baudrate);
      if (bridge.serial == NULL) {
        if (!serial_reported) {
          log_warn("cannot open %s (%s), retrying every second", bridge.options.serial_name,
                   serial_last_error());
          serial_reported = true;
        }
      } else {
        serial_reported = false;
        slcan_reader_init(&bridge.slcan);
        log_info("serial port %s open", bridge.options.serial_name);
        if (!configure_adapter(&bridge)) {
          close_serial(&bridge);
        }
      }
    }

    /* --- bus --- */
    if (bridge.bus == NET_INVALID_SOCKET && now >= next_bus_try) {
      next_bus_try = now + RETRY_INTERVAL_MS;
      bridge.bus = net_connect_tcp(bridge.options.bus_host, bridge.options.bus_port);
      if (bridge.bus == NET_INVALID_SOCKET) {
        if (!bus_reported) {
          log_warn("bus %s:%u not reachable (%s), retrying every second", bridge.options.bus_host,
                   (unsigned)bridge.options.bus_port, net_last_error());
          bus_reported = true;
        }
      } else {
        bus_reported = false;
        net_set_nonblocking(bridge.bus, true);
        line_reader_init(&bridge.bus_reader);
        log_info("connected to bus %s:%u", bridge.options.bus_host, (unsigned)bridge.options.bus_port);
      }
    }

    /* --- adapter -> bus --- */
    if (bridge.serial != NULL) {
      char buffer[512];
      int n = serial_read(bridge.serial, buffer, sizeof(buffer), SERIAL_POLL_MS);
      if (n > 0) {
        slcan_reader_feed(&bridge.slcan, buffer, (size_t)n, on_adapter_line, &bridge);
      } else if (n < 0) {
        log_warn("serial port error (%s), reopening", serial_last_error());
        close_serial(&bridge);
      }
    }

    /* --- bus -> adapter --- */
    if (bridge.bus != NET_INVALID_SOCKET) {
      NetPollFd entry = {bridge.bus, NET_POLL_IN, 0};
      if (net_poll(&entry, 1, BUS_POLL_MS) > 0) {
        char buffer[1024];
        for (;;) {
          int n = net_recv(bridge.bus, buffer, sizeof(buffer));
          if (n > 0) {
            line_reader_feed(&bridge.bus_reader, buffer, (size_t)n, on_bus_line, &bridge);
            continue;
          }
          if (n == NET_RECV_WOULD_BLOCK) {
            break;
          }
          log_warn("connection to bus lost, reconnecting");
          net_close(bridge.bus);
          bridge.bus = NET_INVALID_SOCKET;
          break;
        }
      }
    }

    if (bridge.serial == NULL && bridge.bus == NET_INVALID_SOCKET) {
      pal_sleep_ms(50);
    }

    /* --- statistics --- */
    if (bridge.options.stats_interval_s > 0 && now >= next_stats) {
      next_stats = now + bridge.options.stats_interval_s * 1000ull;
      log_info("bridge: %llu frames adapter->bus, %llu bus->adapter, %llu dropped while offline, "
               "%llu adapter errors",
               (unsigned long long)bridge.frames_to_bus, (unsigned long long)bridge.frames_to_adapter,
               (unsigned long long)bridge.dropped_offline, (unsigned long long)bridge.adapter_errors);
      if (bridge.serial != NULL) {
        serial_write_all(bridge.serial, "Q\r", 2);
      }
    }
  }

  log_info("serial_bridge stopping");
  if (bridge.serial != NULL) {
    serial_write_all(bridge.serial, "C\r", 2);
    close_serial(&bridge);
  }
  net_close(bridge.bus);
  net_cleanup();
  return 0;
}
