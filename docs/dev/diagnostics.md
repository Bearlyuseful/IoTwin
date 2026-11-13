# Diagnostics (UDS)

The simulated light ECU (`can_sim`) and the NUCLEO-G0B1RE gateway firmware
are diagnosable over the bus with **UDS** (ISO 14229-1) on top of **ISO-TP**
(ISO 15765-2), like a real vehicle ECU. `uds_client` is the tester.

```
uds_client ── iotwin-bus ──┬── can_sim            0x7E0 -> 0x7E8
                           └── serial_bridge ── NUCLEO gateway   0x7E1 -> 0x7E9
functional requests (all ECUs): 0x7DF
```

## Quick start

```bash
iotwin-bus
can_sim
uds_client scan                       # identification of the light ECU
uds_client dtc                        # trouble codes (failed or confirmed)
uds_client session extended + write 0103 012C + read 0103   # blink every 300 ms
uds_client --target gateway read 0200 0201 0202             # NUCLEO live data
uds_client                            # interactive, keeps the session alive
```

`uds_client --log-level debug ...` shows every CAN frame. In the visualizer's
schematic view (TAB) the tester is a node of its own and every diagnostic
frame is decoded (`DIAG_ECU_REQ SF ReadDataByIdentifier F190`).

## Services

The server (`libs/can/uds.c`) is portable C without heap use, the same code
runs in `can_sim` and on the STM32.

| SID | Service                    | Supported                                              |
|-----|----------------------------|--------------------------------------------------------|
| 10  | DiagnosticSessionControl   | 01 default, 03 extended (P2 50 ms, P2* 5 s)            |
| 11  | ECUReset                   | 01 hard, 03 soft                                       |
| 14  | ClearDiagnosticInformation | FFFFFF (all) or a single DTC                           |
| 19  | ReadDTCInformation         | 01 count by status mask, 02 by status mask, 0A all     |
| 22  | ReadDataByIdentifier       | up to 8 identifiers per request                        |
| 2E  | WriteDataByIdentifier      | extended session only                                  |
| 3E  | TesterPresent              | 00                                                     |

- `suppressPosRspMsgIndicationBit` (sub-function | 0x80) for 10, 11 and 3E.
- Functional requests (0x7DF) are single frames; NRCs 11, 12, 31, 7E and 7F
  are not sent for them, as the standard requires.
- S3 timeout 5 s: without requests the server returns to the default
  session. The interactive tester sends `3E 80` every 2 s.
- Negative responses: `7F <SID> <NRC>`, e.g. `7F 2E 7F` (write in the
  default session), `7F 22 31` (unknown identifier).

## Light ECU (`can_sim`, 0x7E0 -> 0x7E8)

| DID  | Content                         | Format         | Access     |
|------|---------------------------------|----------------|------------|
| F186 | active diagnostic session       | 1 byte         | read       |
| F187 | spare part number               | ASCII          | read       |
| F189 | software version                | ASCII          | read       |
| F18C | serial number                   | ASCII          | read       |
| F190 | VIN                             | ASCII, 17      | read       |
| 0100 | lamp output (after protection)  | 1 byte         | read       |
| 0101 | switch input (last SWITCH_STATE)| 1 byte         | read       |
| 0102 | LAMP_CMD cycle time             | uint16 ms, 0 = off | read / write (0 or 50..60000) |
| 0103 | blink period                    | uint16 ms, 0 = off | read / write (0 or 50..60000) |
| 0104 | supply voltage (simulated)      | uint16 mV      | read       |

### Faults and trouble codes

Faults are injected on the `can_sim` console; monitors turn them into DTCs
the way a real ECU does:

| Console             | Effect                                         | DTC        | Detected            |
|---------------------|------------------------------------------------|------------|---------------------|
| `fault short on`    | output switched off (short circuit protection) | `B1001-11` | always              |
| `fault open on`     | none visible (lamp would stay dark)            | `B1001-13` | only while the lamp is on |
| `fault supply on`   | DID 0104 drops to 9800 mV                      | `B1002-16` | always              |

`dtc` on the console shows the ECU's own view. A removed fault leaves the
DTC confirmed until it is cleared (`uds_client clear`). An ECUReset keeps the
DTC memory and starts a new operation cycle, like a real ECU.

Status byte (ISO 14229-1 annex D): bit 0 testFailed, 1 testFailedThisOperationCycle,
2 pending, 3 confirmed, 4 testNotCompletedSinceLastClear, 5 testFailedSinceLastClear,
6 testNotCompletedThisOperationCycle. A failed test gives `2F`, a cleared DTC `50`.

## Gateway (NUCLEO-G0B1RE firmware, 0x7E1 -> 0x7E9)

Requests reach the gateway from the PC (iotwin-bus -> `serial_bridge` ->
SLCAN) or from a tester on a real CAN bus; responses go to both sides.

| DID  | Content                                                 |
|------|---------------------------------------------------------|
| F187 | spare part number `NUCLEO-G0B1RE-GW`                    |
| F189 | firmware version                                        |
| F18C | 96-bit unique device id of the STM32 (12 bytes)         |
| 0100 | last LAMP_CMD (LED)                                     |
| 0101 | wall switch (button B1)                                 |
| 0200 | TEC, REC, state (0 active, 1 warning, 2 passive, 3 bus off) |
| 0201 | uint32 frames to CAN, from CAN, loopback echoes          |
| 0202 | uint32 uptime in ms                                     |
| 0203 | channel open, mode (0 normal, 1 loopback, 2 listen), uint32 bitrate |

DTC `U0073-88` (communication bus off) is monitored while the channel is
open and not in listen mode.

ECUReset `hard` restarts the MCU 50 ms after the response (so it can leave
UART and CAN); `soft` resets the counters and keeps the channel to the host
open. After a hard reset or a press of the reset button the board's SLCAN
channel is closed; `serial_bridge` notices the rejected frames and configures
the adapter again within about a second.

Verified on the hardware: identification incl. unique id, live data, DTC
status, soft and hard reset with automatic recovery of the bridge, functional
TesterPresent answered by both nodes.

## Addresses and DBC

The diagnostic ids are part of `data/dbc/iotwin.dbc` (`DIAG_ECU_REQ`,
`DIAG_ECU_RESP`, `DIAG_GW_REQ`, `DIAG_GW_RESP`, `DIAG_FUNCTIONAL_REQ`, no
signals). In `data/systems/light-demo.json` these messages carry
`"protocol": "uds"`, which tells tools such as the visualizer to decode
ISO-TP and UDS instead of signals.

## Not implemented (yet)

SecurityAccess (27), RoutineControl (31), communication control (28),
download (34/36/37), DTC snapshots and extended data, response pending (78)
on the server side. ISO-TP with CAN FD and extended addressing.
