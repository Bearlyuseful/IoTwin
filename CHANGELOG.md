# Changelog

## Unreleased

### Schematic view (visualizer)
- TAB switches between the 3D scene and a 2D schematic built from
  `data/systems/light-demo.json`: nodes, CAN bus with members, USB link.
- Live traffic: every frame decoded with the DBC (value tables, Intel and
  Motorola signals) at the node that sent it, animated from producer to
  consumers, node status, frame rate and message log.
- Detail panel per node incl. characteristics from the component database
  (unverified values marked).
- The release package now contains the whole `data/` folder (replaces `config/`).
- Automation tests for DBC reader, system description and component catalog.

### Component database
- `data/components/`: parts with characteristics for simulation (value or
  min/typ/max, unit, source, verified flag), interfaces, ports and contained
  parts; first entries: STM32G0B1RET6 and NUCLEO-G0B1RE.
- `data/systems/light-demo.json`: nodes, buses, links and messages of the
  demo - the input for the planned schematic view.
- JSON schemas for editor support and `tools/validate_data.py` (CTest).

### Repository
- Flat layout: `libs/` (shared C libraries), `apps/` (programs), `firmware/`,
  `visualizer/` (Unreal project, was `src/ue/IoTwin`), `data/` (was `config/`),
  see ADR 0005. User-specific Rider `.idea` files are no longer tracked.

## 0.2.0 - 2026-11-13

Roadmap steps 1-3: a real microcontroller on the virtual bus.

### Firmware (NUCLEO-G0B1RE)
- Bare-metal on CMSIS headers: startup code and vector table in C, linker
  script, 64 MHz PLL clock, interrupt driven USART2 with ring buffers.
- FDCAN1 driver on register level: message RAM, run-time bit timing,
  Tx FIFO, Rx FIFO 0 interrupt, bus-off recovery; normal, internal loopback
  and bus monitoring mode.
- SLCAN adapter (incl. extensions `l` loopback and `Q` statistics), echo
  suppression in loopback mode, user button = wall switch, LED = lamp.

### Core
- `serial_bridge`: SLCAN adapter <-> iotwin-bus, auto-reconnect on both sides.
- Portable serial port layer (Win32 / termios).
- Shared, host-tested code: SLCAN codec, CAN bit timing calculation.
- Tests: gateway logic, SLCAN, bit timing, bridge, PTY serial port and an
  end-to-end test with the real bridge process (Linux).

### CAN tools (roadmap steps 4 and 5)
- DBC reader (messages, signals, multiplexing, value tables, extended ids)
  and a signal codec for Intel/Motorola layouts, sign, factor and offset.
- `can_monitor`: live view of the bus, decoded with `config/can/iotwin.dbc`.
- ISO-TP (ISO 15765-2) library: segmentation, flow control with block size
  and STmin, WAIT/OVFLW, N_Bs/N_Cr timeouts, padding - portable, no heap.
- `socketcan_bridge` (Linux): any SocketCAN interface <-> iotwin-bus.

### Repository
- Unreal assets (`*.uasset`, `*.umap`) are stored with Git LFS.

### Tooling
- CI builds the firmware; `tools/flash-firmware.ps1`; release package with
  firmware and `start-demo-hardware.bat`.

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
