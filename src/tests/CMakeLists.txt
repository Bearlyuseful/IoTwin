add_executable(test_can test_can.c)
target_link_libraries(test_can PRIVATE common can_sim)

add_test(NAME test_can COMMAND test_can)