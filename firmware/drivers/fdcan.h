#ifndef IOTWIN_FDCAN_H
#define IOTWIN_FDCAN_H

/**
 * @file fdcan.h
 * @brief Register level driver for FDCAN1 of the STM32G0B1 in classic CAN mode.
 *
 * Message RAM (fixed layout on STM32G0/G4, 212 words per instance):
 *   0x000  28 standard filters      (1 word)
 *   0x070   8 extended filters      (2 words)
 *   0x0B0   3 Rx FIFO 0 elements    (18 words)
 *   0x188   3 Rx FIFO 1 elements    (18 words)
 *   0x260   3 Tx event FIFO entries (2 words)
 *   0x278   3 Tx buffers            (18 words)
 *
 * No acceptance filters are configured: every frame goes to Rx FIFO 0, the
 * interrupt copies it into a software queue. Transmission uses the Tx FIFO.
 */

#include <stdbool.h>
#include <stdint.h>

#include "can_frame.h"

typedef enum {
  FDCAN_MODE_NORMAL,
  FDCAN_MODE_INTERNAL_LOOPBACK,
  FDCAN_MODE_BUS_MONITORING
} FdcanMode;

typedef struct {
  uint8_t tx_error_count;
  uint8_t rx_error_count;
  bool error_warning;
  bool error_passive;
  bool bus_off;
  uint8_t last_error_code; /**< PSR.LEC: 0 none, 1 stuff, 2 form, 3 ack, 4 bit1, 5 bit0, 6 crc, 7 no change */
} FdcanStatus;

/** @brief Configures and starts the controller. False if the bitrate cannot be reached. */
bool fdcan_start(uint32_t kernel_clock_hz, uint32_t bitrate, FdcanMode mode);

/** @brief Stops the controller (initialisation mode). */
void fdcan_stop(void);

/** @brief Queues a frame for transmission. False if the Tx FIFO is full or the controller is stopped. */
bool fdcan_send(const CanFrame* frame);

/** @brief Takes the oldest received frame from the software queue. */
bool fdcan_receive(CanFrame* frame);

/** @brief True (once) if frames were lost since the last call. */
bool fdcan_take_overrun(void);

void fdcan_get_status(FdcanStatus* status);

/** @brief Call periodically: restarts the controller after bus-off. */
void fdcan_poll(void);

#endif /* IOTWIN_FDCAN_H */
