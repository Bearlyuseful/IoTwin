# Small JSON reader for engineering data (host only).
add_library(iotwin_json STATIC json.c json.h)
target_include_directories(iotwin_json PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(iotwin_json PRIVATE iotwin_compile_options)
if(NOT WIN32)
    target_link_libraries(iotwin_json PUBLIC m)
endif()
