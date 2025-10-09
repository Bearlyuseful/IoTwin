add_library(can_sim can_bus.c)
target_include_directories(can_sim PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(can_sim PRIVATE common)