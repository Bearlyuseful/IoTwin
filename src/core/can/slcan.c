#include "slcan.h"

static const uint32_t g_bitrates[] = {10000u,  20000u,  50000u,  100000u, 125000u,
                                      250000u, 500000u, 800000u, 1000000u};
#define BITRATE_COUNT (sizeof(g_bitrates) / sizeof(g_bitrates[0]))

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

static bool parse_hex(const char* text, size_t digits, uint32_t* value) {
  uint32_t result = 0;
  for (size_t i = 0; i < digits; i++) {
    int v = hex_value(text[i]);
    if (v < 0) {
      return false;
    }
    result = (result << 4) | (uint32_t)v;
  }
  *value = result;
  return true;
}

void slcan_put_hex(char* out, uint32_t value, unsigned digits) {
  static const char hex[] = "0123456789ABCDEF";
  for (unsigned i = 0; i < digits; i++) {
    out[digits - 1 - i] = hex[value & 0xFu];
    value >>= 4;
  }
}

uint32_t slcan_bitrate_from_index(unsigned index) {
  return index < BITRATE_COUNT ? g_bitrates[index] : 0u;
}

int slcan_index_from_bitrate(uint32_t bitrate) {
  for (unsigned i = 0; i < BITRATE_COUNT; i++) {
    if (g_bitrates[i] == bitrate) {
      return (int)i;
    }
  }
  return -1;
}

static bool parse_frame(const char* line, size_t length, CanFrame* frame) {
  const char kind = line[0];
  const bool extended = (kind == 'T' || kind == 'R');
  const bool remote = (kind == 'r' || kind == 'R');
  const size_t id_digits = extended ? 8u : 3u;

  if (length < 1u + id_digits + 1u) {
    return false;
  }
  uint32_t id = 0;
  if (!parse_hex(line + 1, id_digits, &id)) {
    return false;
  }
  const char dlc_char = line[1 + id_digits];
  if (dlc_char < '0' || dlc_char > '8') {
    return false;
  }

  CanFrame parsed;
  can_frame_clear(&parsed);
  parsed.id = id;
  parsed.extended = extended;
  parsed.rtr = remote;
  parsed.dlc = (uint8_t)(dlc_char - '0');
  if (!can_frame_is_valid(&parsed)) {
    return false;
  }

  size_t pos = 2u + id_digits;
  if (!remote) {
    for (uint8_t i = 0; i < parsed.dlc; i++) {
      uint32_t byte = 0;
      if (pos + 2u > length || !parse_hex(line + pos, 2, &byte)) {
        return false;
      }
      parsed.data[i] = (uint8_t)byte;
      pos += 2u;
    }
  }

  /* Optional 16-bit timestamp (Z1 mode) is accepted and ignored. */
  const size_t rest = length - pos;
  if (rest == 4) {
    uint32_t ignored = 0;
    if (!parse_hex(line + pos, 4, &ignored)) {
      return false;
    }
  } else if (rest != 0) {
    return false;
  }

  *frame = parsed;
  return true;
}

bool slcan_parse(const char* line, size_t length, SlcanCommand* command) {
  if (line == NULL || command == NULL || length == 0) {
    return false;
  }

  switch (line[0]) {
    case 't':
    case 'T':
    case 'r':
    case 'R':
      command->type = SLCAN_CMD_FRAME;
      return parse_frame(line, length, &command->frame);
    case 'O':
      command->type = SLCAN_CMD_OPEN;
      return length == 1;
    case 'l':
      command->type = SLCAN_CMD_OPEN_LOOPBACK;
      return length == 1;
    case 'L':
      command->type = SLCAN_CMD_OPEN_LISTEN;
      return length == 1;
    case 'C':
      command->type = SLCAN_CMD_CLOSE;
      return length == 1;
    case 'V':
      command->type = SLCAN_CMD_VERSION;
      return length == 1;
    case 'N':
      command->type = SLCAN_CMD_SERIAL;
      return length == 1;
    case 'F':
      command->type = SLCAN_CMD_STATUS;
      return length == 1;
    case 'Q':
      command->type = SLCAN_CMD_STATISTICS;
      return length == 1;
    case 'S':
      command->type = SLCAN_CMD_BITRATE;
      if (length != 2 || line[1] < '0' || line[1] > '8') {
        return false;
      }
      command->bitrate = slcan_bitrate_from_index((unsigned)(line[1] - '0'));
      return true;
    case 'Z':
      command->type = SLCAN_CMD_TIMESTAMP;
      if (length != 2 || (line[1] != '0' && line[1] != '1')) {
        return false;
      }
      command->enable = line[1] == '1';
      return true;
    default:
      return false;
  }
}

int slcan_format_frame(const CanFrame* frame, char* buffer, size_t buffer_size) {
  if (buffer == NULL || !can_frame_is_valid(frame)) {
    return -1;
  }
  const unsigned id_digits = frame->extended ? 8u : 3u;
  const size_t needed = 1u + id_digits + 1u + (frame->rtr ? 0u : 2u * frame->dlc) + 1u;
  if (needed + 1u > buffer_size) {
    return -1;
  }

  char* p = buffer;
  *p++ = frame->extended ? (frame->rtr ? 'R' : 'T') : (frame->rtr ? 'r' : 't');
  slcan_put_hex(p, frame->id, id_digits);
  p += id_digits;
  *p++ = (char)('0' + frame->dlc);
  if (!frame->rtr) {
    for (uint8_t i = 0; i < frame->dlc; i++) {
      slcan_put_hex(p, frame->data[i], 2);
      p += 2;
    }
  }
  *p++ = '\r';
  *p = '\0';
  return (int)(p - buffer);
}

void slcan_reader_init(SlcanReader* reader) {
  reader->length = 0;
  reader->discarding = false;
  reader->last_was_cr = false;
  reader->overflows = 0;
}

void slcan_reader_feed(SlcanReader* reader, const char* data, size_t length,
                       SlcanLineCallback callback, void* user_data) {
  for (size_t i = 0; i < length; i++) {
    const char c = data[i];

    if (c == '\a') {
      reader->last_was_cr = false;
      if (callback) {
        callback("\a", 1, user_data);
      }
      continue;
    }

    if (c == '\r' || c == '\n') {
      const bool swallow = (c == '\n' && reader->last_was_cr);
      reader->last_was_cr = (c == '\r');
      if (swallow) {
        continue;
      }
      if (reader->discarding) {
        reader->discarding = false;
        reader->overflows++;
      } else if (callback) {
        reader->buffer[reader->length] = '\0';
        callback(reader->buffer, reader->length, user_data);
      }
      reader->length = 0;
      continue;
    }

    reader->last_was_cr = false;
    if (reader->discarding) {
      continue;
    }
    if (reader->length < SLCAN_MAX_LINE - 1) {
      reader->buffer[reader->length++] = c;
    } else {
      reader->discarding = true;
      reader->length = 0;
    }
  }
}
