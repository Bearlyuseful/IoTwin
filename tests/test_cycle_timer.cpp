#include <catch2/catch_all.hpp>

#include "cycle_timer.h"
#include "pal_time.h"

TEST_CASE("Monotonic clock never goes backwards", "[platform]") {
  uint64_t previous = pal_time_monotonic_ns();
  for (int i = 0; i < 1000; i++) {
    uint64_t now = pal_time_monotonic_ns();
    REQUIRE(now >= previous);
    previous = now;
  }
}

TEST_CASE("Cycle timer waits for absolute deadlines", "[platform]") {
  pal_time_init();
  constexpr uint64_t kPeriod = 5 * PAL_NS_PER_MS;
  CycleTimer timer;
  const uint64_t start = pal_time_monotonic_ns();
  cycle_timer_start(&timer, kPeriod);
  for (int i = 0; i < 6; i++) {
    cycle_timer_wait(&timer);
  }
  const uint64_t elapsed = pal_time_monotonic_ns() - start;

  CHECK(elapsed >= 6 * kPeriod);
  CHECK(timer.stats.samples + timer.stats.overruns >= 6);
  CHECK(timer.stats.min_latency_ns >= 0);
  CHECK(timer.stats.max_latency_ns >= timer.stats.min_latency_ns);
}

TEST_CASE("Cycle timer skips missed cycles instead of bursting", "[platform]") {
  constexpr uint64_t kPeriod = 2 * PAL_NS_PER_MS;
  CycleTimer timer;
  cycle_timer_start(&timer, kPeriod);
  pal_sleep_ms(15);  // miss several deadlines
  uint64_t missed = cycle_timer_wait(&timer);
  CHECK(missed >= 3);
  CHECK(timer.stats.overruns == missed);
  CHECK(timer.next_deadline_ns > pal_time_monotonic_ns());
}
