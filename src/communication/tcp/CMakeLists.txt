add_library(tcp STATIC socketClient.c socketClient.h)
target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
