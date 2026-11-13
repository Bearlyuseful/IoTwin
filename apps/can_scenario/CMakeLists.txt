# Scenario parser (unit tested) and runner.
add_library(iotwin_scenario STATIC scenario.c scenario.h)
target_include_directories(iotwin_scenario PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(iotwin_scenario PUBLIC iotwin_can PRIVATE iotwin_compile_options)

add_executable(can_scenario main_can_scenario.c)
target_link_libraries(can_scenario PRIVATE
    iotwin_scenario
    iotwin_bus_link
    iotwin_can
    iotwin_net
    iotwin_platform
    iotwin_logger
    iotwin_compile_options
)

install(TARGETS can_scenario RUNTIME DESTINATION bin)
