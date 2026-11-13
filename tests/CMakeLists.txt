# ---- Catch2 setup ----
include(FetchContent)
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(catch2)

# ---- Test Executable ----
add_executable(iotwin_tests
    test_can_bus.cpp
    test_can_frame.cpp
    test_line_reader.cpp
    test_light_ecu.cpp
    test_cycle_timer.cpp
    test_bus_server.cpp
)

target_link_libraries(iotwin_tests PRIVATE
    iotwin_bus_server
    iotwin_light_ecu
    iotwin_can
    iotwin_net
    iotwin_platform
    iotwin_logger
    Threads::Threads
    Catch2::Catch2WithMain
)

# register with CTest
include(Catch)
catch_discover_tests(iotwin_tests)
