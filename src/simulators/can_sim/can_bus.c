#include "can_bus.h"
#include "../../common/log.h"

static CanFrame buffer[CAN_BUS_BUFFER_SIZE];
static uint8_t head = 0;
static uint8_t tail = 0;
static uint8_t count = 0;

bool can_bus_init(void) {
  head = 0;
  tail = 0;
  count = 0;
  log_info("CAN bus initialized");
  return true;
}

bool can_bus_send(const CanFrame* frame) {
  if(count >= CAN_BUS_BUFFER_SIZE){
    log_info("Buffer is full");
    return false;
  }
  buffer[tail] = *frame;
  tail = (tail + 1) % CAN_BUS_BUFFER_SIZE;
  count++;
  log_info("CAN Message put in Buffer");
  return true;
}


bool can_bus_receive(CanFrame* frame) {

  if(count == 0){
    log_info("Buffer is empty");
    return false;
  }
  *frame = buffer[head];
  head = (head + 1) % CAN_BUS_BUFFER_SIZE;
  count--;
  log_info("CAN Message taken from Buffer");
  return true;
}

void can_bus_destroy(){
  head  = 0;
  tail  = 0;
  count = 0;
  log_info("CAN bus destroyed");
}
