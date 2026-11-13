#ifndef IOTWIN_GATEWAY_H
#define IOTWIN_GATEWAY_H

/**
 * @file gateway.h
 * @brief Application logic of the IoTwin CAN gateway firmware (hardware independent).
 *
 * The board is two things at once:
 *  - an SLCAN adapter: frames from the serial link are sent on the CAN bus,
 *    frames received from CAN are reported on the serial link;
 *  - a small CAN node: the user button is a wall switch (SWITCH_STATE 0x200)
 *    and the LED shows the lamp command (LAMP_CMD 0x100), dimmed with its
 *    brightness byte (PWM);
 *  - a UDS server (requests 0x7E1 or functional 0x7DF, responses 0x7E9) for
 *    testers on the serial side or on the CAN bus; responses go to both.
 *
 * In internal loopback mode the controller receives its own frames. They are
 * matched against the transmitted ones and not reported a second time, so the
 * virtual bus does not see echoes.
 *
 * All hardware access goes through GatewayIo, so this file is unit tested on
 * the host with fake I/O.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can_frame.h"
#include "slcan.h"
#include "uds.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GATEWAY_ID_LAMP_CMD 0x100u
#define GATEWAY_ID_SWITCH_STATE 0x200u
#define GATEWAY_DEFAULT_BITRATE 500000u
#define GATEWAY_ECHO_SLOTS 8u
#define GATEWAY_BLINK_PERIOD_MS 500u

/** @name Diagnostics (see docs/dev/diagnostics.md) */
/**@{*/
#define GATEWAY_DIAG_REQUEST_ID 0x7E1u
#define GATEWAY_DIAG_RESPONSE_ID 0x7E9u
#define GATEWAY_DTC_BUS_OFF 0xC07388u    /**< U0073-88 control module communication bus off */
#define GATEWAY_DTC_COUNT 1u
#define GATEWAY_DID_LAMP 0x0100u         /**< 1 byte, last LAMP_CMD */
#define GATEWAY_DID_SWITCH 0x0101u       /**< 1 byte, wall switch (button) */
#define GATEWAY_DID_CAN_ERRORS 0x0200u   /**< TEC, REC, state (0 active, 1 warning, 2 passive, 3 bus off) */
#define GATEWAY_DID_COUNTERS 0x0201u     /**< uint32 frames to CAN, from CAN, echoes */
#define GATEWAY_DID_UPTIME 0x0202u       /**< uint32 ms */
#define GATEWAY_DID_CHANNEL 0x0203u      /**< open, mode (0 normal, 1 loopback, 2 listen), uint32 bitrate */
#define GATEWAY_DID_LED 0x0204u          /**< LED brightness: commanded %, measured % (pin sampling) */
#define GATEWAY_RESET_DELAY_MS 50u       /**< lets the ECUReset response leave UART and CAN */
/**@}*/

/** @brief Operating modes of the CAN controller. */
typedef enum {
  GATEWAY_CAN_NORMAL,   /**< Normal bus operation (needs a transceiver and another node). */
  GATEWAY_CAN_LOOPBACK, /**< Internal loopback: TX is received by the own controller. */
  GATEWAY_CAN_LISTEN    /**< Bus monitoring: receive only, never drive the bus. */
} GatewayCanMode;

/** @brief Controller state reported by the driver. */
typedef struct {
  uint8_t tx_error_count;
  uint8_t rx_error_count;
  bool error_warning;
  bool error_passive;
  bool bus_off;
  bool bus_error; /**< Last protocol error code != none. */
} GatewayCanStatus;

/** @brief Hardware interface (implemented by the board, faked in tests). */
typedef struct {
  void (*serial_write)(void* context, const char* data, size_t length);
  bool (*can_start)(void* context, uint32_t bitrate, GatewayCanMode mode);
  void (*can_stop)(void* context);
  bool (*can_send)(void* context, const CanFrame* frame);
  void (*can_status)(void* context, GatewayCanStatus* status);
  void (*led_set_level)(void* context, uint8_t percent); /**< 0 = off, 100 = full */
  /** Optional: duty cycle measured on the LED pin (self test of the PWM). */
  uint8_t (*led_measure)(void* context);
  /** Optional: restarts the MCU (UDS ECUReset hard). */
  void (*system_reset)(void* context);
  /** Optional: 96-bit unique device id (UDS serial number F18C). */
  void (*unique_id)(void* context, uint8_t out[12]);
  void* context;
} GatewayIo;

/** @brief SLCAN status flags (command F). */
enum {
  GATEWAY_FLAG_RX_QUEUE_FULL = 0x01,
  GATEWAY_FLAG_TX_QUEUE_FULL = 0x02,
  GATEWAY_FLAG_ERROR_WARNING = 0x04,
  GATEWAY_FLAG_DATA_OVERRUN = 0x08,
  GATEWAY_FLAG_ERROR_PASSIVE = 0x20,
  GATEWAY_FLAG_BUS_ERROR = 0x80
};

/** @brief Gateway state. */
typedef struct {
  GatewayIo io;
  SlcanReader reader;
  bool open;
  GatewayCanMode mode;
  uint32_t bitrate;

  bool switch_on; /**< State of the local wall switch (button). */
  bool lamp_on;   /**< Last LAMP_CMD seen on the bus. */
  uint8_t lamp_level; /**< Brightness of the last LAMP_CMD (100 if it had none). */
  bool led_on;
  uint8_t led_level;
  uint32_t last_blink_ms;

  CanFrame echo[GATEWAY_ECHO_SLOTS]; /**< Loopback mode: transmitted, echo not seen yet. */
  size_t echo_head;
  size_t echo_count;

  uint8_t sticky_flags; /**< Flags reported (and cleared) by the next F command. */
  uint32_t tx_frames;   /**< Frames handed to the CAN controller. */
  uint32_t rx_frames;   /**< Frames received from the CAN controller (incl. echoes). */
  uint32_t echoes;      /**< Loopback echoes recognised and suppressed. */

  UdsServer uds;
  UdsDtc dtcs[GATEWAY_DTC_COUNT];
  uint32_t now_ms;         /**< Time of the last tick. */
  bool reset_scheduled;    /**< ECUReset hard: restart at reset_due_ms. */
  uint32_t reset_due_ms;
  uint32_t soft_resets;
} Gateway;

void gateway_init(Gateway* gateway, const GatewayIo* io);

/** @brief Bytes received on the serial link. */
void gateway_on_serial_data(Gateway* gateway, const char* data, size_t length);

/** @brief Frame received by the CAN controller. */
void gateway_on_can_frame(Gateway* gateway, const CanFrame* frame);

/** @brief The user button was pressed (debounced). */
void gateway_on_button(Gateway* gateway);

/** @brief Driver dropped frames because its receive queue was full. */
void gateway_on_rx_overrun(Gateway* gateway);

/**
 * @brief Periodic call (every millisecond) with the current time: diagnostics timers and
 *        monitors, delayed reset, LED heartbeat while the channel is closed.
 */
void gateway_tick(Gateway* gateway, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_GATEWAY_H */
