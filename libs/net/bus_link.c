#include "bus_link.h"

#include <stdio.h>
#include <string.h>

#include "pal_time.h"

#define SEND_TIMEOUT_MS 200

bool bus_link_open(BusLink* link, const char* host, uint16_t port) {
  memset(link, 0, sizeof(*link));
  link->socket = net_connect_tcp(host, port);
  if (link->socket == NET_INVALID_SOCKET) {
    return false;
  }
  net_set_nonblocking(link->socket, true);
  line_reader_init(&link->reader);
  return true;
}

void bus_link_close(BusLink* link) {
  if (link->socket != NET_INVALID_SOCKET) {
    net_close(link->socket);
    link->socket = NET_INVALID_SOCKET;
  }
}

bool bus_link_send_line(BusLink* link, const char* line) {
  char text[LINE_READER_MAX_LINE + 1];
  const size_t length = strlen(line);
  if (length >= LINE_READER_MAX_LINE) {
    return false;
  }
  memcpy(text, line, length);
  text[length] = '\n';
  if (net_send_all(link->socket, text, length + 1u, SEND_TIMEOUT_MS) != 0) {
    link->lost = true;
    return false;
  }
  return true;
}

bool bus_link_send_frame(BusLink* link, const CanFrame* frame) {
  char text[CAN_FRAME_TEXT_MAX];
  if (can_frame_to_text(frame, text, sizeof(text)) < 0) {
    return false;
  }
  return bus_link_send_line(link, text);
}

static void on_line(const char* line, size_t length, void* user_data) {
  BusLink* link = (BusLink*)user_data;
  const BusLinkHandlers* handlers = link->handlers;
  if (length > 0 && (line[0] == '!' || line[0] == '@')) {
    if (line[0] == '!' && link->answer != NULL && !link->answered) {
      snprintf(link->answer, link->answer_size, "%.*s", (int)length, line);
      link->answered = true;
    }
    if (handlers != NULL && handlers->on_control != NULL) {
      handlers->on_control(handlers->context, line, length);
    }
    return;
  }
  CanFrame frame;
  if (handlers != NULL && handlers->on_frame != NULL && can_frame_from_text(line, length, &frame)) {
    handlers->on_frame(handlers->context, &frame);
  }
}

bool bus_link_poll(BusLink* link, int timeout_ms, const BusLinkHandlers* handlers) {
  if (link->lost) {
    return false;
  }
  NetPollFd entry = {link->socket, NET_POLL_IN, 0};
  if (net_poll(&entry, 1, timeout_ms) <= 0) {
    return true;
  }
  link->handlers = handlers;
  char buffer[2048];
  for (;;) {
    int received = net_recv(link->socket, buffer, sizeof(buffer));
    if (received > 0) {
      line_reader_feed(&link->reader, buffer, (size_t)received, on_line, link);
      continue;
    }
    if (received != NET_RECV_WOULD_BLOCK) {
      link->lost = true;
    }
    break;
  }
  link->handlers = NULL;
  return !link->lost;
}

bool bus_link_command(BusLink* link, const char* command, char* answer, size_t answer_size, int timeout_ms,
                      const BusLinkHandlers* handlers) {
  char line[LINE_READER_MAX_LINE];
  snprintf(line, sizeof(line), "!%s", command);
  char local[128];
  link->answer = answer != NULL ? answer : local;
  link->answer_size = answer != NULL ? answer_size : sizeof(local);
  link->answered = false;
  link->answer[0] = '\0';

  bool ok = bus_link_send_line(link, line);
  const uint64_t deadline = pal_time_monotonic_ms() + (uint64_t)timeout_ms;
  while (ok && !link->answered && pal_time_monotonic_ms() < deadline) {
    ok = bus_link_poll(link, 10, handlers);
  }
  const bool success = link->answered && (strncmp(link->answer, "!ok", 3) == 0 ||
                                          strncmp(link->answer, "!status", 7) == 0);
  link->answer = NULL;
  link->answered = false;
  return success;
}
