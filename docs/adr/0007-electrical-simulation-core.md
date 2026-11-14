# ADR 0007: Electrical Simulation Core

**Date:** 2026-11-14
**Status:** Accepted (interface to Unreal: decided in [ADR 0008](0008-simulation-interface.md))

**Context:**
The original concept puts the electrical behaviour of the house into a
separate simulator; Unreal shows the result. Today Unreal decides itself
whether a device is powered (a search through connected ports), and the C
core only models a single DC lamp output (`libs/power`). The three products
(ADR 0006) need real installation physics: 230/400 V AC, protective earth,
circuit breakers, residual current devices, faults and the measurements an
electrician makes.

**Decision:**

1. **The simulator owns every electrical result.** Unreal (or any other
   frontend) owns editing: placing devices, connecting conductors, operating
   switches. It sends these changes to the simulator and shows what the
   simulator reports. It never computes "powered" itself.
2. **Domain model** (one installation file per house, `data/installations/`):
   - **Device** with an id, a type and **terminals** (`lamp_living:L`, `:N`, `:PE`).
     Types: `supply`, `breaker`, `fuse`, `rcd`, `switch`, `load`, `socket`.
     Electrical parameters come from the component database (`component`),
     the installation may override them.
   - **Connection** = one conductor between two terminals, with its role
     (`L`, `N`, `PE`) and optionally the **cable** it belongs to.
   - **Cable** = type, cross-section, length (gives the conductor resistance).
   - Ids are stable strings, so frontends, saved games and faults can refer to them.
3. **Physics:** steady state per time step (quasi static, 10 ms by default)
   with complex phasors at 50 Hz, solved with modified nodal analysis. Linear
   elements only (resistive loads first; reactive loads later). Supply as
   three phases with source impedance and an earthing system (TN-S, TN-C-S, TT).
4. **Protection** is evaluated after each solution: circuit breakers
   (thermal and magnetic trip), fuses (I^2 t), RCDs (residual current). A trip
   changes the topology and the step is solved again.
5. **Measurements** use the same network: voltage between terminals, and
   impedance between terminals (loop impedance, continuity, insulation
   resistance) as the Thevenin impedance seen by the instrument.
6. **Faults** are changes to the network: an impedance between two terminals
   (short circuit, insulation fault, earth fault) or an open connection.

**Interface to Unreal:** the first version is a C library plus the
command line simulator `iotwin-sim`. The interface to Unreal and other
frontends is decided in [ADR 0008](0008-simulation-interface.md): a
transport-independent session API, first used in-process in Unreal.

**Consequences:**

- `libs/elsim` is tested without Unreal; the installation files are
  validated in CI like the other engineering data.
- Unreal's own "powered" search is replaced by the simulator (ADR 0008).
- `libs/power` (the light ECU's DC output) stays as a small special case
  until the ECU itself is placed into an installation.
