#ifndef IOTWIN_SOCKETCAN_H
#define IOTWIN_SOCKETCAN_H

/**
 * @file socketcan.h
 * @brief Linux SocketCAN raw sockets and conversion to/from CanFrame (Linux only).
 */

#include <linux/can.h>
#include <stdbool.h>

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opens a CAN_RAW socket bound to @p interface_name (e.g. "vcan0"); -1 on error (errno set). */
int socketcan_open(const char* interface_name);

/** @brief Converts a kernel frame; false for error frames. */
bool socketcan_to_frame(const struct can_frame* in, CanFrame* out);

/** @brief Converts to a kernel frame; false if @p in is invalid. */
bool socketcan_from_frame(const CanFrame* in, struct can_frame* out);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_SOCKETCAN_H */
