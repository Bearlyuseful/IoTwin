# ADR 0003: Linux (PREEMPT_RT) as Target Platform, Windows Best Effort

**Date:** 2026-11-13
**Status:** Accepted

**Context:**
The core simulation should be able to show realistic real-time behaviour
(cycle times, jitter) on a PREEMPT_RT Linux kernel. At the same time the
Unreal visualizer is developed on Windows, and running the whole demo on
one Windows machine makes development and testing much easier.

**Decision:**

- Linux is the reference platform of the C core. Real-time features are
  implemented for Linux only: `SCHED_FIFO` priorities, CPU pinning,
  `mlockall()` and drift-free cycles with `clock_nanosleep(TIMER_ABSTIME)`.
- The same sources build on Windows with MSYS2 GCC through a thin platform
  layer (`libs/platform`, `libs/net/net.*`). On Windows the
  programs run *best effort*: correct behaviour, but no timing guarantees.
- Simulation logic (e.g. `light_ecu.c`) is free of platform code and time
  sources so it stays portable and unit-testable.
- Every cyclic loop measures its wake-up latency (`--stats N`) so the
  difference between Windows, standard Linux and PREEMPT_RT is visible.

**Consequences:**

- Both platforms are built and tested in CI.
- If real-time requirements grow (e.g. bit-timing accurate arbitration),
  Windows can be reduced to a demo mode without touching the simulation
  logic; nothing in the logic may depend on Windows.
- The TCP link to Unreal is never hard real-time; the visualizer mirrors
  the state of the bus.
