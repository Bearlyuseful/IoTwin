#ifndef IOTWIN_CYCLE_TIMER_H
#define IOTWIN_CYCLE_TIMER_H

/**
 * @file cycle_timer.h
 * @brief Drift-free periodic execution with wake-up latency statistics.
 *
 * The timer sleeps until absolute deadlines (start + n * period), so the
 * cycle does not drift with the execution time of the loop body. The
 * measured wake-up latency (actual wake-up minus deadline) is the usual
 * figure of merit for real-time systems and makes the difference between
 * a standard and a PREEMPT_RT kernel visible.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Wake-up latency statistics of a cycle timer. */
typedef struct {
  uint64_t samples;       /**< Number of completed cycles. */
  int64_t min_latency_ns; /**< Smallest wake-up latency. */
  int64_t max_latency_ns; /**< Largest wake-up latency. */
  double sum_latency_ns;  /**< Sum of latencies (for the mean). */
  uint64_t overruns;      /**< Deadlines missed completely (cycle skipped). */
} CycleStats;

/** @brief Periodic timer state. */
typedef struct {
  uint64_t period_ns;        /**< Cycle period. */
  uint64_t next_deadline_ns; /**< Next absolute deadline on the monotonic clock. */
  CycleStats stats;          /**< Statistics since the last reset. */
} CycleTimer;

/** @brief Starts the timer; the first deadline is one period from now. */
void cycle_timer_start(CycleTimer* timer, uint64_t period_ns);

/**
 * @brief Sleeps until the next deadline and records the wake-up latency.
 * @return Number of cycles that were skipped because the loop was too late (0 normally).
 */
uint64_t cycle_timer_wait(CycleTimer* timer);

/** @brief Clears the statistics. */
void cycle_stats_reset(CycleStats* stats);

/** @brief Mean wake-up latency in nanoseconds (0 if there are no samples). */
double cycle_stats_mean_ns(const CycleStats* stats);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_CYCLE_TIMER_H */
