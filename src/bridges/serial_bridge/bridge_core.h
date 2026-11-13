#ifndef IOTWIN_BRIDGE_CORE_H
#define IOTWIN_BRIDGE_CORE_H

/**
 * @file bridge_core.h
 * @brief Translation between an SLCAN adapter (serial) and iotwin-bus (TCP text lines).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief How the adapter's CAN controller is opened. */
typedef enum {
  BRIDGE_MODE_NORMAL,   /**< 'O': real bus, needs a transceiver and another node. */
  BRIDGE_MODE_LOOPBACK, /**< 'l': internal loopback, works with a bare board. */
  BRIDGE_MODE_LISTEN    /**< 'L': listen only. */
} BridgeMode;

/** @brief Parses "normal", "loopback" or "listen". */
bool bridge_parse_mode(const char* text, BridgeMode* mode);

/** @brief Command that opens the channel in @p mode ("O\r", "l\r" or "L\r"). */
const char* bridge_open_command(BridgeMode mode);

/** @brief Bitrate command, e.g. "S6\r" for 500 kbit/s. NULL if SLCAN has no index for it. */
const char* bridge_bitrate_command(uint32_t bitrate);

/**
 * @brief Converts a line from the adapter (without CR) into a bus line (without newline).
 * @return Length of the bus line, or -1 if the adapter line is not a frame.
 */
int bridge_adapter_to_bus(const char* line, size_t length, char* out, size_t out_size);

/**
 * @brief Converts a bus line into an SLCAN frame command including CR.
 * @return Length, or -1 if the bus line is not a valid frame.
 */
int bridge_bus_to_adapter(const char* line, size_t length, char* out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_BRIDGE_CORE_H */
