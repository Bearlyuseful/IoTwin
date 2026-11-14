# Electrical Simulation

The electrical simulation core computes how the wiring of a house behaves:
voltages, currents and power, circuit breakers, fuses and RCDs, faults, and
the measurements an electrician makes. It is the shared core of the three
products ([ADR 0006](../adr/0006-one-core-three-products.md)); its design is
in [ADR 0007](../adr/0007-electrical-simulation-core.md), its interface to
Unreal and other frontends in [ADR 0008](../adr/0008-simulation-interface.md).

```bash
iotwin-sim                       # demo flat, interactive
sim> state
sim> fault socket_kitchen:L socket_kitchen:N
     0.20 s  f2 tripped (short circuit, 402 A = 25.1 x In)
sim> z socket_kitchen:L socket_kitchen:PE
  Z = 0.567 Ohm, prospective fault current at 230 V: 406 A
```

| Part | Where |
|------|-------|
| Network solver (nodal analysis, complex phasors) | `libs/elsim/el_circuit.*`, sparse LDLᵀ in `el_sparse.*` |
| Installation model, protection, faults, measurements | `libs/elsim/elsim.*` |
| Loading installations and components from `data/` | `libs/elsim/el_install.*` |
| Session: commands in, snapshots and events out, lock-free between threads | `libs/elsim/el_session.*` |
| Generated houses for benchmarks and tests, benchmark tool | `libs/elsim/el_generate.*`, `apps/elsim_bench` |
| In the visualizer (compiled in, own thread) | `visualizer/Source/IoTwin/Electrical` ([Visualizer](visualizer.md#electrical-simulation-sourceiotwinelectrical)) |
| JSON reader | `libs/json` |
| Command line simulator | `apps/iotwin_sim` (`iotwin-sim`) |
| Installations | `data/installations/*.json`, schema `data/schema/installation.schema.json` |

## Installation files

An installation lists **devices** with typed **terminals**, **cables** and
**connections**. A connection is one conductor between two terminals,
written `device:terminal`:

```json
{
  "id": "demo-flat",
  "name": "Demo flat: lighting and kitchen socket",
  "cables": [ { "id": "w-kitchen", "type": "NYM-J 3x2.5", "cross_section": 2.5, "length": 15 } ],
  "devices": [
    { "id": "intake", "type": "supply", "system": "TN-S", "voltage": 230, "loop_impedance": 0.35 },
    { "id": "f2", "type": "breaker", "component": "generic.mcb-b16" },
    { "id": "socket_kitchen", "type": "socket" },
    { "id": "kettle", "type": "load", "component": "generic.kettle-2000w", "plugged_into": "socket_kitchen" }
  ],
  "connections": [
    { "id": "kitchen-l", "from": "f2:out", "to": "socket_kitchen:L", "role": "L", "cable": "w-kitchen" }
  ]
}
```

| Type | Terminals | Parameters (component or device) |
|------|-----------|----------------------------------|
| `supply` | `L1 L2 L3 N PE` | `system` (TN-S, TN-C-S, TT), `voltage` (phase, 230), `frequency`, `loop_impedance` Ze, `earth_electrode` (TT), `source_earth` |
| `breaker` | `in out` | `rated_current`, characteristic B/C/D or `magnetic_trip_multiple`, `thermal_limit` |
| `fuse` | `in out` | `rated_current`, `melting_i2t` |
| `rcd` | `L_in N_in L_out N_out` | `rated_current`, `rated_residual_current` |
| `switch` | `in out` | `contact_resistance`; starts **off** unless `"on": true` |
| `load` | `L N PE` | `rated_power`, `rated_voltage`; `plugged_into` a socket |
| `socket` | `L N PE` | – |
| `junction` | `L N PE` | – (junction box, terminal block; a port in Unreal) |
| `dimmer` | `in out` | `level` 0..1, `contact_resistance`; starts **off** unless `"on": true` |

Parameters are taken from the type defaults, then from the referenced
component in `data/components` (e.g. `generic.mcb-b16`), then from the device
entry itself. Connections without a cable are short internal links (1 mΩ);
with a cable their resistance is `0.0178 Ω·mm²/m × length / cross-section`
(copper, 20 °C). `tools/validate_data.py` checks types, terminals, cables,
components and sockets (CTest `data_validation`).

## Physics

- **Steady state per time step** (10 ms), complex phasors at 50 Hz, nodal
  analysis with a sparse solver (see [Solver](#solver)). All elements are
  linear; loads are resistive (`R = U² / P`).
- **Supply:** three sources 230 V at 0°, −120°, +120° behind half of the
  loop impedance Ze per conductor (L1-L3, N, PE), so L-N and L-PE loops both
  see Ze. The sources enter the network as Norton equivalents (current
  U / (Ze/2) with Ze/2 in parallel), so there are no ideal voltage sources. **TN-S / TN-C-S:** PE connects to the transformer star point.
  **TT:** PE goes to the house earth electrode (`earth_electrode`, default 30 Ω),
  the star point to its own earth (`source_earth`, 1 Ω).
- Every node has 1 GΩ to earth, so open breakers and loose terminals stay
  solvable; it also limits insulation readings (two floating nodes read 500 MΩ).
- **Dimmer:** an ideal (lossless) series reactance X chosen so that the RMS
  current is √level and the power of a resistive load level × the full value,
  as with a phase-cut or PWM dimmer. X follows from the Thevenin impedance
  Z = A + jB the dimmer sees: |Z + jX| = |Z| / √level. It is recomputed only
  when the network changes (`ElModel.revision`). The supply sees reactive
  power instead of the distortion power of a real dimmer.
- A load counts as **powered** when it gets at least 1 % of its rated power.
- Contacts that can open (switches, breakers, fuses, RCD poles, dimmers,
  loads, interrupted conductors) stay in the network as zero admittances when
  open, so switching changes values, not the topology.
- Portable C99: phasors are a small struct (`ElPhasor`) instead of C99 complex
  numbers, so MSVC compiles the core (inside Unreal); after the first solution
  of a network the solver does not allocate memory.

## Protection

Evaluated after every solution; a trip changes the network, which is solved
again in the same step (cascades, e.g. RCD and breaker together).

| Device | Model |
|--------|-------|
| Circuit breaker | **magnetic:** trips at once at `magnetic_trip_multiple` × In (B 5, C 10, D 20 – the upper limits of IEC 60898-1); **thermal:** integrates `((I/In)² − 1) dt` above In, trips at `thermal_limit` (60 s: about 45 s at 1.5 × In), cools down below In |
| Fuse | integrates `(I² − In²) dt`, blows at `melting_i2t`; must be replaced (`reset`) |
| RCD | trips when `|I_L + I_N| ≥ 0.8 × IΔn` (IEC 61008-1: between 0.5 and 1.0 × IΔn); test button draws about 2 × IΔn |

## Faults and measurements

- **Faults:** an impedance between any two terminals (`fault a b ohm`: short
  circuit, insulation fault, earth fault) or an interrupted conductor (`open <connection>`).
- **Voltage** between two terminals (`u`).
- **Impedance** between two terminals (`z`): the Thevenin impedance the
  instrument sees, used for **loop impedance** (L-PE) and continuity; with the
  prospective fault current 230 V / Zs.
- **Insulation resistance** (`riso`): like a real tester it refuses while a
  voltage above 50 V is present. Loads that are still connected show up in
  the reading, as they do on site.

The tests (`tests/test_elsim.cpp`, `tests/elsim/demo-flat.sim`) check exactly
the lessons a trainee should learn, for example:

- a short circuit trips the breaker magnetically, not the RCD;
- an insulation fault of 2 kΩ trips the RCD (113 mA) and darkens every circuit behind it;
- in a **TT** system an earth fault draws only about 7 A: the breaker holds,
  the housing is at a deadly touch voltage, only the RCD protects;
- an interrupted neutral leaves the load dead but puts line potential on N.

## iotwin-sim

```
iotwin-sim [--data DIR] [--script FILE] [installation.json]
```

| Command | |
|---------|--|
| `state` | all devices with state, voltage, current, power |
| `on/off <id>`, `reset <id>`, `test <rcd>` | operate switches, breakers, RCDs, appliances |
| `level <dimmer> <percent>` | dimmer level |
| `fault <a> <b> [ohm]`, `open/close <conn>`, `clear` | faults |
| `plug <load> <socket>`, `unplug <load>` | appliances |
| `u`, `z`, `riso <a> <b>` | measurements |
| `wait <ms>` | let time pass (every change already lets 200 ms pass) |
| `expect <id> powered/unpowered/tripped/on/off`, `expect u/z <a> <b> <min> <max>` | checks for scripts |

With `--script` the exit code is 1 if an `expect` fails (CTest `electrical_demo`).

## Solver

Without ideal voltage sources the nodal admittance matrix Y of the network is
complex symmetric, and its real part (the conductances, plus 1 GΩ from every
node to earth) is positive definite. Such a matrix has an LDLᵀ factorization
for every symmetric ordering without pivoting, so the ordering is chosen for
sparsity alone (`el_sparse.c`):

| Phase | When | Cost |
|-------|------|------|
| Minimum-degree ordering, pattern of L | topology changes (a fault, plugging in) | once, cached (4 topologies) |
| Numeric factorization LDLᵀ | values change (switching, a trip, dimming) | proportional to the fill of L |
| Forward/back substitution | every solution | proportional to the fill of L |

A step of an unchanged network is therefore only a substitution. After the
first solution of a topology the solver does not allocate memory. Dense
modified nodal analysis with partial pivoting remains for networks with
voltage sources and as the reference (`EL_SOLVER_DENSE`, used by the tests).

`elsim_bench` measures it on a generated house (three-phase supply, RCD
groups, lighting and socket circuits with junction boxes, lamps, switches and
appliances):

```bash
elsim_bench                      # 24 circuits x 6 boxes, about 1200 nodes
elsim_bench --circuits 96 --boxes 8 --dense
```

| 24 circuits x 6 boxes, 1202 nodes | Windows (MinGW) | Linux (WSL) |
|---|---|---|
| steady step (mean / worst) | 42 / 65 µs | 48 / 136 µs |
| light switched (refactorization) | 70 µs | 129 µs |
| short circuit (new element), breaker trips | 0.66 ms | 0.64 ms |
| first step (ordering + factorization) | 0.56 ms | 0.73 ms |
| dense modified nodal analysis, one step | 21 ms | 34 ms |

With 96 circuits x 8 boxes (6317 nodes) a steady step takes about 0.2 ms, a
switch 0.37 ms and a short circuit 3.5 ms - all within the 10 ms step.
Sparse and dense results agree to about 3·10⁻¹⁰ (CTest `electrical_bench_smoke`
checks it).

## Session interface

`el_session.h` is what every frontend uses ([ADR 0008](../adr/0008-simulation-interface.md)):

```c
ElSession* s = el_session_create(&model);                 /* takes over the model */
ElCommand c = el_command(EL_CMD_SET_LEVEL, "relay", NULL, 0.3);
el_session_send(s, &c);                                   /* command thread */
el_session_step(s, 0.01);                                 /* simulation thread, every 10 ms */
const ElSnapshot* snap = el_session_snapshot(s);          /* reader: newest complete state */
ElEvent e;
while (el_session_next_event(s, &e)) { /* "F1 tripped (short circuit, 503 A = 50.3 x In)" */ }
```

One thread sends commands, one steps, one reads (sender and reader may be
the same). Queues and the snapshot exchange are lock-free and wait-free, so a
real-time simulation thread never waits for a slow reader. Rejected commands
come back as events ("command rejected: ...").

## Limits and next steps

- Resistive loads only; reactive loads (motors, power factor) and three-phase
  devices need only new element types, the solver already uses complex numbers.
- Simplified trip curves and no conductor heating.
- A new fault or plugging in changes the topology (a new ordering, under
  1 ms for a house); a fault generator could reserve elements to avoid that.
- Out-of-process adapter (Zenoh, shared memory) with a FlatBuffers schema for
  a simulator on a separate (real-time) machine and several clients.
- Fault generator and rule checks for the electrician training build on the
  fault and measurement functions.
