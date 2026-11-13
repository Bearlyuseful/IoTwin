#include "can_bus.h"

#include "log.h"

bool can_bus_init(CanBus* bus) {
  bus->head = 0;
  bus->tail = 0;
  bus->count = 0;
  bus->dropped = 0;
  if (pthread_mutex_init(&bus->lock, NULL) != 0) {
    log_error("CAN bus FIFO: mutex initialisation failed");
    return false;
  }
  return true;
}

bool can_bus_send(CanBus* bus, const CanFrame* frame) {
  if (!can_frame_is_valid(frame)) {
    log_debug("CAN bus FIFO: rejected invalid frame");
    return false;
  }
  pthread_mutex_lock(&bus->lock);
  if (bus->count >= CAN_BUS_BUFFER_SIZE) {
    bus->dropped++;
    pthread_mutex_unlock(&bus->lock);
    log_debug("CAN bus FIFO: buffer full, frame dropped");
    return false;
  }
  bus->buffer[bus->tail] = *frame;
  bus->tail = (bus->tail + 1) % CAN_BUS_BUFFER_SIZE;
  bus->count++;
  pthread_mutex_unlock(&bus->lock);
  return true;
}

bool can_bus_receive(CanBus* bus, CanFrame* frame) {
  pthread_mutex_lock(&bus->lock);
  if (bus->count == 0) {
    pthread_mutex_unlock(&bus->lock);
    return false;
  }
  *frame = bus->buffer[bus->head];
  bus->head = (bus->head + 1) % CAN_BUS_BUFFER_SIZE;
  bus->count--;
  pthread_mutex_unlock(&bus->lock);
  return true;
}

size_t can_bus_count(CanBus* bus) {
  pthread_mutex_lock(&bus->lock);
  size_t count = bus->count;
  pthread_mutex_unlock(&bus->lock);
  return count;
}

uint64_t can_bus_dropped(CanBus* bus) {
  pthread_mutex_lock(&bus->lock);
  uint64_t dropped = bus->dropped;
  pthread_mutex_unlock(&bus->lock);
  return dropped;
}

void can_bus_destroy(CanBus* bus) {
  pthread_mutex_destroy(&bus->lock);
  bus->head = 0;
  bus->tail = 0;
  bus->count = 0;
}
