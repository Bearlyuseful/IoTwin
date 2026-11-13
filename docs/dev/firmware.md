# Firmware: NUCLEO-G0B1RE CAN Gateway

The board joins the virtual bus as a real node and turns the STM32's FDCAN
controller into an SLCAN adapter:

```
 iotwin-bus <-TCP-> serial_bridge <-USB (ST-LINK VCP, SLCAN)-> NUCLEO-G0B1RE <-FDCAN-> CAN bus
                                                                   |
                                                    B1 = wall switch, LD4 = lamp
```

- Every frame from the virtual bus is transmitted by the FDCAN controller, every
  frame received by the controller is reported to the virtual bus.
- The board is also a node of its own: the blue user button **B1** toggles a wall
  switch (`SWITCH_STATE`, id `0x200`), the green LED **LD4** shows the lamp
  command (`LAMP_CMD`, id `0x100`).
- Without a transceiver the controller runs in **internal loopback** mode: frames
  go through the real CAN peripheral (bit timing, message RAM, interrupts) and
  are received by the controller itself. These echoes are recognised and not
  reported a second time.

Design decisions: [ADR 0004](../adr/0004-firmware-in-mono-repo-bare-metal.md).

## Hardware

| Function | Pin | Notes |
|----------|-----|-------|
| System clock | - | HSI16 → PLL (M=1, N=8, R=2) = 64 MHz, PCLK 64 MHz |
| Virtual COM port | PA2 (TX) / PA3 (RX), USART2 AF1 | 115200 8N1 via the ST-LINK USB connection |
| FDCAN1 | PA12 (TX) / PA11 (RX), AF3 | morpho connector CN10; kernel clock = PCLK |
| User LED LD4 | PA5 | active high |
| User button B1 | PC13 | active low |

LED behaviour: **blinking** (1 Hz) = firmware running, channel closed (waiting
for the bridge); **steady** = channel open, LED follows the lamp command.

### Real CAN bus (normal mode)

Connect a 3.3 V CAN transceiver (e.g. SN65HVD230) to PA12 (TXD) and PA11
(RXD), terminate the bus with 120 Ω at both ends and add at least one other
node - a CAN frame is only acknowledged by another node. Start the bridge with
`--mode normal`. Check the pin positions on CN10 in the board user manual
(UM2324).

## Build

Requires `arm-none-eabi-gcc` (+ newlib), CMake and Ninja. On Windows with MSYS2:

```bash
pacman -S mingw-w64-ucrt-x86_64-arm-none-eabi-gcc mingw-w64-ucrt-x86_64-arm-none-eabi-newlib mingw-w64-ucrt-x86_64-openocd
```

```bash
cd firmware
cmake --preset nucleo-g0b1re
cmake --build --preset nucleo-g0b1re
```

Output in `firmware/build/nucleo-g0b1re/`: `iotwin_gateway.elf` (debugging),
`.bin` / `.hex` (flashing), `.map`. Memory use is printed after linking.

## Flash

```powershell
./tools/flash-firmware.ps1              # OpenOCD via ST-LINK, falls back to the drive
./tools/flash-firmware.ps1 -MassStorage # copy the .bin to the NOD_G0B1RE drive
```

Copying `iotwin_gateway.bin` to the `NOD_G0B1RE` USB drive works without any
debugger driver. Manually with OpenOCD:

```bash
openocd -f interface/stlink.cfg -f target/stm32g0x.cfg -c "program firmware/build/nucleo-g0b1re/iotwin_gateway.elf verify reset exit"
```

Debugging: `openocd -f interface/stlink.cfg -f target/stm32g0x.cfg` and in a
second terminal `gdb-multiarch firmware/build/nucleo-g0b1re/iotwin_gateway.elf`
→ `target extended-remote :3333`.

## Run

Find the COM port ("STMicroelectronics STLink Virtual COM Port" in the Device
Manager, `/dev/ttyACM0` on Linux), then:

```bash
iotwin-bus
can_sim
serial_bridge --serial COM5 --mode loopback --stats 10
```

or `./tools/run-dev.ps1 -SerialPort COM5`. Press B1: the bridge forwards
`200#01`, `can_sim` answers with `100#01`, LD4 turns on and the lamp in the
visualizer lights up.

## Serial protocol (SLCAN)

Commands end with CR; the board answers CR (ok) or BEL (error). Frames use the
same syntax in both directions.

| Command | Meaning | Answer |
|---------|---------|--------|
| `tIIIL<data>` | send standard frame, e.g. `t1002AA55` | `z` |
| `TIIIIIIIIL<data>` | send extended frame | `Z` |
| `rIIIL` / `RIIIIIIIIL` | send remote frame | `z` / `Z` |
| `S0` … `S8` | bitrate 10k, 20k, 50k, 100k, 125k, 250k, **500k** (default), 800k, 1M | CR |
| `O` | open, normal mode | CR |
| `L` | open, listen only (bus monitoring) | CR |
| `l` | open, internal loopback *(IoTwin extension)* | CR |
| `C` | close | CR |
| `V` / `N` | version / serial number | `V0100` / `NG0B1` |
| `F` | status flags (bit 0 RX queue full, 1 TX queue full, 2 error warning, 3 overrun, 5 error passive, 7 bus error) | `Fxx` |
| `Q` | statistics *(IoTwin extension)*: TX frames, RX frames, loopback echoes (8 hex each), TEC, REC, flags (2 hex each) | `Q…` |
| `Z0` / `Z1` | timestamps - accepted, not implemented | CR |

Any terminal works for experiments (115200 8N1, send CR): `V`, `S6`, `l`,
`t100101` → LD4 on.

## Diagnostics (UDS)

The firmware is a UDS server on `0x7E1 -> 0x7E9` (and functional `0x7DF`):
identification incl. the STM32 unique id, CAN error counters and frame
counters, uptime, DTC `U0073-88` for bus off, soft and hard ECUReset. The
same server code (`libs/can/uds.c`, `isotp.c`) is unit tested on the host.
Details: [Diagnostics (UDS)](diagnostics.md).

```bash
uds_client --target gateway scan
uds_client --target gateway read 0200 0201 0202
```

`serial_bridge` configures the adapter again when it rejects frames after a
reset of the board (UDS hard reset, reset button, power).

## Code structure

| Path | Content |
|------|---------|
| `firmware/board/nucleo_g0b1re/` | startup code with vector table (C), linker script, clock/GPIO/SysTick |
| `firmware/drivers/uart.c` | USART2 with interrupt driven RX/TX ring buffers |
| `firmware/drivers/fdcan.c` | FDCAN1: message RAM, bit timing, Tx FIFO, Rx FIFO 0 interrupt, bus-off recovery |
| `firmware/app/gateway.c` | hardware independent application (SLCAN, echo suppression, local node) |
| `firmware/app/main.c` | wiring, button debounce, main loop with `WFI` |
| `libs/can/` | `can_frame`, `slcan`, `can_bit_timing` - shared with the host |
| `firmware/vendor/` | CMSIS core 5.9.0 (ARM) and STM32G0 device headers (ST), Apache-2.0 |

Bit timing is calculated at run time from the kernel clock: 500 kbit/s at
64 MHz → prescaler 8, 16 tq (1 + 13 + 2), sample point 87.5 %, SJW 2.

## Tests without hardware

- `tests/test_gateway.cpp`: application logic against fake hardware.
- `tests/test_slcan.cpp`, `tests/test_can_bit_timing.cpp`: shared code.
- `tests/test_serial_bridge_e2e.cpp` (Linux): starts the real `serial_bridge`,
  connects it to a pseudo terminal on which the gateway logic runs with a
  simulated loopback controller, and checks the whole path bus ↔ bridge ↔ board.
