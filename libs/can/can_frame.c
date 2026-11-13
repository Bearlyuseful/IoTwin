#include "can_frame.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

void can_frame_clear(CanFrame* frame) { memset(frame, 0, sizeof(*frame)); }

bool can_frame_is_valid(const CanFrame* frame) {
  if (frame == NULL || frame->dlc > CAN_MAX_DLEN) {
    return false;
  }
  const uint32_t mask = frame->extended ? CAN_EFF_MASK : CAN_SFF_MASK;
  return (frame->id & ~mask) == 0;
}

bool can_frame_set(CanFrame* frame, uint32_t id, bool extended, const uint8_t* data, uint8_t dlc) {
  can_frame_clear(frame);
  frame->id = id;
  frame->extended = extended;
  frame->dlc = dlc;
  if (!can_frame_is_valid(frame) || (dlc > 0 && data == NULL)) {
    can_frame_clear(frame);
    return false;
  }
  if (dlc > 0) {
    memcpy(frame->data, data, dlc);
  }
  return true;
}

bool can_frame_equals(const CanFrame* a, const CanFrame* b) {
  if (a->id != b->id || a->extended != b->extended || a->rtr != b->rtr || a->dlc != b->dlc) {
    return false;
  }
  if (a->rtr) {
    return true;
  }
  return memcmp(a->data, b->data, a->dlc) == 0;
}

int can_frame_to_text(const CanFrame* frame, char* buffer, size_t buffer_size) {
  if (buffer == NULL || buffer_size == 0 || !can_frame_is_valid(frame)) {
    return -1;
  }
  char text[CAN_FRAME_TEXT_MAX];
  int length = frame->extended ? snprintf(text, sizeof(text), "%08" PRIX32 "#", frame->id)
                               : snprintf(text, sizeof(text), "%03" PRIX32 "#", frame->id);
  if (frame->rtr) {
    if (frame->dlc > 0) {
      length += snprintf(text + length, sizeof(text) - (size_t)length, "R%u", (unsigned)frame->dlc);
    } else {
      length += snprintf(text + length, sizeof(text) - (size_t)length, "R");
    }
  } else {
    for (uint8_t i = 0; i < frame->dlc; i++) {
      length += snprintf(text + length, sizeof(text) - (size_t)length, "%02X", frame->data[i]);
    }
  }
  if ((size_t)length >= buffer_size) {
    return -1;
  }
  memcpy(buffer, text, (size_t)length + 1);
  return length;
}

static int hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

bool can_frame_from_text(const char* text, size_t length, CanFrame* frame) {
  if (text == NULL || frame == NULL) {
    return false;
  }

  size_t separator = 0;
  while (separator < length && text[separator] != '#') {
    separator++;
  }
  if (separator >= length || (separator != 3 && separator != 8)) {
    return false; /* missing '#' or unsupported identifier length */
  }

  CanFrame parsed;
  can_frame_clear(&parsed);
  parsed.extended = (separator == 8);
  for (size_t i = 0; i < separator; i++) {
    int value = hex_value(text[i]);
    if (value < 0) {
      return false;
    }
    parsed.id = (parsed.id << 4) | (uint32_t)value;
  }
  if (!can_frame_is_valid(&parsed)) {
    return false; /* identifier out of range, e.g. 800# */
  }

  size_t pos = separator + 1;
  if (pos < length && text[pos] == '#') {
    return false; /* "##" = CAN FD, not supported */
  }

  if (pos < length && (text[pos] == 'R' || text[pos] == 'r')) {
    parsed.rtr = true;
    pos++;
    if (pos < length) {
      if (length - pos != 1 || text[pos] < '0' || text[pos] > '8') {
        return false;
      }
      parsed.dlc = (uint8_t)(text[pos] - '0');
    }
    *frame = parsed;
    return true;
  }

  uint8_t count = 0;
  while (pos < length) {
    if (text[pos] == '.') {
      /* separator only allowed between two bytes */
      if (count == 0 || pos + 1 >= length || text[pos + 1] == '.') {
        return false;
      }
      pos++;
      continue;
    }
    if (pos + 1 >= length || count >= CAN_MAX_DLEN) {
      return false; /* odd number of digits or more than 8 bytes */
    }
    int high = hex_value(text[pos]);
    int low = hex_value(text[pos + 1]);
    if (high < 0 || low < 0) {
      return false;
    }
    parsed.data[count++] = (uint8_t)((high << 4) | low);
    pos += 2;
  }
  parsed.dlc = count;
  *frame = parsed;
  return true;
}
