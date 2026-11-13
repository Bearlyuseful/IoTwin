add_library(iotwin_bridge_core STATIC bridge_core.c bridge_core.h)
target_include_directories(iotwin_bridge_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(iotwin_bridge_core PUBLIC iotwin_can PRIVATE iotwin_compile_options)

add_executable(serial_bridge main_serial_bridge.c)
target_link_libraries(serial_bridge PRIVATE
    iotwin_bridge_core
    iotwin_serial
    iotwin_net
    iotwin_platform
    iotwin_logger
    iotwin_compile_options
)

install(TARGETS serial_bridge RUNTIME DESTINATION bin)
