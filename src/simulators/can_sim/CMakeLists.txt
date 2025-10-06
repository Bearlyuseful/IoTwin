add_executable(can_sim main.c can_bus.c)
target_include_directories(can_sim PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(can_sim PRIVATE common)