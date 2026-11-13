add_library(iotwin_platform STATIC
    pal_time.c pal_time.h
    pal_rt.c pal_rt.h
    cycle_timer.c cycle_timer.h
    cli.c cli.h
)
target_include_directories(iotwin_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(iotwin_platform PUBLIC iotwin_logger Threads::Threads PRIVATE iotwin_compile_options)
if(WIN32)
    target_link_libraries(iotwin_platform PRIVATE winmm)
endif()
