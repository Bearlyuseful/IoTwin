# ---- Catch2 setup ----
include(FetchContent)
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(catch2)

# Firmware application logic is hardware independent and tested on the host.
add_library(iotwin_gateway_logic STATIC ${PROJECT_SOURCE_DIR}/firmware/app/gateway.c)
target_include_directories(iotwin_gateway_logic PUBLIC ${PROJECT_SOURCE_DIR}/firmware/app)
target_link_libraries(iotwin_gateway_logic PUBLIC iotwin_can PRIVATE iotwin_compile_options)

# ---- Test Executable ----
add_executable(iotwin_tests
    test_can_bus.cpp
    test_can_frame.cpp
    test_can_bit_timing.cpp
    test_can_signal.cpp
    test_dbc.cpp
    test_isotp.cpp
    test_uds.cpp
    test_scenario.cpp
    test_slcan.cpp
    test_line_reader.cpp
    test_light_ecu.cpp
    test_light_ecu_diag.cpp
    test_power_path.cpp
    test_cycle_timer.cpp
    test_bus_server.cpp
    test_gateway.cpp
    test_bridge_core.cpp
    test_serial_port.cpp
    test_serial_bridge_e2e.cpp
    test_socketcan.cpp
)

target_link_libraries(iotwin_tests PRIVATE
    iotwin_bus_server
    iotwin_light_ecu
    iotwin_scenario
    iotwin_gateway_logic
    iotwin_bridge_core
    iotwin_serial
    iotwin_can
    iotwin_net
    iotwin_platform
    iotwin_logger
    Threads::Threads
    Catch2::Catch2WithMain
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(iotwin_tests PRIVATE util iotwin_socketcan) # openpty, SocketCAN
    add_dependencies(iotwin_tests socketcan_bridge)
    target_compile_definitions(iotwin_tests PRIVATE SOCKETCAN_BRIDGE_PATH="$<TARGET_FILE:socketcan_bridge>")
endif()
# The end-to-end test starts the real serial_bridge executable.
add_dependencies(iotwin_tests serial_bridge iotwin-bus can_sim can_scenario)
target_compile_definitions(iotwin_tests PRIVATE
    SERIAL_BRIDGE_PATH="$<TARGET_FILE:serial_bridge>"
    IOTWIN_DBC_PATH="${PROJECT_SOURCE_DIR}/data/dbc/iotwin.dbc"
)

# Engineering data (data/components, data/systems) must stay consistent.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME data_validation
             COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/validate_data.py ${PROJECT_SOURCE_DIR}/data)
    # Scenario scripts against a real iotwin-bus and can_sim (fresh processes per scenario).
    add_test(NAME scenarios
             COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/run_scenarios.py
                     --bin $<TARGET_FILE_DIR:iotwin-bus> ${PROJECT_SOURCE_DIR}/data/scenarios/light-demo)
    set_tests_properties(scenarios PROPERTIES TIMEOUT 180)
else()
    message(STATUS "Python 3 not found - data validation test disabled")
endif()

# register with CTest
include(Catch)
catch_discover_tests(iotwin_tests PROPERTIES SKIP_RETURN_CODE 4) # 4 = Catch2 SKIP
