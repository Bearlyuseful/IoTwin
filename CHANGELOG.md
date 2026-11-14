# Changelog

## 0.4.0 - 2026-11-14

The electrical simulation of the installation: a core shared by the three
planned products, running inside the visualizer and fast enough for a whole
house within a 10 ms real-time step.

### Compatibility
- Visualizer: whether a device has power now comes from the electrical
  simulation. `UElectricalPortComponent::HasPower()` and `GetSupplyLevel()`
  report simulated values instead of searching the wiring; `SetSupplying()`
  only marks a supply output. A lamp stays dark when its breaker trips, even
  though the CAN relay is closed.
- `UElectricalDeviceComponent` no longer ticks: it is a load with
  `RatedPower` (default 60 W) and receives voltage, current and power.
- The release package contains `bin\iotwin-sim` and `data\installations`.

### Electrical simulation core
- ADR 0006: one core for three products (test platform, renovation game,
  electrician training); ADR 0007: the electrical simulation owns all
  electrical results, Unreal displays them.
- `libs/elsim`: installation model with devices, terminals, conductors and
  cables; network solver with complex phasors;
  supply in TN-S, TN-C-S and TT; circuit breakers (magnetic and thermal),
  fuses (I²t) and RCDs incl. test button; faults (impedance between any two
  terminals, interrupted conductors); measurements of voltage, loop
  impedance and insulation resistance (refused while live).
- `data/installations/demo-flat.json` with schema; new components: circuit
  breakers B10/B16, RCD 40 A / 30 mA, fuse gG 16 A, lamp, kettle.
  `tools/validate_data.py` checks installations as well.
- `libs/json`: small JSON reader for the C core.
- `iotwin-sim`: command line simulator, interactive or as a script with
  `expect` checks (CTest `electrical_demo`); included in the release package.

### Electrical simulation in the visualizer
- ADR 0008: transport-independent session interface (commands in, snapshots
  and events out), adapters per product - in-process first, Zenoh next.
- `libs/elsim/el_session`: lock-free, wait-free queues and triple-buffered
  snapshots between the simulation thread and the reader.
- The core is portable C99 (own phasor type instead of C99 complex numbers)
  and compiles with MSVC; the solver no longer allocates per step.
- New device types `junction` and `dimmer` (lossless series reactance: RMS
  current and power of a dimmed resistive load).
- Unreal: the core is compiled into the visualizer and runs in its own
  thread with 10 ms steps. Ports, power sources, cables and loads describe
  themselves to the model; the lamp, its brightness and the HUD show the
  simulated values (voltage, current, power, breaker F1). Unreal no longer
  decides "powered" itself.
- Keys F (short circuit at the lamp) and R (reset breakers); smoke-test
  options `-IoTwinShortCircuitAfter=<s>` and `-IoTwinElectricalDisabled`.

### Sparse solver for whole houses
- Nodal analysis with a sparse LDLᵀ factorization (`el_sparse`): minimum-degree
  ordering once per topology, numeric factorization only when values change,
  otherwise only a substitution per step; no memory allocation after the first
  solution. Dense modified nodal analysis stays as the reference.
- Supplies as Norton equivalents (no ideal voltage sources); open contacts
  stay in the network as zero admittances, so switching keeps the topology.
- `el_generate`: reproducible synthetic houses (three-phase, RCD groups,
  lighting and socket circuits); `elsim_bench` times them.
- House with 1202 nodes: steady step about 40 µs (dense: 21 ms), a light
  switched 70 µs, short circuit with trip 0.7 ms.

## 0.3.0 - 2026-11-13

Next phase, features A-E: component database, 2D schematic view, UDS
diagnostics, scenarios with fault injection, power model and dimming.

### Compatibility
- `LAMP_CMD` has two bytes (brightness in byte 1); receivers treat a missing
  byte as 100 %, so older senders still work.
- The release package contains `data/` (DBC, components, systems,
  scenarios) instead of `config/`.
- The bus protocol accepts control (`!`) and test bench (`@`) lines; nodes
  that never send them are not affected.

### Component database
- STM32G0B1: USART, LPUART, SPI, I2C, UCPD and USB instances verified from
  the ST device header.
- `data/components/`: parts with characteristics for simulation (value or
  min/typ/max, unit, source, verified flag), interfaces, ports and contained
  parts; first entries: STM32G0B1RET6 and NUCLEO-G0B1RE.
- `data/systems/light-demo.json`: nodes, buses, links and messages of the
  demo - the input for the planned schematic view.
- JSON schemas for editor support and `tools/validate_data.py` (CTest).

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

### UDS diagnostics
- `libs/can/uds`: ISO 14229-1 server on ISO-TP, portable C without heap (host
  and STM32): session control, ECU reset, read/write data by identifier,
  read/clear DTCs, tester present; functional addressing (0x7DF), suppress
  positive response, S3 timeout.
- Light ECU (`can_sim`, 0x7E0/0x7E8): identification, live data, writable
  cycle and blink time; console command `fault short|open|supply on|off`
  injects faults that the ECU detects as DTCs (a short switches the output off).
- Gateway firmware (0x7E1/0x7E9): identification incl. STM32 unique id, CAN
  error and frame counters, uptime, DTC U0073-88 bus off, soft/hard reset.
- `uds_client`: tester for iotwin-bus, one-shot (`scan`, `read`, `write`,
  `dtc`, `clear`, `reset`, `raw`, chained with `+`) or interactive with
  automatic TesterPresent.
- `serial_bridge` configures the adapter again after the board was reset.
- Diagnostic messages in the DBC and the system description
  (`"protocol": "uds"`); the schematic view decodes them and shows the tester.

### Scenarios and faults
- `iotwin-bus` fault injection of the medium, controlled with `!fault`
  lines: frame loss (optionally by id, reproducible with a seed), delay
  with jitter (no reordering), silence; `!status` with counters.
- Test bench channel: `@<node> <command>` reaches only registered nodes;
  `can_sim` registers as `ecu` and runs e.g. `fault short on`.
- `can_monitor --log` records in candump -L format, `can_replay` plays such
  logs (also from can-utils) with the original timing, speed factor, loop
  and id filter.
- `can_scenario` runs scripts with `send`, `expect`, `expect-not`, `wait`,
  `bench`, `bus`, `uds`; seven scenarios of the light demo run as CTest
  `scenarios` (fresh bus and ECU each, Linux and Windows).
- Visualizer: the schematic shows injected bus faults (red bus, fault text,
  lost and delayed frames); it registers on the bus as `visualizer`.

### Power and dimming
- `libs/power`: electrical model of a switched output: supply with internal
  resistance, slow fuse (I2t), smart high-side switch with PWM, current limit
  and restart, resistive load.
- Light ECU: brightness 0..100 % (`DIMMER_CMD` 0x201, console `dim`, DID
  0105), `LAMP_CMD` carries it in byte 1; `LAMP_STATUS` (0x101) with current,
  supply voltage, duty and flags every 200 ms; DIDs 0106 current, 0107 fuse.
- The existing faults act on the circuit; new fault `overload`: overcurrent
  DTC `B1001-19`, the 3 A fuse blows after about 2 s (`B1003-13`),
  `fuse replace`.
- Gateway firmware: LD4 dimmed with TIM2 PWM; DID 0204 compares commanded
  and measured brightness (verified on the NUCLEO).
- Visualizer: mouse wheel dims, the lamp follows the brightness, the HUD
  shows current and voltage.
- Scenarios `dimmer` and `overload-fuse`; `can_scenario` expectations also
  see reactions that arrive while an action is still running.

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
