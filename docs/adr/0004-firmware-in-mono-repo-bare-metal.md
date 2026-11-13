# ADR 0004: Firmware in the Mono-Repo, Bare-Metal on CMSIS, SLCAN on the Serial Link

**Date:** 2026-11-13
**Status:** Accepted

**Context:**
Phases 2 and 3 of the roadmap bring a microcontroller (NUCLEO-G0B1RE,
STM32G0B1RE with FDCAN) into the system: it joins the virtual bus over a
serial link and acts as a real CAN controller. We need to decide where the
firmware lives, how it is built, which abstraction level it uses and which
protocol runs on the serial link.

**Decision:**

1. **Mono-repo:** the firmware lives in `firmware/` as a separate CMake project
   with its own toolchain file (`arm-none-eabi-gcc`). The host build is not
   affected; CI builds both.
2. **Bare-metal on CMSIS:** only the CMSIS core and device headers from ARM and
   ST are vendored (`firmware/vendor`). Startup code, linker script, clock, UART
   and FDCAN drivers are written against the registers - no HAL, no RTOS.
3. **Portable core code is shared and host tested:** `can_frame`, `slcan` and
   `can_bit_timing` (in `libs/can`) and the application logic
   (`firmware/app/gateway.c`, hardware access only through function pointers)
   compile for the PC and the MCU and are unit tested on the host.
4. **SLCAN (Lawicel) on the serial link**, with two documented extensions:
   `l` (open in internal loopback mode) and `Q` (statistics).

**Alternatives considered:**

- *Separate firmware repository:* cleaner boundaries, but shared code (frame
  model, codec) would need versioning across repositories.
- *STM32 HAL / CubeMX:* faster start, but large generated code and less
  insight into the peripheral; the FDCAN register interface is small enough.
- *Own binary or candump-text protocol on the serial link:* SLCAN is the
  de-facto standard of serial CAN adapters (python-can, SavvyCAN, CANable), so
  the board is also usable as a generic USB-CAN adapter.

**Consequences:**

- The firmware is small (~6.5 KB flash) and every register access is visible.
- Hardware independent logic is tested without the board; the hardware path is
  tested end to end on Linux with a pseudo terminal and the real `serial_bridge`.
- Supporting another MCU means a new `board/` directory plus drivers; the
  application and the shared code stay unchanged.
