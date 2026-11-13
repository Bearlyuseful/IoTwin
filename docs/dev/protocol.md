# Bus Protocol

All nodes connect to `iotwin-bus` via TCP (default `127.0.0.1:8080`) and
exchange CAN frames as text, **one frame per line** terminated by `\n`
(`\r\n` is accepted). The server forwards every valid frame to all other
connected nodes and drops invalid lines.

## Frame syntax

The syntax is the compact format of the Linux can-utils (`cansend`, `candump`):

| Example        | Meaning                                                      |
|----------------|--------------------------------------------------------------|
| `123#DEADBEEF` | Standard frame, 11-bit id (3 hex digits, max `7FF`), 4 bytes |
| `1F334455#11`  | Extended frame, 29-bit id (8 hex digits, max `1FFFFFFF`)     |
| `123#`         | Data frame without payload (DLC 0)                           |
| `123#R`        | Remote transmission request, `123#R4` with DLC 4             |
| `123#DE.AD.BE` | Optional `.` between bytes (input only)                      |

- Hex digits are case-insensitive on input; the bus always sends upper case
  without separators (`1ab#de.ad` arrives as `1AB#DEAD`).
- At most 8 data bytes (classic CAN). CAN FD (`##`) is not supported.
- Lines longer than 255 characters are discarded.

## Signals of the light demo

| Id    | Name           | Sender               | Payload                               |
|-------|----------------|----------------------|---------------------------------------|
| `100` | `LAMP_CMD`     | `can_sim` (light ECU)| `data[0]` bit 0: 1 = relay closed, lamp on |
| `200` | `SWITCH_STATE` | Visualizer wall switch | `data[0]` bit 0: 1 = switch on       |

The ECU sends `LAMP_CMD` on every change and cyclically (default every
500 ms), so a node that connects later learns the state within one cycle.

## Serial side

Microcontroller nodes are attached through `serial_bridge`, which speaks
**SLCAN** (Lawicel) on the serial link and translates every frame 1:1 into the
bus format above, e.g. `t1002AA55` ↔ `100#AA55`. The commands are listed in
[Firmware](firmware.md#serial-protocol-slcan).

## Trying it by hand

```bash
iotwin-bus --log-frames
can_sim
# in a third terminal (any TCP client works):
nc 127.0.0.1 8080
200#01        <- type this: switch on, the ECU answers with 100#01
```

On Windows without `nc`, use `ncat` (Nmap) or PowerShell, or simply type
`send 200#01` into a second `can_sim` console.
