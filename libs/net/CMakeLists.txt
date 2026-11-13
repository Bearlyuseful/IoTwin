add_library(iotwin_net STATIC
    net.c net.h
    line_reader.c line_reader.h
)
target_include_directories(iotwin_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(iotwin_net PRIVATE iotwin_compile_options)
if(WIN32)
    target_link_libraries(iotwin_net PUBLIC ws2_32)
endif()

# Client side of iotwin-bus for tools (can_replay, can_scenario).
add_library(iotwin_bus_link STATIC bus_link.c bus_link.h)
target_include_directories(iotwin_bus_link PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(iotwin_bus_link PUBLIC iotwin_net iotwin_can iotwin_platform PRIVATE iotwin_compile_options)
