#ifndef IOTWIN_EL_ATOMIC_H
#define IOTWIN_EL_ATOMIC_H

/*
 * Minimal atomics for the lock-free queues of el_session.c, usable from C
 * with GCC, Clang and MSVC (MSVC's C mode has no <stdatomic.h> by default).
 * Loads acquire, stores release, exchange is a full barrier. Internal header.
 */

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
typedef volatile long ElAtomic;
static __inline long el_atomic_load(ElAtomic* a) { return _InterlockedOr(a, 0); }
static __inline void el_atomic_store(ElAtomic* a, long value) { _InterlockedExchange(a, value); }
static __inline long el_atomic_exchange(ElAtomic* a, long value) { return _InterlockedExchange(a, value); }
#else
typedef long ElAtomic;
static inline long el_atomic_load(ElAtomic* a) { return __atomic_load_n(a, __ATOMIC_ACQUIRE); }
static inline void el_atomic_store(ElAtomic* a, long value) { __atomic_store_n(a, value, __ATOMIC_RELEASE); }
static inline long el_atomic_exchange(ElAtomic* a, long value) {
  return __atomic_exchange_n(a, value, __ATOMIC_ACQ_REL);
}
#endif

#endif /* IOTWIN_EL_ATOMIC_H */
