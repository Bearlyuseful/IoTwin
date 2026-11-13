add_library(iotwin_logger STATIC log.c log.h)
target_include_directories(iotwin_logger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(iotwin_logger PUBLIC Threads::Threads PRIVATE iotwin_compile_options)
