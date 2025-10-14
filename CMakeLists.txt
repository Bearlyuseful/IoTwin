cmake_minimum_required(VERSION 3.22)
project(IoTwin LANGUAGES C CXX)

if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "C Compiler is not GCC! Detected: ${CMAKE_C_COMPILER_ID}")
endif()

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "C++ Compiler is not G++! Detected: ${CMAKE_CXX_COMPILER_ID}")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

# Enable testing globally
include(CTest)

add_subdirectory(src)
add_subdirectory(tests)
