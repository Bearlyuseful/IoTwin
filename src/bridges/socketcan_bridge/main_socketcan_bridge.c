/**
 * @file main_socketcan_bridge.c
 * @brief socketcan_bridge: connects a Linux CAN interface (vcan0, can0, USB adapter) to iotwin-bus.
 *
 *   candump / cansend / real ECUs <-> can0 <-> socketcan_bridge <-TCP-> iotwin-bus
 *
 * SocketCAN does not deliver a socket's own frames back to it (CAN_RAW_RECV_OWN_MSGS
 * is off by default), so frames written by the bridge do not loop back.
 */

#define _DEFAULT_SOURCE

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "can_frame.h"
#include "cli.h"
#include "line_reader.h"
#include "log.h"
#include "net.h"
#include "pal_rt.h"
#include "pal_time.h"
#include "socketcan.h"

#define RETRY_INTERVAL_MS 1000

typedef struct {
  int can_fd;
  net_socket_t bus;
  LineReader reader;
  unsigned long long to_bus;
  unsigned long long to_can;
} Bridge;

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int signal_number) {
  (void)signal_number;
  g_stop = 1;
}

static void on_bus_line(const char* line, size_t length, void* user_data) {
  Bridge* bridge = (Bridge*)user_data;
  CanFrame frame;
  struct can_frame kernel_frame;
  if (!can_frame_from_text(line, length, &frame) || !socketcan_from_frame(&frame, &kernel_frame)) {
    log_warn("bus: ignoring invalid line '%.*s'", (int)(length > 40 ? 40 : length), line);
    return;
  }
  if (write(bridge->can_fd, &kernel_frame, sizeof(kernel_frame)) != (ssize_t)sizeof(kernel_frame)) {
    log_warn("writing to the CAN interface failed: %s", strerror(errno));
    return;
  }
  bridge->to_can++;
}

static void forward_can_frame(Bridge* bridge) {
  struct can_frame kernel_frame;
  ssize_t n = read(bridge->can_fd, &kernel_frame, sizeof(kernel_frame));
  if (n != (ssize_t)sizeof(kernel_frame)) {
    return;
  }
  CanFrame frame;
  char text[CAN_FRAME_TEXT_MAX + 1];
  if (!socketcan_to_frame(&kernel_frame, &frame)) {
    log_debug("ignoring CAN error frame");
    return;
  }
  int length = can_frame_to_text(&frame, text, CAN_FRAME_TEXT_MAX);
  if (length < 0 || bridge->bus == NET_INVALID_SOCKET) {
    return;
  }
  text[length++] = '\n';
  if (net_send_all(bridge->bus, text, (size_t)length, 100) != 0) {
    log_warn("sending to bus failed (%s), reconnecting", net_last_error());
    net_close(bridge->bus);
    bridge->bus = NET_INVALID_SOCKET;
    return;
  }
  bridge->to_bus++;
}

static void print_usage(const char* program) {
  printf("Usage: %s --interface IFACE [options]\n\n"
         "Connects a Linux SocketCAN interface to iotwin-bus.\n\n"
         "Options:\n"
         "  --interface IFACE     CAN interface, e.g. vcan0 or can0 (required)\n"
         "  --host HOST           bus server host (default 127.0.0.1)\n"
         "  --port PORT           bus server port (default 8080)\n"
         "%s"
         "  --help                show this help\n\n"
         "Virtual interface: sudo modprobe vcan && sudo ip link add vcan0 type vcan && sudo ip link set up vcan0\n",
         program, cli_common_usage());
}

int main(int argc, char** argv) {
  const char* interface_name = NULL;
  const char* host = "127.0.0.1";
  long port = 8080;
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
    } else if (strcmp(option, "--interface") == 0 && value) {
      interface_name = value;
      i++;
    } else if (strcmp(option, "--host") == 0 && value) {
      host = value;
      i++;
    } else if (strcmp(option, "--port") == 0 && cli_parse_long(value, 1, 65535, &port)) {
      i++;
    } else {
      fprintf(stderr, "Unknown or invalid option '%s' (see --help)\n", option);
      return 2;
    }
  }
  if (interface_name == NULL) {
    fprintf(stderr, "--interface is required (see --help)\n");
    return 2;
  }

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  pal_time_init();
  if (!net_init()) {
    return 1;
  }
  pal_rt_setup_and_log(&rt);

  Bridge bridge;
  memset(&bridge, 0, sizeof(bridge));
  bridge.bus = NET_INVALID_SOCKET;
  bridge.can_fd = socketcan_open(interface_name);
  if (bridge.can_fd < 0) {
    log_error("cannot open CAN interface %s: %s", interface_name, strerror(errno));
    return 1;
  }
  log_info("socketcan_bridge: %s <-> bus %s:%ld", interface_name, host, port);

  uint64_t next_connect = 0;
  bool reported = false;
  while (!g_stop) {
    const uint64_t now = pal_time_monotonic_ms();
    if (bridge.bus == NET_INVALID_SOCKET && now >= next_connect) {
      next_connect = now + RETRY_INTERVAL_MS;
      bridge.bus = net_connect_tcp(host, (uint16_t)port);
      if (bridge.bus == NET_INVALID_SOCKET) {
        if (!reported) {
          log_warn("bus %s:%ld not reachable (%s), retrying every second", host, port, net_last_error());
          reported = true;
        }
      } else {
        reported = false;
        net_set_nonblocking(bridge.bus, true);
        line_reader_init(&bridge.reader);
        log_info("connected to bus %s:%ld", host, port);
      }
    }

    struct pollfd fds[2] = {{bridge.can_fd, POLLIN, 0}, {bridge.bus, POLLIN, 0}};
    const nfds_t count = bridge.bus == NET_INVALID_SOCKET ? 1 : 2;
    if (poll(fds, count, 100) <= 0) {
      continue;
    }
    if (fds[0].revents & POLLIN) {
      forward_can_frame(&bridge);
    }
    if (count == 2 && (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
      char buffer[2048];
      for (;;) {
        int n = net_recv(bridge.bus, buffer, sizeof(buffer));
        if (n > 0) {
          line_reader_feed(&bridge.reader, buffer, (size_t)n, on_bus_line, &bridge);
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

  log_info("socketcan_bridge stopping: %llu frames to bus, %llu to %s", bridge.to_bus, bridge.to_can,
           interface_name);
  close(bridge.can_fd);
  net_close(bridge.bus);
  net_cleanup();
  return 0;
}
