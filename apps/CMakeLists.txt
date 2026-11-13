# Executables.
add_subdirectory(bus)
add_subdirectory(can_sim)
add_subdirectory(serial_bridge)
add_subdirectory(can_monitor)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(socketcan_bridge)
endif()
