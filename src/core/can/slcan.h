#ifndef IOTWIN_SLCAN_H
#define IOTWIN_SLCAN_H

/**
 * @file slcan.h
 * @brief SLCAN (Lawicel CAN232/CANUSB) ASCII protocol used on serial links.
 *
 * Commands are terminated by a carriage return. The adapter answers with
 * CR (ok) or BEL (0x07, error). Frames use the same syntax in both directions:
 *
 *   t1232DEAD    standard frame, id 0x123, DLC 2, data DE AD
 *   T1F3344551AA extended frame, id 0x1F334455, DLC 1
 *   r1230        standard remote frame, DLC 0
 *   R1F3344552   extended remote frame, DLC 2
 *
 * Supported commands: O (open), L (listen only), C (close), S0..S8 (bitrate),
 * V (version), N (serial number), F (status flags), Z0/Z1 (timestamps, accepted
 * but ignored). IoTwin extensions: l (open in internal loopback mode) and
 * Q (statistics). Portable C without libc formatting, used by the host bridge
 * and the STM32 firmware.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest line including terminator: 'T' + 8 id + 1 DLC + 16 data + 4 timestamp + CR. */
#define SLCAN_MAX_LINE 32

#define SLCAN_OK "\r"
#define SLCAN_ERROR "\a"

/** @brief Command types. */
typedef enum {
  SLCAN_CMD_FRAME,         /**< t, T, r, R */
  SLCAN_CMD_OPEN,          /**< O */
  SLCAN_CMD_OPEN_LOOPBACK, /**< l (IoTwin extension) */
  SLCAN_CMD_OPEN_LISTEN,   /**< L */
  SLCAN_CMD_CLOSE,         /**< C */
  SLCAN_CMD_BITRATE,       /**< S0..S8 */
  SLCAN_CMD_VERSION,       /**< V */
  SLCAN_CMD_SERIAL,        /**< N */
  SLCAN_CMD_STATUS,        /**< F */
  SLCAN_CMD_TIMESTAMP,     /**< Z0 / Z1 */
  SLCAN_CMD_STATISTICS     /**< Q (IoTwin extension) */
} SlcanCommandType;

/** @brief A parsed command. */
typedef struct {
  SlcanCommandType type;
  CanFrame frame;   /**< SLCAN_CMD_FRAME */
  uint32_t bitrate; /**< SLCAN_CMD_BITRATE, bit/s */
  bool enable;      /**< SLCAN_CMD_TIMESTAMP */
} SlcanCommand;

/**
 * @brief Parses one line (without its terminator).
 * @return false for unknown commands and malformed frames (answer with BEL).
 */
bool slcan_parse(const char* line, size_t length, SlcanCommand* command);

/**
 * @brief Encodes @p frame including the trailing CR.
 * @return Number of characters written (excluding NUL), -1 if invalid or too small.
 */
int slcan_format_frame(const CanFrame* frame, char* buffer, size_t buffer_size);

/** @brief Bitrate of S0..S8 (10k, 20k, 50k, 100k, 125k, 250k, 500k, 800k, 1M); 0 if invalid. */
uint32_t slcan_bitrate_from_index(unsigned index);

/** @brief Index for an S command, -1 if the bitrate has none. */
int slcan_index_from_bitrate(uint32_t bitrate);

/** @brief Writes @p value as @p digits upper-case hex characters (no terminator). */
void slcan_put_hex(char* out, uint32_t value, unsigned digits);

/* ------------------------------------------------------------------------- */
/* Stream splitting                                                          */
/* ------------------------------------------------------------------------- */

/**
 * @brief Called for each complete line (NUL terminated, without terminator).
 *
 * An empty line is a plain CR, i.e. an "ok" answer. A BEL byte is reported as
 * the one-character line "\a" (error answer); BEL needs no terminator.
 */
typedef void (*SlcanLineCallback)(const char* line, size_t length, void* user_data);

/** @brief Splits a byte stream at CR or LF (CR LF counts once). */
typedef struct {
  char buffer[SLCAN_MAX_LINE];
  size_t length;
  bool discarding;      /**< Current line too long, skipped until its end. */
  bool last_was_cr;     /**< Swallows the LF of a CR LF pair. */
  uint32_t overflows;   /**< Over-long lines dropped. */
} SlcanReader;

void slcan_reader_init(SlcanReader* reader);

/** @brief Feeds bytes; @p callback runs for every complete line or BEL. */
void slcan_reader_feed(SlcanReader* reader, const char* data, size_t length,
                       SlcanLineCallback callback, void* user_data);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_SLCAN_H */
