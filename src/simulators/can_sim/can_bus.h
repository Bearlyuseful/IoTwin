#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "can_frame.h"

#define CAN_BUS_BUFFER_SIZE 16

bool can_bus_init(void);
bool can_bus_send(const CanFrame* frame);
bool can_bus_receive(CanFrame* frame);
void can_bus_destroy(void);