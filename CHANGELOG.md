# Changelog

## 0.1.0 - 2026-11-13

First runnable version: the light demo end to end.

### Core (C)
- `iotwin-bus`: virtual CAN bus over TCP, forwards frames between all nodes,
  validates and normalises the `candump`-style text protocol.
- `can_sim`: simulated light ECU with a drift-free cyclic control loop,
  automatic reconnect and an interactive console.
- Platform layer for Linux (real-time: `SCHED_FIFO`, CPU pinning, `mlockall`,
  wake-up latency statistics) and Windows (MSYS2 GCC, best effort).
- 36 Catch2 tests including socket integration tests; CI on Linux and Windows.

### Visualizer (Unreal Engine 5.6)
- Bus subsystem with background connection, CAN signal component and
  Blueprint helpers.
- Electrical model: persistent symmetric wiring, cycle-safe power search,
  CAN-controlled relay, visible cable, lamp bulb, clickable wall switch.
- Light demo scene generated at runtime, status HUD with recent bus traffic.

### Tooling
- `tools/package.ps1` builds the Windows release, `tools/run-dev.ps1` and
  `tools/run-core.sh` start the system for development.
