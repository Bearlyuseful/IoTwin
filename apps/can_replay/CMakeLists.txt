add_executable(can_replay main_can_replay.c)
target_link_libraries(can_replay PRIVATE
    iotwin_bus_link
    iotwin_can
    iotwin_net
    iotwin_platform
    iotwin_logger
    iotwin_compile_options
)

install(TARGETS can_replay RUNTIME DESTINATION bin)
