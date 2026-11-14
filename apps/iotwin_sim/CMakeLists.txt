add_executable(iotwin-sim main_iotwin_sim.c)
target_link_libraries(iotwin-sim PRIVATE iotwin_elsim iotwin_json iotwin_compile_options)

install(TARGETS iotwin-sim RUNTIME DESTINATION bin)
