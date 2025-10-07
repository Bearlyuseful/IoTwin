#include "can_bus.h"
#include "../../common/common.h"

bool can_init(void) {
  log_info("CAN bus initialized");
  return true;
}

bool can_send(uint32_t id, uint8_t data) {
  printf("[CAN] Sending ID: %u Data: %u\n", id, data);
  return true;
}