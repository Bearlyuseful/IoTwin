# Scenarios and Faults

IoTwin can disturb the virtual bus on purpose and check how the nodes react,
like a hardware-in-the-loop test bench:

- **Fault injection of the medium** in `iotwin-bus`: lost frames, delay and
  jitter, a silent bus.
- **Test bench commands** to simulated nodes, e.g. a short circuit at the
  lamp output of the light ECU.
- **Recording and replay** in the can-utils `candump -L` format.
- **Scenario scripts** (`can_scenario`) that drive all of this and check the
  reactions, including UDS diagnostics. They run as automatic tests.

```bash
iotwin-bus
can_sim
can_scenario data/scenarios/light-demo/lamp-short-dtc.scn
```

```
RUN  Short circuit of the lamp output becomes DTC B1001-11
PASS Short circuit of the lamp output becomes DTC B1001-11 (12 steps, 124 ms)
```

## Scenario scripts

One step per line, `#` starts a comment (`200#01` inside a frame is not a
comment). Times are in milliseconds.

| Step | Meaning |
|------|---------|
| `name <text>` | name shown in the output (default: file name) |
| `send <frame>` | put a frame on the bus, e.g. `send 200#01` |
| `expect <pattern> [within <ms>]` | a matching frame must arrive (default 1000 ms) |
| `expect-not <pattern> for <ms>` | no matching frame may arrive |
| `wait <ms>` | let time pass |
| `bench <node> <command>` | test bench command to a simulated node, e.g. `bench ecu fault short on` |
| `bus <fault>` | fault of the bus: `drop`, `delay`, `silence`, `seed`, `clear` (see below) |
| `uds <target> <request> [expect <response>]` | UDS request to `ecu`, `gateway` or `TX:RX` (hex ids) |
| `log <text>` | print a note |

Before every action (`send`, `bench`, `bus`, `uds`) the runner discards what
has arrived so far, so an `expect` only sees frames that follow the action.

**Frame patterns:** `100#01` exact payload, `100#01*` payload starting with
`01`, `100#*` any payload, `100#XX01` `XX` = any byte, `100#R` remote frame,
8-digit ids for extended frames.

**UDS:** request and expected response are hex bytes (spaces allowed); a
trailing `*` allows more bytes. Without `expect` the step needs a positive
response. Example: `uds ecu 190209 expect 59027F9001112F` - exactly the DTC
`B1001-11` with status `2F`. Services and identifiers: [Diagnostics](diagnostics.md).

Exit code 0 when all scenarios passed, 1 if one failed, 2 for a syntax error.
A failure names the line and what was seen instead:

```
  FAIL line 1: expect 100#01 within 600 ms
       not seen within 600 ms (last frames: 100#00)
```

Single steps without a file: `can_scenario -e "bus drop 20" -e "bus delay 30 10"`.
`--verbose` prints every step and every frame.

## Bus faults

| `bus ...` | Effect |
|-----------|--------|
| `drop <percent> [<id>[/<mask>]]` | lose frames; optionally only matching ids (hex), e.g. `drop 100 200` drops every `0x200` |
| `delay <ms> [<jitter ms>]` | deliver late; frames never overtake each other (as on CAN) |
| `silence <ms>` | nothing gets through, e.g. a disturbed or short-circuited bus |
| `seed <n>` | start value of the random generator: the same seed loses the same frames |
| `clear` | remove all faults (frames still delayed keep their place) |

Faults affect every node, including the scenario runner. The bus logs who
injected what; `!status` (see [Bus Protocol](protocol.md#control-lines-and-test-bench))
reports the counters of dropped and delayed frames.

## Test bench commands

`bench <node> <command>` sends `@<node> <command>` over the bus. Only nodes
that registered for the test bench receive it, never the CAN nodes.
`can_sim` registers as `ecu` (`--name` changes it) and runs the command like
one typed on its console:

| Command | Effect |
|---------|--------|
| `fault short on/off` | lamp output short to ground: output switched off, DTC `B1001-11` |
| `fault open on/off` | open load: DTC `B1001-13` while the lamp is on |
| `fault supply on/off` | supply voltage low: DTC `B1002-16` |
| `fault clear` | remove all injected faults |
| `lamp on/off/toggle`, `blink <ms>/off` | operate the ECU directly |

## Recording and replay

```bash
can_monitor --log drive.log --quiet      # record everything on the bus
can_replay drive.log                     # play it back with the original timing
can_replay --speed 4 --loop --filter 100 drive.log
```

The log format is the one of `candump -L`, so logs from a real bus (can-utils
on Linux, `candump -L can0 > drive.log`) can be replayed onto iotwin-bus and
IoTwin logs can be played on a real bus with `canplayer`:

```
(1794606835.444395) iotwin 200#01
(1794606835.445622) iotwin 100#01
```

## Scenarios of the light demo

`data/scenarios/light-demo/` (automatic test `scenarios` in CTest):

| Scenario | Checks |
|----------|--------|
| `switch-controls-lamp` | `SWITCH_STATE` -> `LAMP_CMD` within 300 ms |
| `cyclic-lamp-cmd` | 500 ms cycle; cycle 0 via UDS = only on change |
| `diagnostics-identification` | VIN, sessions, write protection, blink via UDS, reset |
| `lamp-short-dtc` | injected short: output off, DTC failed and confirmed, cleared |
| `frame-loss` | lost `SWITCH_STATE`: the lamp does not react |
| `bus-delay` | 250 ms delay adds up in both directions |
| `bus-silence` | silent bus: nothing arrives, the ECU continues afterwards |

`tools/run_scenarios.py` starts a fresh `iotwin-bus` (free port) and `can_sim`
for every scenario, so faults, DTCs and settings never leak into the next one:

```bash
python tools/run_scenarios.py --bin build/gcc-debug/bin data/scenarios/light-demo
```

`data/scenarios/hardware/gateway-diagnostics.scn` checks the NUCLEO gateway
and needs the board with `serial_bridge`; it is not part of the automatic tests.

## Protocol details

The control and test bench lines extend the bus protocol without affecting
existing nodes: a node only receives them after it has sent a `!` command
itself or registered with `!testbench on`. See [Bus Protocol](protocol.md).
