#include "gateway.h"

#include <string.h>

#define VERSION_REPLY "V0100\r"     /* hardware 01, software 00 */
#define SERIAL_REPLY "NG0B1\r"

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

/* Local node: the LED mirrors the lamp command on the bus. */
static void local_node_receive(Gateway* gw, const CanFrame* frame) {
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
  if (gateway->open) {
    return;
  }
  /* Heartbeat while waiting for the host: the firmware is alive, the channel is closed. */
  if (now_ms - gateway->last_blink_ms >= GATEWAY_BLINK_PERIOD_MS) {
    gateway->last_blink_ms = now_ms;
    set_led(gateway, !gateway->led_on);
  }
}
