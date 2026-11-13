# Portable CAN code. can_frame, can_bit_timing, slcan, isotp and uds are also
# compiled into the STM32 firmware (firmware/CMakeLists.txt); dbc is host only.
add_library(iotwin_can STATIC
    can_frame.c can_frame.h
    can_bus.c can_bus.h
    can_bit_timing.c can_bit_timing.h
    can_signal.c can_signal.h
    candump_log.c candump_log.h
    dbc.c dbc.h
    isotp.c isotp.h
    slcan.c slcan.h
    uds.c uds.h
)
target_include_directories(iotwin_can PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(iotwin_can PUBLIC iotwin_logger Threads::Threads PRIVATE iotwin_compile_options)
if(NOT WIN32)
    target_link_libraries(iotwin_can PUBLIC m)
endif()
