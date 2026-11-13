add_library(logger STATIC log.c log.h)
target_include_directories(logger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
