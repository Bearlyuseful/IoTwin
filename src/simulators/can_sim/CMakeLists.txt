add_library(can_sim STATIC can_bus.c can_bus.h canSimulator.c canSimulator.h)
target_include_directories(can_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(can_sim_app main_can_sim.c)

target_link_libraries(can_sim_app PRIVATE can_sim common)
