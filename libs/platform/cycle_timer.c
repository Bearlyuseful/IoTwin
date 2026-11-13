#include "cycle_timer.h"

#include <stdint.h>

#include "pal_time.h"

void cycle_stats_reset(CycleStats* stats) {
  stats->samples = 0;
  stats->min_latency_ns = INT64_MAX;
  stats->max_latency_ns = 0;
  stats->sum_latency_ns = 0.0;
  stats->overruns = 0;
}

double cycle_stats_mean_ns(const CycleStats* stats) {
  return stats->samples ? stats->sum_latency_ns / (double)stats->samples : 0.0;
}

void cycle_timer_start(CycleTimer* timer, uint64_t period_ns) {
  timer->period_ns = period_ns > 0 ? period_ns : 1;
  timer->next_deadline_ns = pal_time_monotonic_ns() + timer->period_ns;
  cycle_stats_reset(&timer->stats);
}

uint64_t cycle_timer_wait(CycleTimer* timer) {
  pal_sleep_until_ns(timer->next_deadline_ns);

  const uint64_t now = pal_time_monotonic_ns();
  const int64_t latency = (int64_t)(now - timer->next_deadline_ns);
  CycleStats* stats = &timer->stats;
  stats->samples++;
  stats->sum_latency_ns += (double)latency;
  if (latency < stats->min_latency_ns) {
    stats->min_latency_ns = latency;
  }
  if (latency > stats->max_latency_ns) {
    stats->max_latency_ns = latency;
  }

  timer->next_deadline_ns += timer->period_ns;
  uint64_t missed = 0;
  if (now >= timer->next_deadline_ns) {
    /* We woke up after the following deadline already: skip the lost cycles
       instead of running a burst of back-to-back iterations. */
    missed = (now - timer->next_deadline_ns) / timer->period_ns + 1;
    timer->next_deadline_ns += missed * timer->period_ns;
    stats->overruns += missed;
  }
  return missed;
}
