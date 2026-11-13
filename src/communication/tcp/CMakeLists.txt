add_library(iotwin_net STATIC
    net.c net.h
    line_reader.c line_reader.h
)
target_include_directories(iotwin_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(iotwin_net PRIVATE iotwin_compile_options)
if(WIN32)
    target_link_libraries(iotwin_net PUBLIC ws2_32)
endif()
