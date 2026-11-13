add_executable(can_monitor main_can_monitor.c)
target_link_libraries(can_monitor PRIVATE
    iotwin_can
    iotwin_net
    iotwin_platform
    iotwin_logger
    iotwin_compile_options
)

install(TARGETS can_monitor RUNTIME DESTINATION bin)
