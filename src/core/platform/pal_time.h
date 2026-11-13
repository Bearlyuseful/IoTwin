#ifndef IOTWIN_PAL_TIME_H
#define IOTWIN_PAL_TIME_H

/**
 * @file pal_time.h
 * @brief Platform abstraction for monotonic time and sleeping.
 *
 * On Linux the absolute sleep uses clock_nanosleep(TIMER_ABSTIME) on
 * CLOCK_MONOTONIC, which is what a PREEMPT_RT system needs for drift-free
 * cyclic execution. On Windows the functions are best effort (millisecond
 * timer resolution, no real-time guarantees).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_NS_PER_MS 1000000ULL
#define PAL_NS_PER_SEC 1000000000ULL

/** @brief One-time process setup (Windows: raises the system timer resolution to 1 ms). */
void pal_time_init(void);

/** @brief Monotonic clock in nanoseconds (arbitrary epoch, never jumps backwards). */
uint64_t pal_time_monotonic_ns(void);

/** @brief Monotonic clock in milliseconds. */
uint64_t pal_time_monotonic_ms(void);

/** @brief Sleeps for at least @p ms milliseconds. */
void pal_sleep_ms(uint32_t ms);

/** @brief Sleeps until the monotonic clock reaches @p deadline_ns. Returns immediately if it already passed. */
void pal_sleep_until_ns(uint64_t deadline_ns);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_PAL_TIME_H */
