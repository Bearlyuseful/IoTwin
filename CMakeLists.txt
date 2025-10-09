cmake_minimum_required(VERSION 3.22)
project(IoTwin LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

# Enable testing globally
include(CTest)

add_subdirectory(src)
add_subdirectory(tests)