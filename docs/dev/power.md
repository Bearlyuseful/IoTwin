# Power and Dimming

The light ECU (`can_sim`) has a dimmable lamp output with an electrical model,
so current, voltage, a fuse and electrical faults behave like on a real
control unit. The NUCLEO gateway dims its LED with PWM accordingly.

```
supply 12.6 V, Ri 50 mOhm -- fuse 3 A slow -- smart high-side switch (PWM) -- 20 mOhm -- 21 W bulb -- GND
```

## Dimming

| Source | How |
|--------|-----|
| Visualizer | mouse wheel: `DIMMER_CMD` in 10 % steps |
| Any node, scenario | `send 201#32` (`DIMMER_CMD`, data[0] = 0..100 %) |
| `can_sim` console / test bench | `dim 50` |
| UDS | `uds_client session extended + write 0105 32` |

The ECU sends the brightness in `LAMP_CMD` byte 1 (`LampLevel`). The
visualizer's lamp and the gateway's LED (TIM2 PWM, 1 kHz) follow it. Senders
that only send byte 0 still work: receivers treat a missing byte as 100 %.

## Electrical model (`libs/power`)

`PowerPath` is a quasi-static model that is advanced every control step:

- **Supply:** open-circuit voltage and internal resistance; the terminal
  voltage drops with the current.
- **Fuse (3 A, slow blow):** I²t model. While the RMS current is above the
  rating, the excess `(I² - In²) * t` heats it up. It blows at 40 A²s and
  cools down at 5 A²s/s below the rating.
- **Smart high-side switch:** PWM, current sense, shutdown above 15 A and a
  retry every 100 ms (thermal restart). Switching the output off resets it.
- **Load:** resistive, `R = U² / P` (21 W at 12 V gives 6.86 Ω, about 1.82 A
  at 12.6 V).

With PWM the average current is `duty * I_on`, and the fuse sees the RMS
value `sqrt(duty) * I_on`.

## Faults and what the ECU sees

Faults change the circuit. The ECU's monitors only use measured values
(current sense, supply voltage), like real software:

| Fault (`fault ... on`) | Circuit | Reaction | DTC |
|-------------------------|---------|----------|-----|
| `short` | load resistance 0 | switch shuts down, retries every 100 ms | `B1001-11` |
| `open` | no load | no current although on | `B1001-13` |
| `overload` | bulb with 3x power (63 W, 5.3 A) | overcurrent after 200 ms; fuse blows after about 2 s, lamp dark | `B1001-19`, then `B1003-13` |
| `supply` | 9.8 V source | supply below 10.5 V | `B1002-16` |

A blown fuse stays blown until `fuse replace` (console or `bench ecu fuse replace`).
Removing the overload alone does not bring the lamp back.

## LAMP_STATUS (0x101)

Sent by the ECU every 200 ms, Intel byte order (see `data/dbc/iotwin.dbc`):

| Bytes | Signal | Unit |
|-------|--------|------|
| 0-1 | `LampCurrent` | mA (factor 0.001 A) |
| 2-3 | `SupplyVoltage` | mV (factor 0.001 V) |
| 4 | `OutputDuty` | % |
| 5 bit 0 | `FuseBlown` | |
| 5 bit 1 | `Overcurrent` (DTC test failed) | |
| 5 bit 2 | `ShortShutdown` | |

The visualizer HUD shows current and voltage from it, the schematic view
decodes it at the ECU node.

## Diagnostics

| DID | Content |
|-----|---------|
| `0104` | measured supply voltage, mV |
| `0105` | brightness 0..100 % (read/write, extended session) |
| `0106` | lamp current, mA |
| `0107` | fuse: blown (0/1), heat in % of the melting integral |
| `0204` (gateway) | LED brightness: commanded %, measured % |

The gateway measures its own PWM: it samples the LED pin's input register for
about 5 ms. This checks the real pin and not just the timer registers.
Verified on the NUCLEO: 100/50/25/10/0 % commanded gave the same values measured.

## Scenarios

`data/scenarios/light-demo/dimmer.scn` and `overload-fuse.scn` check brightness,
current, overcurrent and fuse automatically (CTest `scenarios`).
