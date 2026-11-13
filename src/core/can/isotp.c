#include "isotp.h"

#include <string.h>

enum { TX_IDLE = 0, TX_WAIT_FC, TX_SEND_CF };
enum { RX_IDLE = 0, RX_RECEIVING };

enum {
  PCI_SINGLE = 0x0,
  PCI_FIRST = 0x1,
  PCI_CONSECUTIVE = 0x2,
  PCI_FLOW_CONTROL = 0x3
};
enum { FLOW_CTS = 0, FLOW_WAIT = 1, FLOW_OVERFLOW = 2 };

void isotp_default_config(IsoTpConfig* config, uint32_t tx_id, uint32_t rx_id) {
  memset(config, 0, sizeof(*config));
  config->tx_id = tx_id;
  config->rx_id = rx_id;
  config->block_size = 0;
  config->st_min = 0;
  config->timeout_us = 1000000u;
  config->max_wait_frames = 10;
  config->padding = true;
  config->padding_byte = 0xCC;
}

void isotp_init(IsoTpLink* link, const IsoTpConfig* config, uint8_t* rx_buffer, size_t rx_capacity) {
  memset(link, 0, sizeof(*link));
  link->config = *config;
  link->rx_buffer = rx_buffer;
  link->rx_capacity = rx_capacity;
  link->tx_result = ISOTP_OK;
  link->rx_error = ISOTP_OK;
}

uint32_t isotp_st_min_to_us(uint8_t st_min) {
  if (st_min <= 0x7F) {
    return (uint32_t)st_min * 1000u;
  }
  if (st_min >= 0xF1 && st_min <= 0xF9) {
    return (uint32_t)(st_min - 0xF0) * 100u;
  }
  return 127000u; /* reserved: use the longest valid value */
}

static bool send_frame(IsoTpLink* link, const uint8_t* payload, uint8_t length) {
  CanFrame frame;
  can_frame_clear(&frame);
  frame.id = link->config.tx_id;
  frame.extended = link->config.extended_id;
  memcpy(frame.data, payload, length);
  if (link->config.padding) {
    memset(frame.data + length, link->config.padding_byte, CAN_MAX_DLEN - length);
    frame.dlc = CAN_MAX_DLEN;
  } else {
    frame.dlc = length;
  }
  return link->config.send(link->config.context, &frame);
}

static void send_flow_control(IsoTpLink* link, uint8_t status) {
  const uint8_t payload[3] = {(uint8_t)((PCI_FLOW_CONTROL << 4) | status), link->config.block_size,
                              link->config.st_min};
  send_frame(link, payload, sizeof(payload));
}

static void finish_tx(IsoTpLink* link, IsoTpResult result) {
  link->tx_state = TX_IDLE;
  link->tx_result = result;
}

static void fail_rx(IsoTpLink* link, IsoTpResult error) {
  link->rx_state = RX_IDLE;
  link->rx_error = error;
}

IsoTpResult isotp_send(IsoTpLink* link, const uint8_t* data, size_t length, uint64_t now_us) {
  if (link->tx_state != TX_IDLE) {
    return ISOTP_BUSY;
  }
  if (length == 0 || length > ISOTP_MAX_LENGTH) {
    return ISOTP_ERROR_LENGTH;
  }

  if (length <= 7) {
    uint8_t payload[8];
    payload[0] = (uint8_t)((PCI_SINGLE << 4) | length);
    memcpy(payload + 1, data, length);
    const IsoTpResult result = send_frame(link, payload, (uint8_t)(length + 1)) ? ISOTP_OK : ISOTP_ERROR_SEND;
    link->tx_result = result;
    return result;
  }

  memcpy(link->tx_buffer, data, length);
  link->tx_length = length;

  uint8_t payload[8];
  payload[0] = (uint8_t)((PCI_FIRST << 4) | ((length >> 8) & 0x0Fu));
  payload[1] = (uint8_t)(length & 0xFFu);
  memcpy(payload + 2, data, 6);
  if (!send_frame(link, payload, 8)) {
    link->tx_result = ISOTP_ERROR_SEND;
    return ISOTP_ERROR_SEND;
  }
  link->tx_offset = 6;
  link->tx_sequence = 1;
  link->tx_wait_count = 0;
  link->tx_state = TX_WAIT_FC;
  link->tx_deadline_us = now_us + link->config.timeout_us;
  link->tx_result = ISOTP_OK;
  return ISOTP_OK;
}

static void on_flow_control(IsoTpLink* link, const CanFrame* frame, uint64_t now_us) {
  if (link->tx_state != TX_WAIT_FC || frame->dlc < 3) {
    return; /* not waiting for one: ignore */
  }
  switch (frame->data[0] & 0x0Fu) {
    case FLOW_CTS:
      link->tx_block_size = frame->data[1];
      link->tx_block_remaining = frame->data[1];
      link->tx_st_min_us = isotp_st_min_to_us(frame->data[2]);
      link->tx_state = TX_SEND_CF;
      link->tx_next_cf_us = now_us; /* the first consecutive frame may follow immediately */
      link->tx_wait_count = 0;
      isotp_poll(link, now_us);
      break;
    case FLOW_WAIT:
      if (++link->tx_wait_count > link->config.max_wait_frames) {
        finish_tx(link, ISOTP_ERROR_WAIT_LIMIT);
      } else {
        link->tx_deadline_us = now_us + link->config.timeout_us;
      }
      break;
    case FLOW_OVERFLOW:
      finish_tx(link, ISOTP_ERROR_OVERFLOW);
      break;
    default:
      finish_tx(link, ISOTP_ERROR_UNEXPECTED);
      break;
  }
}

static void on_single_frame(IsoTpLink* link, const CanFrame* frame) {
  const size_t length = frame->data[0] & 0x0Fu;
  if (length == 0 || length > 7 || length + 1 > frame->dlc) {
    return; /* invalid single frame */
  }
  if (link->rx_state == RX_RECEIVING) {
    link->rx_error = ISOTP_ERROR_UNEXPECTED; /* new message interrupts the old one */
    link->rx_state = RX_IDLE;
  }
  if (length > link->rx_capacity) {
    fail_rx(link, ISOTP_ERROR_OVERFLOW);
    return;
  }
  memcpy(link->rx_buffer, frame->data + 1, length);
  link->rx_length = length;
  link->rx_complete = true;
}

static void on_first_frame(IsoTpLink* link, const CanFrame* frame, uint64_t now_us) {
  const size_t length = ((size_t)(frame->data[0] & 0x0Fu) << 8) | frame->data[1];
  if (frame->dlc < 8 || length < 8) {
    return; /* invalid first frame for classic CAN */
  }
  if (link->rx_state == RX_RECEIVING) {
    link->rx_error = ISOTP_ERROR_UNEXPECTED;
  }
  if (length > link->rx_capacity) {
    send_flow_control(link, FLOW_OVERFLOW);
    fail_rx(link, ISOTP_ERROR_OVERFLOW);
    return;
  }
  memcpy(link->rx_buffer, frame->data + 2, 6);
  link->rx_length = length;
  link->rx_offset = 6;
  link->rx_sequence = 1;
  link->rx_block_count = 0;
  link->rx_state = RX_RECEIVING;
  link->rx_complete = false;
  link->rx_deadline_us = now_us + link->config.timeout_us;
  send_flow_control(link, FLOW_CTS);
}

static void on_consecutive_frame(IsoTpLink* link, const CanFrame* frame, uint64_t now_us) {
  if (link->rx_state != RX_RECEIVING) {
    return; /* unexpected: ignore */
  }
  if ((frame->data[0] & 0x0Fu) != link->rx_sequence) {
    fail_rx(link, ISOTP_ERROR_SEQUENCE);
    return;
  }
  size_t remaining = link->rx_length - link->rx_offset;
  size_t chunk = remaining < 7 ? remaining : 7;
  if (frame->dlc < chunk + 1) {
    fail_rx(link, ISOTP_ERROR_UNEXPECTED);
    return;
  }
  memcpy(link->rx_buffer + link->rx_offset, frame->data + 1, chunk);
  link->rx_offset += chunk;
  link->rx_sequence = (uint8_t)((link->rx_sequence + 1u) & 0x0Fu);

  if (link->rx_offset == link->rx_length) {
    link->rx_state = RX_IDLE;
    link->rx_complete = true;
    return;
  }
  link->rx_deadline_us = now_us + link->config.timeout_us;
  if (link->config.block_size != 0 && ++link->rx_block_count == link->config.block_size) {
    link->rx_block_count = 0;
    send_flow_control(link, FLOW_CTS);
  }
}

void isotp_on_frame(IsoTpLink* link, const CanFrame* frame, uint64_t now_us) {
  if (frame->id != link->config.rx_id || frame->extended != link->config.extended_id || frame->rtr ||
      frame->dlc == 0) {
    return;
  }
  switch (frame->data[0] >> 4) {
    case PCI_SINGLE: on_single_frame(link, frame); break;
    case PCI_FIRST: on_first_frame(link, frame, now_us); break;
    case PCI_CONSECUTIVE: on_consecutive_frame(link, frame, now_us); break;
    case PCI_FLOW_CONTROL: on_flow_control(link, frame, now_us); break;
    default: break;
  }
}

void isotp_poll(IsoTpLink* link, uint64_t now_us) {
  if (link->rx_state == RX_RECEIVING && now_us >= link->rx_deadline_us) {
    fail_rx(link, ISOTP_ERROR_TIMEOUT_CR);
  }

  if (link->tx_state == TX_WAIT_FC && now_us >= link->tx_deadline_us) {
    finish_tx(link, ISOTP_ERROR_TIMEOUT_BS);
    return;
  }

  while (link->tx_state == TX_SEND_CF && now_us >= link->tx_next_cf_us) {
    const size_t remaining = link->tx_length - link->tx_offset;
    const size_t chunk = remaining < 7 ? remaining : 7;
    uint8_t payload[8];
    payload[0] = (uint8_t)((PCI_CONSECUTIVE << 4) | link->tx_sequence);
    memcpy(payload + 1, link->tx_buffer + link->tx_offset, chunk);
    if (!send_frame(link, payload, (uint8_t)(chunk + 1))) {
      return; /* driver queue full: retry on the next poll */
    }
    link->tx_offset += chunk;
    link->tx_sequence = (uint8_t)((link->tx_sequence + 1u) & 0x0Fu);

    if (link->tx_offset == link->tx_length) {
      finish_tx(link, ISOTP_OK);
      return;
    }
    if (link->tx_block_size != 0 && --link->tx_block_remaining == 0) {
      link->tx_state = TX_WAIT_FC;
      link->tx_deadline_us = now_us + link->config.timeout_us;
      return;
    }
    link->tx_next_cf_us = now_us + link->tx_st_min_us;
    if (link->tx_st_min_us > 0) {
      return;
    }
  }
}

bool isotp_receive(IsoTpLink* link, size_t* length) {
  if (!link->rx_complete) {
    return false;
  }
  link->rx_complete = false;
  if (length != NULL) {
    *length = link->rx_length;
  }
  return true;
}

bool isotp_tx_busy(const IsoTpLink* link) { return link->tx_state != TX_IDLE; }

IsoTpResult isotp_tx_result(const IsoTpLink* link) { return link->tx_result; }

IsoTpResult isotp_take_rx_error(IsoTpLink* link) {
  const IsoTpResult error = link->rx_error;
  link->rx_error = ISOTP_OK;
  return error;
}

const char* isotp_result_name(IsoTpResult result) {
  switch (result) {
    case ISOTP_OK: return "ok";
    case ISOTP_BUSY: return "busy";
    case ISOTP_ERROR_LENGTH: return "invalid length";
    case ISOTP_ERROR_SEND: return "send failed";
    case ISOTP_ERROR_TIMEOUT_BS: return "timeout waiting for flow control (N_Bs)";
    case ISOTP_ERROR_TIMEOUT_CR: return "timeout waiting for consecutive frame (N_Cr)";
    case ISOTP_ERROR_OVERFLOW: return "receiver buffer overflow";
    case ISOTP_ERROR_SEQUENCE: return "wrong sequence number";
    case ISOTP_ERROR_WAIT_LIMIT: return "too many WAIT flow controls";
    case ISOTP_ERROR_UNEXPECTED: return "unexpected frame";
  }
  return "?";
}
