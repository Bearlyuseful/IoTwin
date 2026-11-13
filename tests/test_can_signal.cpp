#include <catch2/catch_all.hpp>

#include <cstring>

#include "can_signal.h"

namespace {

CanSignal make(uint16_t start, uint8_t length, CanByteOrder order, bool is_signed = false,
               double factor = 1.0, double offset = 0.0) {
  return CanSignal{start, length, order, is_signed, factor, offset};
}

}  // namespace

TEST_CASE("Intel signals read little endian from the start bit", "[can_signal]") {
  const uint8_t data[8] = {0x34, 0x12, 0xAB, 0, 0, 0, 0, 0};
  CHECK(can_signal_extract_raw(&(const CanSignal&)make(0, 16, CAN_BYTE_ORDER_INTEL), data) == 0x1234);
  CHECK(can_signal_extract_raw(&(const CanSignal&)make(4, 12, CAN_BYTE_ORDER_INTEL), data) == 0x123);
  CHECK(can_signal_extract_raw(&(const CanSignal&)make(16, 4, CAN_BYTE_ORDER_INTEL), data) == 0xB);
  CHECK(can_signal_extract_raw(&(const CanSignal&)make(1, 1, CAN_BYTE_ORDER_INTEL), data) == 0);
  CHECK(can_signal_extract_raw(&(const CanSignal&)make(2, 1, CAN_BYTE_ORDER_INTEL), data) == 1);
}

TEST_CASE("Motorola signals read big endian in sawtooth order", "[can_signal]") {
  const uint8_t data[8] = {0x12, 0x34, 0x56, 0, 0, 0, 0, 0};
  // start bit 7 = MSB of byte 0, 16 bits -> bytes 0..1 big endian
  CHECK(can_signal_extract_raw(&(const CanSignal&)make(7, 16, CAN_BYTE_ORDER_MOTOROLA), data) == 0x1234);
  // start bit 3, 12 bits: low nibble of byte 0, then byte 1
  CHECK(can_signal_extract_raw(&(const CanSignal&)make(3, 12, CAN_BYTE_ORDER_MOTOROLA), data) == 0x234);
  // start bit 15, 16 bits: bytes 1..2
  CHECK(can_signal_extract_raw(&(const CanSignal&)make(15, 16, CAN_BYTE_ORDER_MOTOROLA), data) == 0x3456);
}

TEST_CASE("Signal layouts are validated", "[can_signal]") {
  CanSignal s = make(56, 8, CAN_BYTE_ORDER_INTEL);
  CHECK(can_signal_layout_valid(&s));
  s = make(57, 8, CAN_BYTE_ORDER_INTEL);
  CHECK_FALSE(can_signal_layout_valid(&s));
  s = make(7, 64, CAN_BYTE_ORDER_MOTOROLA);
  CHECK(can_signal_layout_valid(&s));
  s = make(0, 2, CAN_BYTE_ORDER_MOTOROLA);  // would continue at bit 15 of the next byte: fine
  CHECK(can_signal_layout_valid(&s));
  s = make(56, 2, CAN_BYTE_ORDER_MOTOROLA);  // LSB would be in byte 8
  CHECK_FALSE(can_signal_layout_valid(&s));
  s = make(0, 0, CAN_BYTE_ORDER_INTEL);
  CHECK_FALSE(can_signal_layout_valid(&s));
}

TEST_CASE("Signed signals, factor and offset", "[can_signal]") {
  // Temperature: 12 bit signed, 0.1 degC per bit, offset -10
  const CanSignal temp = make(0, 12, CAN_BYTE_ORDER_INTEL, true, 0.1, -10.0);
  uint8_t data[8] = {};
  REQUIRE(can_signal_encode(&temp, data, 21.5));  // raw 315
  CHECK(can_signal_extract_raw(&temp, data) == 315);
  CHECK(can_signal_decode(&temp, data) == Catch::Approx(21.5));

  REQUIRE(can_signal_encode(&temp, data, -50.0));  // raw -400
  CHECK(can_signal_raw_to_int(&temp, can_signal_extract_raw(&temp, data)) == -400);
  CHECK(can_signal_decode(&temp, data) == Catch::Approx(-50.0));
}

TEST_CASE("Encoding keeps other bits and rejects values out of range", "[can_signal]") {
  const CanSignal nibble = make(4, 4, CAN_BYTE_ORDER_INTEL);
  uint8_t data[8] = {0x0F, 0xFF, 0, 0, 0, 0, 0, 0};
  REQUIRE(can_signal_encode(&nibble, data, 0xA));
  CHECK(data[0] == 0xAF);
  CHECK(data[1] == 0xFF);
  CHECK_FALSE(can_signal_encode(&nibble, data, 16));
  CHECK_FALSE(can_signal_encode(&nibble, data, -1));
  CHECK(data[0] == 0xAF);

  const CanSignal motorola = make(7, 16, CAN_BYTE_ORDER_MOTOROLA);
  uint8_t be[8] = {};
  REQUIRE(can_signal_encode(&motorola, be, 0xBEEF));
  CHECK(be[0] == 0xBE);
  CHECK(be[1] == 0xEF);
}

TEST_CASE("64 bit signals round trip", "[can_signal]") {
  const CanSignal all = make(0, 64, CAN_BYTE_ORDER_INTEL);
  uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  const uint64_t raw = can_signal_extract_raw(&all, data);
  CHECK(raw == 0x0807060504030201ull);
  uint8_t copy[8] = {};
  can_signal_insert_raw(&all, copy, raw);
  CHECK(std::memcmp(copy, data, 8) == 0);
}
