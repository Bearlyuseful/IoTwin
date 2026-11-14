# ADR 0006: One Core, Three Products

**Date:** 2026-11-14
**Status:** Accepted

**Context:**
IoTwin started (autumn 2025) as an electrical training simulation of a
modular house: Unreal Engine shows the house and lets the user interact, a
separate simulator in C computes the electrical behaviour. Since then the
code grew mostly on the communication side (virtual CAN bus, NUCLEO gateway,
UDS, scenarios). The intended product line is:

1. **Smart home test platform** - the IoTwin digital twin of a home: with
   the real communication in a house, test faults and the behaviour of new
   devices before they are installed.
2. **Technical house renovation game** - a game in the style of house
   renovation games, focused on the technical and electrical side.
3. **Electrician training** - a deep electrical simulation in which a house
   gets randomly generated electrical problems; trainees find and repair them
   according to the standards. Focus on a rule engine and a fault generator.

**Decision:**
Build **one shared core** and three thin **product shells** on top of it.

| Shared core | Product specific |
|-------------|------------------|
| Electrical simulation (`libs/elsim`, `iotwin-sim`) | 1: home protocols (CAN today; MQTT, KNX, Matter later), device test runs |
| Engineering data: components, installations, systems (`data/`) | 2: gameplay, economy, progression |
| Bus, scenarios, fault injection, recording/replay | 3: fault generator, rule engine, assessment of the trainee |
| Unreal building blocks (ports, cables, devices, schematic view) | per product: Unreal project / game mode, packaging |

The core is data driven (installations and components as JSON) and free of
Unreal types. It is tested on its own; Unreal is a frontend.

**Order:** the electrical simulation core comes first, because all three
products need it and the original concept centres on it.

**Consequences:**

- New features are checked against all three products; reusable parts go
  into the core, product-only parts stay in the shell.
- The repository stays one repository for now (ADR 0005). Product shells can
  become separate Unreal projects or repositories once the core interfaces
  are stable.
