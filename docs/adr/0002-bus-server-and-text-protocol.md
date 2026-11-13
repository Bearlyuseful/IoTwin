# ADR 0002: Virtual CAN Bus Server and Text Line Protocol

**Date:** 2026-11-13
**Status:** Accepted (replaces the earlier gRPC idea)

**Context:**
Simulated ECUs (C) and the Unreal Engine visualizer must exchange CAN frames.
The first prototype had a simulator acting as TCP client without any server.
We need to decide who owns the bus and how frames are encoded on TCP.

**Decision:**

1. A dedicated C program, `iotwin-bus`, is the TCP server and acts as the bus.
   Every frame a client sends is validated, normalised and forwarded to all
   other clients - the broadcast semantics of a physical CAN bus.
   Simulators and the visualizer are equal clients ("nodes").
2. Frames are encoded as text lines in the compact `cansend`/`candump`
   format (`123#DEADBEEF\n`), see [protocol](../dev/protocol.md).

**Alternatives considered:**

- *Visualizer as server:* fewer processes, but every test would need Unreal
  and only one simulator could connect.
- *Binary frames (16 byte, SocketCAN layout):* more compact, but harder to
  debug and to parse in Unreal; bandwidth is irrelevant at CAN data rates.
- *gRPC:* heavy dependency for a stream of 25-byte messages, no benefit for
  a broadcast bus.

**Consequences:**

- The bus can be tested headless in CI (integration tests open real sockets).
- Any number of simulators, the visualizer and tools like `netcat` can join.
- Traffic is human readable; a later bridge to Linux SocketCAN/`vcan` can
  reuse the can-utils syntax.
- The bus adds one TCP hop of latency. Hard real-time timing between ECUs
  belongs inside the core (see ADR 0003), not on the TCP link to Unreal.
