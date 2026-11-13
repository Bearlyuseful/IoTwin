# Electrical models of simulated devices (host only).
add_library(iotwin_power STATIC power_path.c power_path.h)
target_include_directories(iotwin_power PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(iotwin_power PRIVATE iotwin_compile_options)
