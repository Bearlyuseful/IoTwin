#ifndef IOTWIN_CAN_BIT_TIMING_H
#define IOTWIN_CAN_BIT_TIMING_H

/**
 * @file can_bit_timing.h
 * @brief Calculation of CAN bit timing parameters from a controller clock.
 *
 * A CAN bit is divided into time quanta (tq):
 *
 *   | SYNC (1 tq) | SEG1 = PROP + PHASE1 | SEG2 = PHASE2 |
 *                                        ^ sample point
 *
 *   bitrate      = clock / (prescaler * (1 + seg1 + seg2))
 *   sample point = (1 + seg1) / (1 + seg1 + seg2)
 *
 * The calculation only accepts exact bitrates (no rounding error) and picks
 * the solution closest to the requested sample point, preferring more time
 * quanta per bit (finer resynchronisation). Portable C, unit tested on the
 * host and used by the STM32 FDCAN driver.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Timing parameters in time quanta (not register encoded). */
typedef struct {
  uint32_t prescaler;             /**< Clock divider for one time quantum. */
  uint32_t tq_per_bit;            /**< 1 + seg1 + seg2. */
  uint32_t seg1;                  /**< Propagation + phase segment 1. */
  uint32_t seg2;                  /**< Phase segment 2. */
  uint32_t sjw;                   /**< Synchronisation jump width. */
  uint32_t sample_point_permille; /**< Resulting sample point, e.g. 875 = 87.5 %. */
} CanBitTiming;

/** @brief Limits of a CAN controller's timing registers. */
typedef struct {
  uint32_t max_prescaler;
  uint32_t max_seg1;
  uint32_t max_seg2;
  uint32_t max_sjw;
  uint32_t min_tq_per_bit;
  uint32_t max_tq_per_bit;
} CanBitTimingLimits;

/** @brief Nominal (arbitration phase) limits of the Bosch M_CAN / STM32 FDCAN. */
extern const CanBitTimingLimits CAN_BIT_TIMING_LIMITS_FDCAN_NOMINAL;

/** @brief Sample point recommended by CiA 301 for most bitrates. */
#define CAN_BIT_TIMING_DEFAULT_SAMPLE_POINT 875u

/**
 * @brief Finds timing parameters for @p bitrate.
 * @return false if no exact solution exists within @p limits.
 */
bool can_bit_timing_calculate(uint32_t clock_hz, uint32_t bitrate, uint32_t sample_point_permille,
                              const CanBitTimingLimits* limits, CanBitTiming* timing);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_CAN_BIT_TIMING_H */
