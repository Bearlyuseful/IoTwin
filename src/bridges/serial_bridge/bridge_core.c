#include "bridge_core.h"

#include <string.h>

#include "can_frame.h"
#include "slcan.h"

bool bridge_parse_mode(const char* text, BridgeMode* mode) {
  if (strcmp(text, "normal") == 0) {
    *mode = BRIDGE_MODE_NORMAL;
  } else if (strcmp(text, "loopback") == 0) {
    *mode = BRIDGE_MODE_LOOPBACK;
  } else if (strcmp(text, "listen") == 0) {
    *mode = BRIDGE_MODE_LISTEN;
  } else {
    return false;
  }
  return true;
}

const char* bridge_open_command(BridgeMode mode) {
  switch (mode) {
    case BRIDGE_MODE_LOOPBACK: return "l\r";
    case BRIDGE_MODE_LISTEN: return "L\r";
    default: return "O\r";
  }
}

const char* bridge_bitrate_command(uint32_t bitrate) {
  static const char* const commands[] = {"S0\r", "S1\r", "S2\r", "S3\r", "S4\r",
                                         "S5\r", "S6\r", "S7\r", "S8\r"};
  int index = slcan_index_from_bitrate(bitrate);
  return index < 0 ? NULL : commands[index];
}

int bridge_adapter_to_bus(const char* line, size_t length, char* out, size_t out_size) {
  if (length == 0 || (line[0] != 't' && line[0] != 'T' && line[0] != 'r' && line[0] != 'R')) {
    return -1;
  }
  SlcanCommand command;
  if (!slcan_parse(line, length, &command) || command.type != SLCAN_CMD_FRAME) {
    return -1;
  }
  return can_frame_to_text(&command.frame, out, out_size);
}

int bridge_bus_to_adapter(const char* line, size_t length, char* out, size_t out_size) {
  CanFrame frame;
  if (!can_frame_from_text(line, length, &frame)) {
    return -1;
  }
  return slcan_format_frame(&frame, out, out_size);
}
