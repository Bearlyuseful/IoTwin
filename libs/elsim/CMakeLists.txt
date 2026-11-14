# Electrical simulation core (ADR 0007, 0008): network solver, installation model, loader, session.
add_library(iotwin_elsim STATIC
    el_circuit.c el_circuit.h
    elsim.c elsim.h
    el_install.c el_install.h
    el_session.c el_session.h el_atomic.h
)
target_include_directories(iotwin_elsim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(iotwin_elsim PUBLIC iotwin_json PRIVATE iotwin_compile_options)
if(NOT WIN32)
    target_link_libraries(iotwin_elsim PUBLIC m)
endif()
# The core also compiles with MSVC inside the Unreal visualizer: no variable-length arrays.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(iotwin_elsim PRIVATE -Wvla)
endif()
