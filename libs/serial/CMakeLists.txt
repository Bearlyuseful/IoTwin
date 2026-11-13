add_library(iotwin_serial STATIC serial_port.c serial_port.h)
target_include_directories(iotwin_serial PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(iotwin_serial PRIVATE iotwin_compile_options)
