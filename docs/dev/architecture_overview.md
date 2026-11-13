# Architecture Overview

```
 ┌──────────────┐   TCP, text lines    ┌──────────────┐   TCP    ┌────────────────────┐
 │   can_sim    │ ───────────────────▶ │  iotwin-bus  │ ◀──────▶ │ IoTwin Visualizer  │
 │ (light ECU)  │ ◀─────────────────── │ virtual CAN  │          │ (Unreal Engine 5)  │
 └──────────────┘                      └──────────────┘          └────────────────────┘
        ▲  further simulators, tools (netcat, tests) can join as additional nodes
```

- **iotwin-bus** (C): owns the bus. Validates, normalises and broadcasts
  frames to every other node ([ADR 0002](../adr/0002-bus-server-and-text-protocol.md)).
- **can_sim** (C): a simulated ECU with a cyclic control loop. It reacts to
  the wall switch and commands the lamp relay.
- **Visualizer** (Unreal Engine 5.6): a digital twin of the electrical
  installation. It mirrors bus signals into the scene (relay → power → lamp)
  and sends user interactions (wall switch) back to the bus.

## Repository layout

| Path                              | Content                                                  |
|-----------------------------------|----------------------------------------------------------|
| `src/core/logger`                 | Thread-safe logger                                       |
| `src/core/platform`               | Time, cycle timer, real-time setup, CLI helpers          |
| `src/core/can`                    | `CanFrame` model + text codec, `CanBus` FIFO             |
| `src/communication/tcp`           | Portable sockets (`net`), line framing (`line_reader`)   |
| `src/bus`                         | `iotwin-bus` server                                      |
| `src/simulators/can_sim`          | Light ECU logic and `can_sim` executable                 |
| `src/ue/IoTwin`                   | Unreal Engine project (visualizer)                       |
| `tests`                           | Catch2 unit and socket integration tests                 |
| `tools`                           | Run and packaging scripts                                |

## can_sim threads

| Thread  | Priority                          | Work                                                          |
|---------|-----------------------------------|---------------------------------------------------------------|
| main    | optionally `SCHED_FIFO` (`--rt-*`) | every tick: read socket (non-blocking), ECU step, send, sleep until the next absolute deadline |
| console | normal                            | reads stdin commands, hands frames to the main loop via a `CanBus` FIFO |

The main loop never blocks on the network while connected. Reconnecting
(blocking `connect`) only happens while the bus is unreachable.

## Visualizer

See [Visualizer](visualizer.md) for the Unreal side: the bus subsystem,
CAN signal components and the electrical model.
