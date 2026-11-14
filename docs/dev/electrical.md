# Electrical Simulation

The electrical simulation core computes how the wiring of a house behaves:
voltages, currents and power, circuit breakers, fuses and RCDs, faults, and
the measurements an electrician makes. It is the shared core of the three
products ([ADR 0006](../adr/0006-one-core-three-products.md)); its design is
in [ADR 0007](../adr/0007-electrical-simulation-core.md).

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
| Network solver (modified nodal analysis, complex phasors) | `libs/elsim/el_circuit.*` |
| Installation model, protection, faults, measurements | `libs/elsim/elsim.*` |
| Loading installations and components from `data/` | `libs/elsim/el_install.*` |
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

Parameters are taken from the type defaults, then from the referenced
component in `data/components` (e.g. `generic.mcb-b16`), then from the device
entry itself. Connections without a cable are short internal links (1 mΩ);
with a cable their resistance is `0.0178 Ω·mm²/m × length / cross-section`
(copper, 20 °C). `tools/validate_data.py` checks types, terminals, cables,
components and sockets (CTest `data_validation`).

## Physics

- **Steady state per time step** (10 ms), complex phasors at 50 Hz,
  modified nodal analysis with dense Gaussian elimination. All elements are
  linear; loads are resistive (`R = U² / P`).
- **Supply:** three sources 230 V at 0°, −120°, +120° behind half of the
  loop impedance Ze per conductor (L1-L3, N, PE), so L-N and L-PE loops both
  see Ze. **TN-S / TN-C-S:** PE connects to the transformer star point.
  **TT:** PE goes to the house earth electrode (`earth_electrode`, default 30 Ω),
  the star point to its own earth (`source_earth`, 1 Ω).
- Every node has 1 GΩ to earth, so open breakers and loose terminals stay
  solvable; it also limits insulation readings (two floating nodes read 500 MΩ).

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
| `fault <a> <b> [ohm]`, `open/close <conn>`, `clear` | faults |
| `plug <load> <socket>`, `unplug <load>` | appliances |
| `u`, `z`, `riso <a> <b>` | measurements |
| `wait <ms>` | let time pass (every change already lets 200 ms pass) |
| `expect <id> powered/unpowered/tripped/on/off`, `expect u/z <a> <b> <min> <max>` | checks for scripts |

With `--script` the exit code is 1 if an `expect` fails (CTest `electrical_demo`).

## Limits of the first version and next steps

- Resistive loads only; reactive loads (motors, power factor) and three-phase
  devices need only new element types, the solver already uses complex numbers.
- Simplified trip curves and no conductor heating.
- No connection to Unreal yet: the interface (gRPC or JSON/protobuf over TCP)
  is the next decision (ADR 0007). Unreal then shows what the simulator
  computes instead of deciding "powered" itself.
- Fault generator and rule checks for the electrician training build on the
  fault and measurement functions.
