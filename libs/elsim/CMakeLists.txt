# Electrical simulation core (ADR 0007): network solver, installation model, loader.
add_library(iotwin_elsim STATIC
    el_circuit.c el_circuit.h
    elsim.c elsim.h
    el_install.c el_install.h
)
target_include_directories(iotwin_elsim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(iotwin_elsim PUBLIC iotwin_json PRIVATE iotwin_compile_options)
if(NOT WIN32)
    target_link_libraries(iotwin_elsim PUBLIC m)
endif()
