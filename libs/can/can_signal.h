#ifndef IOTWIN_CAN_SIGNAL_H
#define IOTWIN_CAN_SIGNAL_H

/**
 * @file can_signal.h
 * @brief Encoding and decoding of signals inside a CAN payload (DBC semantics).
 *
 * Bit numbering follows the DBC convention: bit n is bit (n % 8) of byte (n / 8).
 *
 *  - Intel (little endian, "@1"): start_bit is the least significant bit, the
 *    signal grows towards higher bit numbers.
 *  - Motorola (big endian, "@0"): start_bit is the most significant bit; less
 *    significant bits follow downwards in the byte and continue at bit 7 of the
 *    next byte ("sawtooth").
 *
 * physical = raw * factor + offset. Portable C without heap use (also usable
 * in firmware).
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  CAN_BYTE_ORDER_MOTOROLA = 0, /**< big endian, DBC "@0" */
  CAN_BYTE_ORDER_INTEL = 1     /**< little endian, DBC "@1" */
} CanByteOrder;

/** @brief Signal layout and scaling. */
typedef struct {
  uint16_t start_bit;
  uint8_t length; /**< 1..64 bits */
  CanByteOrder byte_order;
  bool is_signed; /**< two's complement */
  double factor;
  double offset;
} CanSignal;

/** @brief True if all bits of the signal lie inside an 8 byte payload. */
bool can_signal_layout_valid(const CanSignal* signal);

/** @brief Raw (unscaled, not sign extended) bits of the signal. */
uint64_t can_signal_extract_raw(const CanSignal* signal, const uint8_t data[8]);

/** @brief Writes @p raw into the signal's bits (other bits unchanged). */
void can_signal_insert_raw(const CanSignal* signal, uint8_t data[8], uint64_t raw);

/** @brief Raw value interpreted with the signal's signedness. */
int64_t can_signal_raw_to_int(const CanSignal* signal, uint64_t raw);

/** @brief Physical value of the signal. */
double can_signal_decode(const CanSignal* signal, const uint8_t data[8]);

/**
 * @brief Writes a physical value (rounded to the nearest raw step).
 * @return false (payload unchanged) if the value does not fit into the signal.
 */
bool can_signal_encode(const CanSignal* signal, uint8_t data[8], double physical);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_CAN_SIGNAL_H */
