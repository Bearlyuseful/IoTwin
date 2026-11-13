#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "pal_time.h"

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#else
#include <errno.h>
#include <time.h>
#endif

#ifdef _WIN32

void pal_time_init(void) { timeBeginPeriod(1); }

uint64_t pal_time_monotonic_ns(void) {
  LARGE_INTEGER frequency;
  LARGE_INTEGER counter;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&counter);
  const uint64_t f = (uint64_t)frequency.QuadPart;
  const uint64_t c = (uint64_t)counter.QuadPart;
  return (c / f) * PAL_NS_PER_SEC + ((c % f) * PAL_NS_PER_SEC) / f;
}

void pal_sleep_ms(uint32_t ms) { Sleep(ms); }

void pal_sleep_until_ns(uint64_t deadline_ns) {
  for (;;) {
    const uint64_t now = pal_time_monotonic_ns();
    if (now >= deadline_ns) {
      return;
    }
    const uint64_t remaining_ms = (deadline_ns - now + PAL_NS_PER_MS - 1) / PAL_NS_PER_MS;
    Sleep((DWORD)remaining_ms);
  }
}

#else /* POSIX */

void pal_time_init(void) {}

uint64_t pal_time_monotonic_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * PAL_NS_PER_SEC + (uint64_t)ts.tv_nsec;
}

void pal_sleep_ms(uint32_t ms) { pal_sleep_until_ns(pal_time_monotonic_ns() + ms * PAL_NS_PER_MS); }

void pal_sleep_until_ns(uint64_t deadline_ns) {
  struct timespec ts;
  ts.tv_sec = (time_t)(deadline_ns / PAL_NS_PER_SEC);
  ts.tv_nsec = (long)(deadline_ns % PAL_NS_PER_SEC);
  while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {
  }
}

#endif

uint64_t pal_time_monotonic_ms(void) { return pal_time_monotonic_ns() / PAL_NS_PER_MS; }
