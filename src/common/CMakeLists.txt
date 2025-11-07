add_library(common STATIC log.c log.h socketClient.c socketClient.h)
target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
