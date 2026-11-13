# Roadmap

Status: November 2026 - v0.1.0 released, steps 0-3 implemented and verified on a NUCLEO-G0B1RE

This roadmap merges the two earlier plans - the MVP plan of October 2025 and the
post-MVP plan of March 2026 (virtual CAN bus → serial bridge → STM32 CAN
controller) - with what the MVP actually delivered.

The project doubles as an embedded learning project, so the ordering puts the
embedded topics (MCU, serial, CAN hardware, real-time Linux) before further
digital-twin features.

## Where we are

| Item | Origin | Status |
|------|--------|--------|
| Virtual CAN bus (TCP software bus, `iotwin-bus`, `can_sim`) | Mar 2026, phase 1 | ✅ done in v0.1.0 |
| Communication layer (gRPC idea) | Oct 2025 | replaced by the TCP text protocol ([ADR 0002](adr/0002-bus-server-and-text-protocol.md)) |
| Unreal integration, status display | Oct 2025 | ✅ as module code (bus subsystem, lamp, switch); not yet a standalone plugin |
| Electrical / load simulation | Oct 2025 | ⚠️ on/off only, no load, current or fuse |
| ADR: repository structure (mono vs multi repo) | Oct 2025 | ✅ [ADR 0004](adr/0004-firmware-in-mono-repo-bare-metal.md) |
| Git tag for the MVP | Oct 2025 | ✅ `v0.1.0` |
| Serial bridge PC ↔ MCU | Mar 2026, phase 2 | ✅ `serial_bridge` + SLCAN firmware ([Firmware](dev/firmware.md)) |
| STM32 as CAN controller | Mar 2026, phase 3 | ✅ FDCAN driver, internal loopback; ⚠️ real bus needs a transceiver + second node |
| SocketCAN bridge | step 4 | ✅ `socketcan_bridge` ([CAN Tools](dev/can_tools.md)); live test needs `vcan0` |
| Real-time measurements on PREEMPT_RT | step 4 | ❌ needs PREEMPT_RT hardware |
| DBC, ISO-TP, UDS | step 5 | ✅ DBC reader + `can_monitor`, ISO-TP, UDS server in ECU and gateway, `uds_client` ([Diagnostics](dev/diagnostics.md)) |
| Scenarios, more devices, extensions | Oct 2025 outlook | scenarios ✅ ([Scenarios and Faults](dev/scenarios.md)), more devices open |

## Next phase (agreed November 2026)

The long-term goal is a digital twin that is built from engineering data:
circuit diagrams and network descriptions (incl. wireless links) describe
the physical setup, the visualizer shows it as a **2D schematic** with live
bus messages and status, and can switch from there into the **3D** scene.

Order of work:

| # | Feature | Notes |
|---|---------|-------|
| A | **Component database** ✅ (structure + NUCLEO-G0B1RE) | parts and their characteristics for simulation, first entry: NUCLEO-G0B1RE; plus a system description (instances, connections, buses) |
| B | **Bus / schematic view in Unreal** ✅ (first version) | 2D view of nodes, buses and live messages, switch to 3D ([Visualizer](dev/visualizer.md#schematic-view-sourceiotwinschematic)); open: generated from circuit diagrams, wireless traffic, jump to the matching 3D object |
| C | **UDS diagnostics** ✅ | on top of ISO-TP: server in ECU and gateway, `uds_client`, injectable faults as DTCs ([Diagnostics](dev/diagnostics.md)); open: SecurityAccess, RoutineControl, flashing |
| D | **Scenarios and faults** ✅ | record/replay (candump format), scripted scenarios as tests, bus faults (loss, delay, silence), test bench faults in the ECU ([Scenarios and Faults](dev/scenarios.md)); faults shown in the schematic view; open: per-node bus-off and error frames |
| E | **More devices and electrical model** ✅ (dimmer, current, fuse) | dimmable lamp with PWM (also on the NUCLEO LED), electrical model of the output with supply, fuse and smart switch, `LAMP_STATUS` ([Power and Dimming](dev/power.md)); open: sensors, fan/motor |
| F | **Real bus and real time** | transceiver + second node, PREEMPT_RT measurements (needs hardware) |

Housekeeping: clearer repository structure, release 0.2.0, Git LFS, branch cleanup (all done).
Release 0.3.0 contains features A-E.

## Order of the post-MVP steps

| # | Step | Size | Why here |
|---|------|------|----------|
| 0 | **Close the MVP** ✅ | S | clean starting point |
| 1 | **Phase 2a – MCU basics and serial protocol** ✅ | M | no CAN hardware needed yet, core embedded skills |
| 2 | **Phase 2b – `serial_bridge` on the PC** ✅ | S–M | connects the MCU to the virtual bus |
| 3 | **Phase 3 – STM32 as CAN controller** ✅ (loopback) | L | real hardware nodes in the digital twin |
| 4 | **Real-time Linux and SocketCAN** (SocketCAN ✅, RT measurement open) | M | can run in parallel to 3 |
| 5 | **Protocols on CAN** ✅ (DBC, ISO-TP, UDS) | M–L | builds on 3 and 4 |
| 6 | **Deeper digital twin** | ongoing | lower priority for the learning goal |

### 0. Close the MVP

- Get CI green and merge the MVP pull request, tag `v0.1.0`.
- ADR 0004 – repository structure: firmware stays in this repository under
  `firmware/` with its own `arm-none-eabi-gcc` build (CMake toolchain file),
  separate from the host CMake build.
- Optional: move `.uasset` / `.umap` to Git LFS before more content is added.

### 1. Phase 2a – MCU basics and serial protocol

- Nucleo board bring-up: CMake toolchain file, flashing and debugging
  (ST-LINK / OpenOCD / GDB).
- UART with interrupt or DMA reception into a ring buffer.
- Line protocol on the serial link: **SLCAN (Lawicel)**, e.g. `t1232DEAD\r`.
  It is the de-facto standard for serial CAN adapters (python-can, SavvyCAN,
  CANable), so the STM32 later becomes a regular USB-CAN adapter.
- The SLCAN parser/encoder is portable C shared by host and firmware and unit
  tested on the host with Catch2.

**Done when:** frames typed on the PC are echoed by the MCU and decoded correctly
on both sides; parser tests run in CI.

### 2. Phase 2b – `serial_bridge` on the PC

- New executable that translates SLCAN on a serial port ↔ bus text lines on
  `iotwin-bus`.
- Serial port layer: `termios` on Linux, Win32 COM ports best effort
  ([ADR 0003](adr/0003-linux-rt-target-windows-best-effort.md)).
- Tested without hardware via `socat` pseudo-terminal pairs (also in CI on Linux).

**Done when:** a frame sent from the MCU appears on the virtual bus and vice versa.

### 3. Phase 3 – STM32 as CAN controller

- bxCAN / FDCAN initialisation, **bit-timing calculation** (prescaler, time
  segments, sample point), acceptance filters, RX FIFO interrupt.
- Error handling: error counters, error-passive, bus-off and recovery.
- First in loopback mode, then on a real bus with a transceiver
  (e.g. SN65HVD230) and a **second node** - a single node on a bus never gets an
  ACK. The second node can be another board or a USB-CAN adapter.
- The STM32 acts as gateway: real CAN bus ↔ UART/USB ↔ `serial_bridge` ↔ `iotwin-bus`.

**Done when:** a physical button on the CAN bus switches the lamp in the
visualizer, and the ECU's `LAMP_CMD` drives an LED on the board.

### 4. Real-time Linux and SocketCAN

- Run the core on a PREEMPT_RT kernel (e.g. Raspberry Pi), compare wake-up
  latency with a standard kernel (`can_sim --stats`, `cyclictest`), document the
  results in [Real-Time Operation](dev/realtime.md).
- Bridge `iotwin-bus` ↔ SocketCAN (`vcan0` / `can0`) so can-utils and USB-CAN
  adapters work directly on Linux.

### 5. Protocols on CAN (optional)

- DBC files: signals by name instead of hard-coded ids/bits (core and visualizer).
- ISO-TP (ISO 15765-2) segmentation and flow control, simple UDS services on top -
  or CANopen, depending on the target domain.

### 6. Deeper digital twin

- Electrical model with load, current and fuses (ADR on electrical modelling).
- More devices and sensors, scripted scenarios including fault injection
  (frame loss, delay, bus-off).
- Extract the bus integration into an Unreal plugin ("DeviceConnector").

## Open decisions

- **Hardware** for steps 1-4: which STM32 board, CAN transceiver, USB-CAN
  adapter, Raspberry Pi.
- **Working mode** for the embedded phases: implemented by hand as a learning
  exercise with AI review, or AI-assisted implementation.
- **Serial protocol:** SLCAN (recommended) or the bus text format.
