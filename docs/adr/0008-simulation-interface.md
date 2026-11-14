# ADR 0008: Interface Between Simulation and Frontends

**Date:** 2026-11-14
**Status:** Accepted

**Context:**
ADR 0007 left open how Unreal talks to the electrical simulation. The options
were gRPC (original concept), JSON or protobuf over the existing TCP
transport, shared memory, a middleware such as DDS or Zenoh, or linking the
core into Unreal. The project has a real-time target (PREEMPT_RT, ADR 0003)
and will grow into three products (ADR 0006), from a single PC game to a test
platform with real devices and several clients. Performance and extensibility
weigh more than a quick first version: a slow foundation would have to be
replaced later.

Facts that decide it:

- Unreal is never real-time (Windows, frame based, garbage collection). Real
  time exists only in the simulator and on the bus, so the link must decouple
  the two: a simulation step must never wait for a frame.
- A whole house is about 1000 nodes and 3000 conductors; a complete state is
  roughly 64 KB, 6 MB/s at 100 Hz. Text (JSON) costs parsing and allocations
  every frame; binary data or no copy at all does not.
- gRPC has no C API for the C core, is heavy to build into Unreal
  (protobuf, abseil), does not run on a microcontroller and adds HTTP/2
  latency; streaming to many receivers has to be built on top.

**Decision:**

1. **Transport-independent session API in C** (`libs/elsim/el_session.h`):
   commands in (switch, dim, fault, reset, plug, ...), complete **snapshots**
   and **events** out. The core knows no transport.
2. **Real-time decoupling inside the session:** single-producer /
   single-consumer queues for commands and events and a triple buffer for
   snapshots, all lock-free and wait-free. The simulation thread does not
   allocate memory after the first step; the reader always gets the newest
   complete snapshot.
3. **Adapters, chosen per product:**
   - **In-process** (first, implemented): the core is compiled into the
     Unreal module and runs in its own thread with a fixed 10 ms step. For
     the game and the training product on one PC: no IPC cost, one install.
     For this the core is portable C99 without C99 complex numbers, so MSVC
     compiles it directly.
   - **Zenoh** (next): publish/subscribe with shared memory on one machine
     and the network between machines (simulator on a PREEMPT_RT board,
     several clients, later multi-user); zenoh-pico is a candidate for
     microcontrollers. DDS (Cyclone DDS) stays the alternative if hard
     distributed real-time guarantees become more important than easy
     networking.
   - **Text protocol** of `iotwin-bus` stays for debugging and candump tools.
4. **Message schema:** the session's command, snapshot and event structs are
   the schema. When the first out-of-process adapter is built they are
   written down once in **FlatBuffers** (readable without copying, small
   runtime in C and in Unreal), not before - a schema without a consumer
   would drift.
5. **Unreal owns the topology, the simulator every value:** the visualizer
   builds the model from its scene (ports become junctions, actors describe
   themselves through `IElectricalElement`) and rebuilds it when the wiring
   changes; it no longer decides "powered" itself.

**Consequences:**

- The C core compiles with GCC, Clang and MSVC (`-Wvla` guards against
  variable-length arrays); the Unreal build compiles it as C17.
- Rebuilding the model on a wiring change resets the electrical state
  (breakers, faults). Incremental topology changes are a later optimisation.
- The dense solver is fine for a flat (about 30 µs per step in the demo);
  whole houses need a sparse LU solver - the next performance step,
  independent of the interface.
- Supersedes the open point "Interface to Unreal" of ADR 0007.
