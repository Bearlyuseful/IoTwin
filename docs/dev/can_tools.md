# CAN Tools: DBC, ISO-TP, SocketCAN

## DBC and `can_monitor`

`data/dbc/iotwin.dbc` describes the signals of the light demo:

| Id | Message | Signal | Values |
|----|---------|--------|--------|
| `0x100` | `LAMP_CMD` (LightECU) | `LampOn` bit 0 | 0 Off, 1 On |
| `0x200` | `SWITCH_STATE` (WallSwitch) | `SwitchOn` bit 0 | 0 Off, 1 On |

`can_monitor` shows the bus traffic decoded with a DBC file:

```bash
can_monitor --dbc data/dbc/iotwin.dbc --changes
```

```
17:51:00.270  100#01                     LAMP_CMD LampOn=1 (On)
17:51:03.689  200#01                     SWITCH_STATE SwitchOn=1 (On)
17:51:03.689  7FF#1234                   (unknown id)
```

`--changes` hides cyclic repetitions with unchanged payload.

### Library (`libs/can`)

- `can_signal.h` – signal codec, portable and heap-free (usable in firmware):
  Intel (little endian) and Motorola (big endian, DBC sawtooth numbering)
  layouts, signed values, factor/offset, range checks when encoding.
- `dbc.h` – reader for `BO_`, `SG_` (incl. multiplexer `M` / `mN`), `VAL_`;
  extended ids via bit 31; skips everything else, also multi-line comments.
  Errors are reported with line numbers. `dbc_describe_frame()` and
  `dbc_set_signal()` decode and build frames by name.

## ISO-TP (ISO 15765-2)

`libs/can/isotp.h` transports messages of up to 4095 bytes over classic
CAN frames (normal addressing), e.g. for UDS diagnostics:

| Frame | PCI | Content |
|-------|-----|---------|
| Single | `0L` | up to 7 data bytes |
| First | `1L LL` | 12-bit length, 6 data bytes |
| Consecutive | `2N` | sequence number 1..15, 0, ...; 7 data bytes |
| Flow control | `3S BS ST` | CTS / WAIT / OVFLW, block size, STmin |

Implemented: flow control with block size and STmin (ms and 100..900 µs
encodings), WAIT with WFTmax limit, overflow reporting, N_Bs and N_Cr
timeouts, sequence checks, padding (default 0xCC), retry when the CAN driver
queue is full. The link is driven by `isotp_on_frame()` and `isotp_poll(now)`,
so it runs on the host and on a microcontroller alike.

```c
IsoTpConfig config;
isotp_default_config(&config, 0x7E0 /* tx */, 0x7E8 /* rx */);
config.send = my_can_send;
isotp_init(&link, &config, rx_buffer, sizeof(rx_buffer));

isotp_send(&link, request, length, now_us);
/* for every received frame: */ isotp_on_frame(&link, &frame, now_us);
/* periodically:             */ isotp_poll(&link, now_us);
if (isotp_receive(&link, &length)) { /* rx_buffer holds a complete message */ }
```

Tests (`tests/test_isotp.cpp`) run two links against each other on a simulated
bus with simulated time.

## Recording and replay

`can_monitor --log FILE` records every frame in the `candump -L` format,
`can_replay FILE` plays it back with the original timing (`--speed`, `--loop`,
`--filter`). Logs of a real bus recorded with can-utils work too. Details and
scripted scenarios with fault injection: [Scenarios and Faults](scenarios.md).

## UDS diagnostics

The UDS server (`libs/can/uds.c`) and the tester `uds_client` build on this
ISO-TP layer; services, identifiers and trouble codes of the light ECU and
the gateway are described in [Diagnostics (UDS)](diagnostics.md).

```bash
uds_client scan
uds_client --target gateway read 0200
```

## SocketCAN bridge (Linux)

`socketcan_bridge` connects a Linux CAN interface to iotwin-bus. With it the
standard can-utils, USB-CAN adapters or a real CAN bus join the virtual bus:

```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

iotwin-bus &
socketcan_bridge --interface vcan0 &
can_sim &
candump vcan0              # shows 100#00 / 100#01 from the light ECU
cansend vcan0 200#01       # switches the lamp
```

The live test in `tests/test_socketcan.cpp` runs when `vcan0` exists and is
skipped otherwise; CI tries to create it.
