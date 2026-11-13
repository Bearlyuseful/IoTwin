add_library(iotwin_light_ecu STATIC light_ecu.c light_ecu.h light_ecu_diag.c light_ecu_diag.h)
target_include_directories(iotwin_light_ecu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(iotwin_light_ecu PUBLIC iotwin_can PRIVATE iotwin_compile_options)

add_executable(can_sim main_can_sim.c)
target_link_libraries(can_sim PRIVATE
    iotwin_light_ecu
    iotwin_can
    iotwin_net
    iotwin_platform
    iotwin_logger
    Threads::Threads
    iotwin_compile_options
)

target_compile_definitions(can_sim PRIVATE IOTWIN_VERSION="${PROJECT_VERSION}")

install(TARGETS can_sim RUNTIME DESTINATION bin)
