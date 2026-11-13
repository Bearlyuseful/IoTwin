add_library(iotwin_bus_server STATIC bus_server.c bus_server.h)
target_include_directories(iotwin_bus_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(iotwin_bus_server
    PUBLIC iotwin_can iotwin_net iotwin_logger
    PRIVATE iotwin_compile_options
)

add_executable(iotwin-bus main_bus.c)
target_link_libraries(iotwin-bus PRIVATE iotwin_bus_server iotwin_platform iotwin_compile_options)

install(TARGETS iotwin-bus RUNTIME DESTINATION bin)
