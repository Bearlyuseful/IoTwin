#include "gateway.h"

#include <string.h>

#define VERSION_REPLY "V0100\r"     /* hardware 01, software 00 */
#define SERIAL_REPLY "NG0B1\r"
#define SPARE_PART_NUMBER "NUCLEO-G0B1RE-GW"

#ifndef IOTWIN_FIRMWARE_VERSION
#define IOTWIN_FIRMWARE_VERSION "dev"
#endif

static void reply(Gateway* gw, const char* text) {
  gw->io.serial_write(gw->io.context, text, strlen(text));
}

static void set_led(Gateway* gw, bool on) {
  gw->led_on = on;
  gw->io.led_set(gw->io.context, on);
}

static void report_frame(Gateway* gw, const CanFrame* frame) {
  char line[SLCAN_MAX_LINE];
  int length = slcan_format_frame(frame, line, sizeof(line));
  if (length > 0) {
    gw->io.serial_write(gw->io.context, line, (size_t)length);
  }
}

/* Local node: diagnostics, and the LED mirrors the lamp command on the bus. */
static void local_node_receive(Gateway* gw, const CanFrame* frame) {
  if (uds_server_on_frame(&gw->uds, frame, (uint64_t)gw->now_ms * 1000u)) {
    return;
  }
  if (frame->extended || frame->rtr || frame->id != GATEWAY_ID_LAMP_CMD || frame->dlc < 1) {
    return;
  }
  gw->lamp_on = (frame->data[0] & 0x01u) != 0;
  if (gw->open) {
    set_led(gw, gw->lamp_on);
  }
}

static void echo_push(Gateway* gw, const CanFrame* frame) {
  if (gw->echo_count == GATEWAY_ECHO_SLOTS) {
    /* Should not happen (TX FIFO is smaller); forget the oldest. */
    gw->echo_head = (gw->echo_head + 1u) % GATEWAY_ECHO_SLOTS;
    gw->echo_count--;
  }
  gw->echo[(gw->echo_head + gw->echo_count) % GATEWAY_ECHO_SLOTS] = *frame;
  gw->echo_count++;
}

/* Removes the first pending echo equal to frame. */
static bool echo_take(Gateway* gw, const CanFrame* frame) {
  for (size_t i = 0; i < gw->echo_count; i++) {
    const size_t index = (gw->echo_head + i) % GATEWAY_ECHO_SLOTS;
    if (!can_frame_equals(&gw->echo[index], frame)) {
      continue;
    }
    /* Close the gap by moving the older entries up by one. */
    for (size_t j = i; j > 0; j--) {
      gw->echo[(gw->echo_head + j) % GATEWAY_ECHO_SLOTS] =
          gw->echo[(gw->echo_head + j - 1) % GATEWAY_ECHO_SLOTS];
    }
    gw->echo_head = (gw->echo_head + 1u) % GATEWAY_ECHO_SLOTS;
    gw->echo_count--;
    return true;
  }
  return false;
}

/* Hands a frame to the CAN controller. */
static bool transmit(Gateway* gw, const CanFrame* frame) {
  if (!gw->open || gw->mode == GATEWAY_CAN_LISTEN) {
    return false;
  }
  if (!gw->io.can_send(gw->io.context, frame)) {
    gw->sticky_flags |= GATEWAY_FLAG_TX_QUEUE_FULL;
    return false;
  }
  gw->tx_frames++;
  if (gw->mode == GATEWAY_CAN_LOOPBACK) {
    echo_push(gw, frame);
  }
  return true;
}

/* ------------------------------------------------------------------------- */
/* Diagnostics                                                                */
/* ------------------------------------------------------------------------- */

/* UDS responses: the tester may sit on the serial side or on the CAN bus. */
static bool uds_send(void* context, const CanFrame* frame) {
  Gateway* gw = (Gateway*)context;
  if (!gw->open) {
    return false;
  }
  /* CAN first: if its FIFO is full, ISO-TP retries later and the host does not see the frame twice.
   * In listen mode the gateway must not drive the bus, only the serial side gets the response. */
  if (gw->mode != GATEWAY_CAN_LISTEN && !transmit(gw, frame)) {
    return false;
  }
  report_frame(gw, frame);
  return true;
}

static void put_u32(uint8_t* out, uint32_t value) {
  out[0] = (uint8_t)(value >> 24);
  out[1] = (uint8_t)(value >> 16);
  out[2] = (uint8_t)(value >> 8);
  out[3] = (uint8_t)value;
}

static int read_did(void* context, uint16_t did, uint8_t* out, size_t capacity) {
  Gateway* gw = (Gateway*)context;
  if (capacity < 16) {
    return -(int)UDS_NRC_RESPONSE_TOO_LONG;
  }
  switch (did) {
    case UDS_DID_SPARE_PART_NUMBER:
      memcpy(out, SPARE_PART_NUMBER, sizeof(SPARE_PART_NUMBER) - 1u);
      return (int)sizeof(SPARE_PART_NUMBER) - 1;
    case UDS_DID_SOFTWARE_VERSION: {
      const size_t length = strlen(IOTWIN_FIRMWARE_VERSION);
      if (length > capacity) {
        return -(int)UDS_NRC_RESPONSE_TOO_LONG;
      }
      memcpy(out, IOTWIN_FIRMWARE_VERSION, length);
      return (int)length;
    }
    case UDS_DID_SERIAL_NUMBER:
      if (gw->io.unique_id == NULL) {
        return -(int)UDS_NRC_REQUEST_OUT_OF_RANGE;
      }
      gw->io.unique_id(gw->io.context, out);
      return 12;
    case GATEWAY_DID_LAMP:
      out[0] = gw->lamp_on ? 1u : 0u;
      return 1;
    case GATEWAY_DID_SWITCH:
      out[0] = gw->switch_on ? 1u : 0u;
      return 1;
    case GATEWAY_DID_CAN_ERRORS: {
      GatewayCanStatus status;
      memset(&status, 0, sizeof(status));
      if (gw->open) {
        gw->io.can_status(gw->io.context, &status);
      }
      out[0] = status.tx_error_count;
      out[1] = status.rx_error_count;
      out[2] = status.bus_off ? 3u : status.error_passive ? 2u : status.error_warning ? 1u : 0u;
      return 3;
    }
    case GATEWAY_DID_COUNTERS:
      put_u32(&out[0], gw->tx_frames);
      put_u32(&out[4], gw->rx_frames);
      put_u32(&out[8], gw->echoes);
      return 12;
    case GATEWAY_DID_UPTIME:
      put_u32(out, gw->now_ms);
      return 4;
    case GATEWAY_DID_CHANNEL:
      out[0] = gw->open ? 1u : 0u;
      out[1] = (uint8_t)gw->mode;
      put_u32(&out[2], gw->bitrate);
      return 6;
    default:
      return -(int)UDS_NRC_REQUEST_OUT_OF_RANGE;
  }
}

static void uds_reset(void* context, uint8_t type) {
  Gateway* gw = (Gateway*)context;
  if (type == UDS_RESET_HARD && gw->io.system_reset != NULL) {
    gw->reset_scheduled = true;
    gw->reset_due_ms = gw->now_ms + GATEWAY_RESET_DELAY_MS;
    return;
  }
  /* Soft reset: restart the application state, keep the channel to the host open. */
  gw->tx_frames = 0;
  gw->rx_frames = 0;
  gw->echoes = 0;
  gw->sticky_flags = 0;
  for (size_t i = 0; i < GATEWAY_DTC_COUNT; i++) {
    uds_dtc_start_operation_cycle(&gw->dtcs[i]);
  }
  gw->soft_resets++;
}

static void diag_init(Gateway* gw) {
  uds_dtc_init(&gw->dtcs[0], GATEWAY_DTC_BUS_OFF);
  UdsServerConfig config;
  uds_server_default_config(&config, GATEWAY_DIAG_REQUEST_ID, GATEWAY_DIAG_RESPONSE_ID);
  config.send = uds_send;
  config.send_context = gw;
  config.handlers.read_did = read_did;
  config.handlers.reset = uds_reset;
  config.handlers.context = gw;
  config.dtcs = gw->dtcs;
  config.dtc_count = GATEWAY_DTC_COUNT;
  uds_server_init(&gw->uds, &config);
}

/* Monitors: bus off is only tested while the controller takes part in the bus. */
static void diag_monitors(Gateway* gw) {
  if (!gw->open || gw->mode == GATEWAY_CAN_LISTEN) {
    return;
  }
  GatewayCanStatus status;
  memset(&status, 0, sizeof(status));
  gw->io.can_status(gw->io.context, &status);
  uds_dtc_report(&gw->dtcs[0], status.bus_off);
}

static uint8_t status_flags(Gateway* gw) {
  GatewayCanStatus status;
  memset(&status, 0, sizeof(status));
  if (gw->open) {
    gw->io.can_status(gw->io.context, &status);
  }
  uint8_t flags = gw->sticky_flags;
  if (status.error_warning) {
    flags |= GATEWAY_FLAG_ERROR_WARNING;
  }
  if (status.error_passive) {
    flags |= GATEWAY_FLAG_ERROR_PASSIVE;
  }
  if (status.bus_error || status.bus_off) {
    flags |= GATEWAY_FLAG_BUS_ERROR;
  }
  return flags;
}

static void reply_statistics(Gateway* gw) {
  GatewayCanStatus status;
  memset(&status, 0, sizeof(status));
  if (gw->open) {
    gw->io.can_status(gw->io.context, &status);
  }
  /* Q<tx 8><rx 8><echoes 8><tec 2><rec 2><flags 2>\r */
  char line[1 + 8 + 8 + 8 + 2 + 2 + 2 + 2];
  char* p = line;
  *p++ = 'Q';
  slcan_put_hex(p, gw->tx_frames, 8);
  p += 8;
  slcan_put_hex(p, gw->rx_frames, 8);
  p += 8;
  slcan_put_hex(p, gw->echoes, 8);
  p += 8;
  slcan_put_hex(p, status.tx_error_count, 2);
  p += 2;
  slcan_put_hex(p, status.rx_error_count, 2);
  p += 2;
  slcan_put_hex(p, status_flags(gw), 2);
  p += 2;
  *p++ = '\r';
  *p = '\0';
  reply(gw, line);
}

static void open_channel(Gateway* gw, GatewayCanMode mode) {
  if (gw->open || !gw->io.can_start(gw->io.context, gw->bitrate, mode)) {
    reply(gw, SLCAN_ERROR);
    return;
  }
  gw->open = true;
  gw->mode = mode;
  gw->echo_head = 0;
  gw->echo_count = 0;
  gw->sticky_flags = 0;
  set_led(gw, gw->lamp_on);
  reply(gw, SLCAN_OK);
}

static void close_channel(Gateway* gw) {
  if (gw->open) {
    gw->io.can_stop(gw->io.context);
    gw->open = false;
    gw->echo_count = 0;
  }
  reply(gw, SLCAN_OK);
}

static void handle_line(const char* line, size_t length, void* user_data) {
  Gateway* gw = (Gateway*)user_data;
  if (length == 0) {
    return; /* empty line, e.g. from a terminal sending CR LF */
  }
  if (length == 1 && line[0] == '\a') {
    return;
  }

  SlcanCommand command;
  if (!slcan_parse(line, length, &command)) {
    reply(gw, SLCAN_ERROR);
    return;
  }

  switch (command.type) {
    case SLCAN_CMD_FRAME:
      if (!gw->open || gw->mode == GATEWAY_CAN_LISTEN) {
        reply(gw, SLCAN_ERROR);
        break;
      }
      if (!transmit(gw, &command.frame)) {
        reply(gw, SLCAN_ERROR);
        break;
      }
      local_node_receive(gw, &command.frame);
      reply(gw, command.frame.extended ? "Z\r" : "z\r");
      break;
    case SLCAN_CMD_OPEN:
      open_channel(gw, GATEWAY_CAN_NORMAL);
      break;
    case SLCAN_CMD_OPEN_LOOPBACK:
      open_channel(gw, GATEWAY_CAN_LOOPBACK);
      break;
    case SLCAN_CMD_OPEN_LISTEN:
      open_channel(gw, GATEWAY_CAN_LISTEN);
      break;
    case SLCAN_CMD_CLOSE:
      close_channel(gw);
      break;
    case SLCAN_CMD_BITRATE:
      if (gw->open) {
        reply(gw, SLCAN_ERROR);
      } else {
        gw->bitrate = command.bitrate;
        reply(gw, SLCAN_OK);
      }
      break;
    case SLCAN_CMD_VERSION:
      reply(gw, VERSION_REPLY);
      break;
    case SLCAN_CMD_SERIAL:
      reply(gw, SERIAL_REPLY);
      break;
    case SLCAN_CMD_STATUS: {
      char text[4] = {'F', '0', '0', '\r'};
      slcan_put_hex(text + 1, status_flags(gw), 2);
      gw->sticky_flags = 0;
      gw->io.serial_write(gw->io.context, text, sizeof(text));
      break;
    }
    case SLCAN_CMD_TIMESTAMP:
      reply(gw, SLCAN_OK); /* accepted, timestamps are not added */
      break;
    case SLCAN_CMD_STATISTICS:
      reply_statistics(gw);
      break;
  }
}

void gateway_init(Gateway* gateway, const GatewayIo* io) {
  memset(gateway, 0, sizeof(*gateway));
  gateway->io = *io;
  gateway->bitrate = GATEWAY_DEFAULT_BITRATE;
  slcan_reader_init(&gateway->reader);
  diag_init(gateway);
  set_led(gateway, false);
}

void gateway_on_serial_data(Gateway* gateway, const char* data, size_t length) {
  slcan_reader_feed(&gateway->reader, data, length, handle_line, gateway);
}

void gateway_on_can_frame(Gateway* gateway, const CanFrame* frame) {
  gateway->rx_frames++;
  if (gateway->mode == GATEWAY_CAN_LOOPBACK && echo_take(gateway, frame)) {
    gateway->echoes++;
    return; /* own frame, already known to the serial side */
  }
  if (gateway->open) {
    report_frame(gateway, frame);
  }
  local_node_receive(gateway, frame);
}

void gateway_on_button(Gateway* gateway) {
  if (!gateway->open) {
    return;
  }
  gateway->switch_on = !gateway->switch_on;
  CanFrame frame;
  const uint8_t payload = gateway->switch_on ? 0x01u : 0x00u;
  can_frame_set(&frame, GATEWAY_ID_SWITCH_STATE, false, &payload, 1);
  /* The local node is on both buses: tell the serial side and the CAN bus. */
  report_frame(gateway, &frame);
  transmit(gateway, &frame);
}

void gateway_on_rx_overrun(Gateway* gateway) {
  gateway->sticky_flags |= GATEWAY_FLAG_RX_QUEUE_FULL | GATEWAY_FLAG_DATA_OVERRUN;
}

void gateway_tick(Gateway* gateway, uint32_t now_ms) {
  gateway->now_ms = now_ms;
  uds_server_poll(&gateway->uds, (uint64_t)now_ms * 1000u);
  diag_monitors(gateway);
  if (gateway->reset_scheduled && (int32_t)(now_ms - gateway->reset_due_ms) >= 0) {
    gateway->reset_scheduled = false;
    gateway->io.system_reset(gateway->io.context);
  }

  if (gateway->open) {
    return;
  }
  /* Heartbeat while waiting for the host: the firmware is alive, the channel is closed. */
  if (now_ms - gateway->last_blink_ms >= GATEWAY_BLINK_PERIOD_MS) {
    gateway->last_blink_ms = now_ms;
    set_led(gateway, !gateway->led_on);
  }
}
