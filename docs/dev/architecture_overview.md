# Architecture Overview

```
 ┌──────────────┐   TCP, text lines    ┌──────────────┐   TCP    ┌────────────────────┐
 │   can_sim    │ ───────────────────▶ │  iotwin-bus  │ ◀──────▶ │ IoTwin Visualizer  │
 │ (light ECU)  │ ◀─────────────────── │ virtual CAN  │          │ (Unreal Engine 5)  │
 └──────────────┘                      └──────┬───────┘          └────────────────────┘
                                              │ TCP
                                      ┌───────┴────────┐  USB / SLCAN  ┌────────────────┐  FDCAN
                                      │ serial_bridge  │ ◀───────────▶ │ NUCLEO-G0B1RE  │ ◀──────▶ real CAN bus
                                      └────────────────┘               │ (gateway, B1,  │
                                                                       │  LD4)          │
                                                                       └────────────────┘
        further simulators, tools (netcat, tests) can join as additional nodes
```

- **iotwin-bus** (C): owns the bus. Validates, normalises and broadcasts
  frames to every other node ([ADR 0002](../adr/0002-bus-server-and-text-protocol.md)).
- **can_sim** (C): a simulated ECU with a cyclic control loop. It reacts to
  the wall switch and commands the lamp relay.
- **Visualizer** (Unreal Engine 5.6): a digital twin of the electrical
  installation. It mirrors bus signals into the scene (relay → power → lamp)
  and sends user interactions (wall switch) back to the bus.
- **serial_bridge** (C) and the **NUCLEO-G0B1RE firmware**: bring a real
  microcontroller and its CAN controller onto the virtual bus
  ([Firmware](firmware.md), [ADR 0004](../adr/0004-firmware-in-mono-repo-bare-metal.md)).

## Repository layout

One repository with flat, role-based directories ([ADR 0005](../adr/0005-repository-layout.md)):

| Path | Content |
|------|---------|
| `libs/can` | `CanFrame` + text codec, `CanBus` FIFO, SLCAN, bit timing, signals/DBC, ISO-TP (partly compiled into the firmware) |
| `libs/net` | portable sockets (`net`), line framing (`line_reader`) |
| `libs/serial` | portable serial port (Win32 / termios) |
| `libs/platform` | time, cycle timer, real-time setup, CLI helpers |
| `libs/log` | thread-safe logger |
| `apps/bus` | `iotwin-bus` server |
| `apps/can_sim` | light ECU and `can_sim` |
| `apps/serial_bridge` | SLCAN adapter ↔ bus |
| `apps/socketcan_bridge` | Linux SocketCAN ↔ bus |
| `apps/can_monitor` | live bus view with DBC decoding |
| `firmware` | NUCLEO-G0B1RE gateway firmware (separate CMake project) |
| `visualizer` | Unreal Engine project |
| `data` | DBC files, component database, system descriptions |
| `tests` | Catch2 unit, socket and end-to-end tests |
| `tools` | run, flash and packaging scripts |
| `docs` | this documentation, ADRs, `Doxyfile` |

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
