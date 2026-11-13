cmake_minimum_required(VERSION 3.22)

# Bare-metal firmware for the NUCLEO-G0B1RE. Separate CMake project with its own
# toolchain (see ADR 0004); configure with: cmake --preset nucleo-g0b1re
project(IoTwinFirmware VERSION 0.2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(BOARD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/board/nucleo_g0b1re)

option(IOTWIN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_executable(iotwin_gateway
    ${BOARD_DIR}/startup.c
    ${BOARD_DIR}/board.c
    drivers/uart.c
    drivers/fdcan.c
    app/gateway.c
    app/main.c
    # portable code shared with the host (unit tested there)
    ${REPO_ROOT}/libs/can/can_frame.c
    ${REPO_ROOT}/libs/can/can_bit_timing.c
    ${REPO_ROOT}/libs/can/slcan.c
)
set_target_properties(iotwin_gateway PROPERTIES SUFFIX ".elf")

target_include_directories(iotwin_gateway PRIVATE
    ${BOARD_DIR}
    drivers
    app
    ${REPO_ROOT}/libs/can
    vendor/cmsis_core
    vendor/cmsis_device_g0
)
target_compile_definitions(iotwin_gateway PRIVATE STM32G0B1xx)

set(MCU_FLAGS -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft)
target_compile_options(iotwin_gateway PRIVATE
    ${MCU_FLAGS}
    -Os -g3
    -ffunction-sections -fdata-sections -fno-common
    -Wall -Wextra
    $<$<BOOL:${IOTWIN_WARNINGS_AS_ERRORS}>:-Werror>
)
target_link_options(iotwin_gateway PRIVATE
    ${MCU_FLAGS}
    -T${BOARD_DIR}/linker.ld
    # own startup code (startup.c) instead of crt0; libc only provides mem*/str*
    -nostartfiles --specs=nano.specs
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/iotwin_gateway.map
    -Wl,--print-memory-usage
    -Wl,--no-warn-rwx-segments
)
set_target_properties(iotwin_gateway PROPERTIES LINK_DEPENDS ${BOARD_DIR}/linker.ld)

add_custom_command(TARGET iotwin_gateway POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:iotwin_gateway> ${CMAKE_CURRENT_BINARY_DIR}/iotwin_gateway.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:iotwin_gateway> ${CMAKE_CURRENT_BINARY_DIR}/iotwin_gateway.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:iotwin_gateway>
    COMMENT "Creating iotwin_gateway.bin / .hex"
)
