#include <catch2/catch_all.hpp>

#include "can_bit_timing.h"

namespace {

CanBitTiming calculate(uint32_t clock, uint32_t bitrate, uint32_t sample_point = 875) {
  CanBitTiming timing{};
  REQUIRE(can_bit_timing_calculate(clock, bitrate, sample_point, &CAN_BIT_TIMING_LIMITS_FDCAN_NOMINAL,
                                   &timing));
  return timing;
}

uint32_t resulting_bitrate(uint32_t clock, const CanBitTiming& t) {
  return clock / (t.prescaler * (1 + t.seg1 + t.seg2));
}

}  // namespace

TEST_CASE("Bit timing for 500 kbit/s from 64 MHz matches the textbook values", "[bit_timing]") {
  const CanBitTiming t = calculate(64000000, 500000);
  CHECK(t.prescaler == 8);
  CHECK(t.tq_per_bit == 16);
  CHECK(t.seg1 == 13);
  CHECK(t.seg2 == 2);
  CHECK(t.sjw == 2);
  CHECK(t.sample_point_permille == 875);
}

TEST_CASE("Bit timing reaches every SLCAN bitrate exactly from 64 MHz", "[bit_timing]") {
  const uint32_t bitrates[] = {10000, 20000, 50000, 100000, 125000, 250000, 500000, 800000, 1000000};
  for (uint32_t bitrate : bitrates) {
    INFO("bitrate " << bitrate);
    const CanBitTiming t = calculate(64000000, bitrate);
    CHECK(resulting_bitrate(64000000, t) == bitrate);
    CHECK(t.tq_per_bit == 1 + t.seg1 + t.seg2);
    CHECK(t.sample_point_permille >= 850);
    CHECK(t.sample_point_permille <= 900);
    CHECK(t.prescaler <= CAN_BIT_TIMING_LIMITS_FDCAN_NOMINAL.max_prescaler);
  }
}

TEST_CASE("Bit timing honours a requested sample point", "[bit_timing]") {
  const CanBitTiming t = calculate(64000000, 500000, 750);
  CHECK(t.sample_point_permille == 750);
  CHECK(t.seg2 == 4);
}

TEST_CASE("Bit timing for other common clocks", "[bit_timing]") {
  // 80 MHz (e.g. STM32G4) at 1 Mbit/s: 20 tq (prescaler 4) only reaches 85 % sample
  // point, 16 tq (prescaler 5) hits 87.5 % exactly and wins.
  const CanBitTiming t80 = calculate(80000000, 1000000);
  CHECK(resulting_bitrate(80000000, t80) == 1000000);
  CHECK(t80.tq_per_bit == 16);
  CHECK(t80.prescaler == 5);
  CHECK(t80.sample_point_permille == 875);

  const CanBitTiming t8 = calculate(8000000, 125000);
  CHECK(resulting_bitrate(8000000, t8) == 125000);
}

TEST_CASE("Bit timing rejects impossible requests", "[bit_timing]") {
  CanBitTiming t{};
  // 64 MHz cannot produce 33.333 kbit/s exactly with 8..25 tq.
  CHECK_FALSE(can_bit_timing_calculate(64000000, 33333, 875, &CAN_BIT_TIMING_LIMITS_FDCAN_NOMINAL, &t));
  // 1 kbit/s would need a prescaler above 512.
  CHECK_FALSE(can_bit_timing_calculate(64000000, 1000, 875, &CAN_BIT_TIMING_LIMITS_FDCAN_NOMINAL, &t));
  CHECK_FALSE(can_bit_timing_calculate(64000000, 0, 875, &CAN_BIT_TIMING_LIMITS_FDCAN_NOMINAL, &t));
  CHECK_FALSE(can_bit_timing_calculate(64000000, 500000, 1000, &CAN_BIT_TIMING_LIMITS_FDCAN_NOMINAL, &t));
}
