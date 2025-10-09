# ---- Catch2 setup ----
include(FetchContent)
# ---- Catch2 setup ----
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(catch2)

# ---- Test Executable ----
add_executable(test_can_sim
    test_can_sim.cpp
)

# include headers from C modules
target_include_directories(test_can_sim PRIVATE
    ${PROJECT_SOURCE_DIR}/src/common
    ${PROJECT_SOURCE_DIR}/src/simulators/can_sim
)

# link with C libraries
target_link_libraries(test_can_sim PRIVATE
    can_sim
    common
    Catch2::Catch2WithMain
)

# register with CTest
include(Catch)
catch_discover_tests(test_can_sim)