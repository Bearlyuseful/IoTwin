# Vendored headers

Unmodified copies, only the files the firmware needs.

| Directory | Source | Version | License |
|-----------|--------|---------|---------|
| `cmsis_core` | [ARM-software/CMSIS_5](https://github.com/ARM-software/CMSIS_5) `CMSIS/Core/Include` | tag `5.9.0` | Apache-2.0 (`LICENSE.txt`) |
| `cmsis_device_g0` | [STMicroelectronics/cmsis-device-g0](https://github.com/STMicroelectronics/cmsis-device-g0) `Include` | commit `7c77b7c` (2026-10-09) | Apache-2.0 (`LICENSE.md`) |

`system_stm32g0xx.h` is only used for its declarations; `SystemInit()` and
`SystemCoreClock` are implemented in `firmware/board/nucleo_g0b1re/board.c`.
