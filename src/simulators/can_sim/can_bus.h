#pragma once

#include <stdbool.h>
#include <stdint.h>


bool can_init(void);
bool can_send(uint32_t id, uint8_t data);