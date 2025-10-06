add_executable(test_can_sim test_can_sim.c)
target_include_directories(test_can_sim PRIVATE ${PROJECT_SOURCE_DIR}/src/tests/unity)
target_link_libraries(test_can_sim PRIVATE common can_sim)

enable_testing()
add_test(NAME test_can_sim COMMAND test_can_sim)