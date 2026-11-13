#ifndef IOTWIN_ISOTP_H
#define IOTWIN_ISOTP_H

/**
 * @file isotp.h
 * @brief ISO-TP (ISO 15765-2) transport protocol for classic CAN, normal addressing.
 *
 * Transfers up to 4095 bytes over 8-byte CAN frames:
 *
 *   SF  0L dd dd dd dd dd dd dd      single frame, L = 1..7
 *   FF  1L LL dd dd dd dd dd dd      first frame, 12-bit length 8..4095
 *   CF  2N dd dd dd dd dd dd dd      consecutive frame, N = sequence 1..15,0,...
 *   FC  3S BS ST                     flow control: S = 0 CTS / 1 WAIT / 2 OVFLW,
 *                                    BS = block size, ST = STmin
 *
 * The link is driven from outside: hand every received frame to
 * isotp_on_frame() and call isotp_poll() regularly with the current time.
 * Portable C without heap use (host and firmware).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ISOTP_MAX_LENGTH 4095u

typedef enum {
  ISOTP_OK = 0,
  ISOTP_BUSY,               /**< a transmission is already running */
  ISOTP_ERROR_LENGTH,       /**< 0 or more than 4095 bytes */
  ISOTP_ERROR_SEND,         /**< the CAN driver refused a frame */
  ISOTP_ERROR_TIMEOUT_BS,   /**< no flow control within N_Bs */
  ISOTP_ERROR_TIMEOUT_CR,   /**< no consecutive frame within N_Cr */
  ISOTP_ERROR_OVERFLOW,     /**< message larger than the receive buffer */
  ISOTP_ERROR_SEQUENCE,     /**< wrong sequence number */
  ISOTP_ERROR_WAIT_LIMIT,   /**< too many WAIT flow controls */
  ISOTP_ERROR_UNEXPECTED    /**< invalid flow status or reception interrupted */
} IsoTpResult;

typedef struct {
  uint32_t tx_id;            /**< CAN id of frames we send */
  uint32_t rx_id;            /**< CAN id of frames we accept */
  bool extended_id;
  uint8_t block_size;        /**< BS announced to the sender (0 = no further FC) */
  uint8_t st_min;            /**< STmin announced to the sender (raw encoding) */
  uint32_t timeout_us;       /**< N_Bs and N_Cr */
  uint8_t max_wait_frames;   /**< WFTmax */
  bool padding;              /**< always send 8 bytes, filled with padding_byte */
  uint8_t padding_byte;
  bool (*send)(void* context, const CanFrame* frame); /**< false = driver queue full */
  void* context;
} IsoTpConfig;

typedef struct {
  IsoTpConfig config;

  /* transmitter */
  int tx_state;
  uint8_t tx_buffer[ISOTP_MAX_LENGTH];
  size_t tx_length;
  size_t tx_offset;
  uint8_t tx_sequence;
  uint8_t tx_block_size;
  uint8_t tx_block_remaining;
  uint32_t tx_st_min_us;
  uint64_t tx_deadline_us;
  uint64_t tx_next_cf_us;
  uint8_t tx_wait_count;
  IsoTpResult tx_result;

  /* receiver */
  int rx_state;
  uint8_t* rx_buffer;
  size_t rx_capacity;
  size_t rx_length;
  size_t rx_offset;
  uint8_t rx_sequence;
  uint8_t rx_block_count;
  uint64_t rx_deadline_us;
  bool rx_complete;
  IsoTpResult rx_error;
} IsoTpLink;

/** @brief Defaults: BS 0, STmin 0, 1 s timeouts, WFTmax 10, padding with 0xCC. */
void isotp_default_config(IsoTpConfig* config, uint32_t tx_id, uint32_t rx_id);

/** @brief Initialises the link; received messages are stored in @p rx_buffer. */
void isotp_init(IsoTpLink* link, const IsoTpConfig* config, uint8_t* rx_buffer, size_t rx_capacity);

/** @brief Starts sending @p data (copied). Single frames go out immediately. */
IsoTpResult isotp_send(IsoTpLink* link, const uint8_t* data, size_t length, uint64_t now_us);

/** @brief Hands a received CAN frame to the link (frames with other ids are ignored). */
void isotp_on_frame(IsoTpLink* link, const CanFrame* frame, uint64_t now_us);

/** @brief Sends due consecutive frames and checks timeouts. */
void isotp_poll(IsoTpLink* link, uint64_t now_us);

/** @brief True (once) when a complete message is in the receive buffer. */
bool isotp_receive(IsoTpLink* link, size_t* length);

/** @brief True while a transmission is running. */
bool isotp_tx_busy(const IsoTpLink* link);

/** @brief Result of the last finished transmission. */
IsoTpResult isotp_tx_result(const IsoTpLink* link);

/** @brief Last receive error since the previous call (ISOTP_OK if none). */
IsoTpResult isotp_take_rx_error(IsoTpLink* link);

/** @brief STmin raw value to microseconds (reserved values mean 127 ms). */
uint32_t isotp_st_min_to_us(uint8_t st_min);

const char* isotp_result_name(IsoTpResult result);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_ISOTP_H */
