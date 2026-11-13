# Linux only (SocketCAN).
add_library(iotwin_socketcan STATIC socketcan.c socketcan.h)
target_include_directories(iotwin_socketcan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(iotwin_socketcan PUBLIC iotwin_can PRIVATE iotwin_compile_options)

add_executable(socketcan_bridge main_socketcan_bridge.c)
target_link_libraries(socketcan_bridge PRIVATE
    iotwin_socketcan
    iotwin_net
    iotwin_platform
    iotwin_logger
    iotwin_compile_options
)

install(TARGETS socketcan_bridge RUNTIME DESTINATION bin)
