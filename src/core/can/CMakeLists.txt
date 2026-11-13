# Portable CAN code: also compiled into the STM32 firmware (firmware/CMakeLists.txt).
add_library(iotwin_can STATIC
    can_frame.c can_frame.h
    can_bus.c can_bus.h
    can_bit_timing.c can_bit_timing.h
    slcan.c slcan.h
)
target_include_directories(iotwin_can PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(iotwin_can PUBLIC iotwin_logger Threads::Threads PRIVATE iotwin_compile_options)
