add_executable(uds_client main_uds_client.c)
target_link_libraries(uds_client PRIVATE
    iotwin_can
    iotwin_net
    iotwin_platform
    iotwin_logger
    Threads::Threads
    iotwin_compile_options
)

install(TARGETS uds_client RUNTIME DESTINATION bin)
