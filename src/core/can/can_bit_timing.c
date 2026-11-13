#include "can_bit_timing.h"

#include <stddef.h>

const CanBitTimingLimits CAN_BIT_TIMING_LIMITS_FDCAN_NOMINAL = {
    .max_prescaler = 512,
    .max_seg1 = 256,
    .max_seg2 = 128,
    .max_sjw = 128,
    .min_tq_per_bit = 8,
    .max_tq_per_bit = 25,
};

static uint32_t distance(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }

bool can_bit_timing_calculate(uint32_t clock_hz, uint32_t bitrate, uint32_t sample_point_permille,
                              const CanBitTimingLimits* limits, CanBitTiming* timing) {
  if (clock_hz == 0 || bitrate == 0 || limits == NULL || timing == NULL ||
      sample_point_permille == 0 || sample_point_permille >= 1000) {
    return false;
  }

  bool found = false;
  uint32_t best_error = UINT32_MAX;
  CanBitTiming best = {0};

  /* From many to few time quanta: on equal sample point error the finer
     resolution (found first) wins. */
  for (uint32_t tq = limits->max_tq_per_bit; tq >= limits->min_tq_per_bit && tq >= 4; tq--) {
    const uint64_t clocks_per_bit = (uint64_t)bitrate * tq;
    if (clock_hz % clocks_per_bit != 0) {
      continue; /* bitrate not exactly reachable with this tq count */
    }
    const uint64_t prescaler = clock_hz / clocks_per_bit;
    if (prescaler < 1 || prescaler > limits->max_prescaler) {
      continue;
    }

    uint32_t seg2 = (tq * (1000u - sample_point_permille) + 500u) / 1000u;
    if (seg2 < 1) {
      seg2 = 1;
    }
    if (seg2 > tq - 2) {
      continue;
    }
    const uint32_t seg1 = tq - 1u - seg2;
    if (seg1 < 1 || seg1 > limits->max_seg1 || seg2 > limits->max_seg2) {
      continue;
    }

    const uint32_t sample_point = (1u + seg1) * 1000u / tq;
    const uint32_t error = distance(sample_point, sample_point_permille);
    if (!found || error < best_error) {
      found = true;
      best_error = error;
      best.prescaler = (uint32_t)prescaler;
      best.tq_per_bit = tq;
      best.seg1 = seg1;
      best.seg2 = seg2;
      best.sjw = seg2 < limits->max_sjw ? seg2 : limits->max_sjw;
      best.sample_point_permille = sample_point;
    }
  }

  if (found) {
    *timing = best;
  }
  return found;
}
