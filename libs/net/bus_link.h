#ifndef IOTWIN_BUS_LINK_H
#define IOTWIN_BUS_LINK_H

/**
 * @file bus_link.h
 * @brief Client connection to iotwin-bus for tools (replay, scenarios).
 *
 * Wraps the TCP socket and the line framing: frames are parsed and handed to
 * on_frame, control answers ("!ok", "!error ...", "!status ...") and test
 * bench lines ("@...") to on_control. Single threaded.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can_frame.h"
#include "line_reader.h"
#include "net.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void (*on_frame)(void* context, const CanFrame* frame);
  void (*on_control)(void* context, const char* line, size_t length);
  void* context;
} BusLinkHandlers;

typedef struct {
  net_socket_t socket;
  LineReader reader;
  bool lost;
  const BusLinkHandlers* handlers;
  /* answer capture for bus_link_command() */
  char* answer;
  size_t answer_size;
  bool answered;
} BusLink;

/** @brief Connects (blocking), then switches to non-blocking I/O. */
bool bus_link_open(BusLink* link, const char* host, uint16_t port);

void bus_link_close(BusLink* link);

/** @brief Sends one frame. */
bool bus_link_send_frame(BusLink* link, const CanFrame* frame);

/** @brief Sends a raw line (a line break is added), e.g. "@ecu fault short on". */
bool bus_link_send_line(BusLink* link, const char* line);

/**
 * @brief Waits up to @p timeout_ms for data and dispatches all complete lines.
 * @return false if the connection is lost.
 */
bool bus_link_poll(BusLink* link, int timeout_ms, const BusLinkHandlers* handlers);

/**
 * @brief Sends "!<command>" and waits for the server's answer line.
 * Frames arriving meanwhile are dispatched to @p handlers.
 * @return true if an answer starting with "!ok" or "!status" arrived; @p answer gets the line.
 */
bool bus_link_command(BusLink* link, const char* command, char* answer, size_t answer_size, int timeout_ms,
                      const BusLinkHandlers* handlers);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_BUS_LINK_H */
