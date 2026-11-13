#include "can_signal.h"

#include <math.h>
#include <stddef.h>

/* Next (less significant) bit position of a Motorola signal. */
static unsigned motorola_next(unsigned position) {
  return (position % 8u == 0u) ? position + 15u : position - 1u;
}

static bool get_bit(const uint8_t data[8], unsigned position) {
  return (data[position / 8u] >> (position % 8u)) & 1u;
}

static void put_bit(uint8_t data[8], unsigned position, bool value) {
  const uint8_t mask = (uint8_t)(1u << (position % 8u));
  if (value) {
    data[position / 8u] |= mask;
  } else {
    data[position / 8u] &= (uint8_t)~mask;
  }
}

bool can_signal_layout_valid(const CanSignal* signal) {
  if (signal == NULL || signal->length == 0 || signal->length > 64 || signal->start_bit > 63) {
    return false;
  }
  if (signal->byte_order == CAN_BYTE_ORDER_INTEL) {
    return (unsigned)signal->start_bit + signal->length <= 64u;
  }
  unsigned position = signal->start_bit;
  for (unsigned i = 1; i < signal->length; i++) {
    position = motorola_next(position);
    if (position > 63u) {
      return false;
    }
  }
  return true;
}

uint64_t can_signal_extract_raw(const CanSignal* signal, const uint8_t data[8]) {
  uint64_t raw = 0;
  if (signal->byte_order == CAN_BYTE_ORDER_INTEL) {
    for (unsigned i = 0; i < signal->length; i++) {
      if (get_bit(data, signal->start_bit + i)) {
        raw |= (uint64_t)1 << i;
      }
    }
    return raw;
  }
  unsigned position = signal->start_bit;
  for (unsigned i = signal->length; i-- > 0;) {
    if (get_bit(data, position)) {
      raw |= (uint64_t)1 << i;
    }
    if (i > 0) {
      position = motorola_next(position);
    }
  }
  return raw;
}

void can_signal_insert_raw(const CanSignal* signal, uint8_t data[8], uint64_t raw) {
  if (signal->byte_order == CAN_BYTE_ORDER_INTEL) {
    for (unsigned i = 0; i < signal->length; i++) {
      put_bit(data, signal->start_bit + i, (raw >> i) & 1u);
    }
    return;
  }
  unsigned position = signal->start_bit;
  for (unsigned i = signal->length; i-- > 0;) {
    put_bit(data, position, (raw >> i) & 1u);
    if (i > 0) {
      position = motorola_next(position);
    }
  }
}

int64_t can_signal_raw_to_int(const CanSignal* signal, uint64_t raw) {
  if (!signal->is_signed || signal->length >= 64) {
    return (int64_t)raw;
  }
  const uint64_t sign_bit = (uint64_t)1 << (signal->length - 1);
  if (raw & sign_bit) {
    return (int64_t)(raw | ~((sign_bit << 1) - 1u)); /* sign extend */
  }
  return (int64_t)raw;
}

double can_signal_decode(const CanSignal* signal, const uint8_t data[8]) {
  const uint64_t raw = can_signal_extract_raw(signal, data);
  const double value = signal->is_signed ? (double)can_signal_raw_to_int(signal, raw) : (double)raw;
  return value * signal->factor + signal->offset;
}

bool can_signal_encode(const CanSignal* signal, uint8_t data[8], double physical) {
  if (signal->factor == 0.0 || !can_signal_layout_valid(signal)) {
    return false;
  }
  const double scaled = nearbyint((physical - signal->offset) / signal->factor);
  const unsigned bits = signal->length;
  double minimum;
  double maximum;
  if (signal->is_signed) {
    minimum = -ldexp(1.0, (int)bits - 1);
    maximum = ldexp(1.0, (int)bits - 1) - 1.0;
  } else {
    minimum = 0.0;
    maximum = ldexp(1.0, (int)bits) - 1.0;
  }
  if (!(scaled >= minimum && scaled <= maximum)) {
    return false;
  }
  uint64_t raw;
  if (signal->is_signed && scaled < 0) {
    raw = (uint64_t)(int64_t)scaled;
  } else {
    raw = (uint64_t)scaled;
  }
  if (bits < 64) {
    raw &= ((uint64_t)1 << bits) - 1u;
  }
  can_signal_insert_raw(signal, data, raw);
  return true;
}
