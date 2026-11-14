@echo off
rem IoTwin light demo: starts the virtual CAN bus, the simulated light ECU and the visualizer.
setlocal
cd /d "%~dp0"

start "IoTwin Bus" bin\iotwin-bus.exe --log-frames
rem give the bus a moment to open its port
ping -n 2 127.0.0.1 >nul
start "IoTwin CAN-Sim (light ECU)" bin\can_sim.exe
start "" Visualizer\IoTwin.exe -windowed -ResX=1600 -ResY=900

echo.
echo IoTwin started:
echo   - "IoTwin Bus" window:     bus traffic
echo   - "IoTwin CAN-Sim" window: type 'help' for commands (lamp on/off, dim 50, fault overload on, ...)
echo   - Visualizer:              click the wall switch or press SPACE, mouse wheel dims, TAB schematic
echo                              F short circuit at the lamp (breaker F1 trips), R resets it
echo   - Diagnostics:             bin\uds_client scan   /   scenarios: bin\can_scenario data\scenarios\light-demo\dimmer.scn
echo   - Electrical simulation:   bin\iotwin-sim --data data   (demo flat: breakers, RCD, faults, measurements)
echo.
echo Close the windows (or Ctrl+C) to stop.
