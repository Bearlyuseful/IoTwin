cmake_minimum_required(VERSION 3.22)
project(IoTwin VERSION 0.4.0 LANGUAGES C CXX)

# GCC is the reference toolchain: natively on Linux (incl. PREEMPT_RT targets)
# and MSYS2 / MinGW-w64 on Windows (best effort, see docs/adr/0003).
if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "C Compiler is not GCC! Detected: ${CMAKE_C_COMPILER_ID}")
endif()

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "C++ Compiler is not G++! Detected: ${CMAKE_CXX_COMPILER_ID}")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(IOTWIN_WARNINGS_AS_ERRORS "Treat compiler warnings in IoTwin code as errors" OFF)

find_package(Threads REQUIRED)

# Compile settings shared by all IoTwin targets (linked PRIVATE).
add_library(iotwin_compile_options INTERFACE)
target_compile_options(iotwin_compile_options INTERFACE
    -Wall -Wextra
    $<$<BOOL:${IOTWIN_WARNINGS_AS_ERRORS}>:-Werror>
)
if(WIN32)
    target_compile_definitions(iotwin_compile_options INTERFACE
        _WIN32_WINNT=0x0A00
        WIN32_LEAN_AND_MEAN
        __USE_MINGW_ANSI_STDIO=1
    )
    # Self-contained executables: no libgcc / libwinpthread DLLs needed next to them.
    add_link_options(-static)
endif()

# Enable testing globally
include(CTest)

add_subdirectory(libs)
add_subdirectory(apps)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
