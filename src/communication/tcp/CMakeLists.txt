add_library(tcp STATIC socketClient.c socketClient.h)
target_include_directories(tcp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
