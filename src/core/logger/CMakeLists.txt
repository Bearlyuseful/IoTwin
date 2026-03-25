add_library(logger STATIC log.c log.h)
target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
