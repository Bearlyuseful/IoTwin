IoTwin - Light Demo
===================

Digital twin of a simple light installation: a simulated control unit (ECU)
switches a lamp via a CAN bus, an Unreal Engine visualizer mirrors it and
lets you operate the wall switch.

Start
-----
Double-click start-demo.bat. Three windows open:

  IoTwin Bus       virtual CAN bus (iotwin-bus), shows all frames
  IoTwin CAN-Sim   simulated light ECU (can_sim), accepts commands
  IoTwin           3D visualizer

In the visualizer click the wall switch (or press SPACE): the switch sends
CAN frame 200#01, the ECU answers with 100#01, the relay closes and the lamp
turns on.

Press TAB to switch between the 3D scene and the 2D schematic: nodes, CAN bus
and USB link of the system with every message decoded at its sender. Click a
node for details, including the characteristics from the component database
(data\components). The system itself is described in data\systems\light-demo.json.

Commands in the CAN-Sim window:

  lamp on | lamp off | lamp toggle
  blink 500        let the ECU blink the lamp every 500 ms (blink off to stop)
  send 123#DEAD    put any frame on the bus
  status | help | quit

Components can also be started separately; every program has --help.
Visualizer options: -IoTwinBusHost=<host> -IoTwinBusPort=<port>
                    -IoTwinView=Schematic -IoTwinSystem=<system json>

With a NUCLEO-G0B1RE
--------------------
1. Copy firmware\iotwin_gateway.bin to the NOD_G0B1RE drive of the board.
2. Find the COM port ("STMicroelectronics STLink Virtual COM Port").
3. start-demo-hardware.bat COM5
The blue button B1 is a second wall switch, the green LED LD4 shows the lamp.

Requirements
------------
Windows 10/11 x64, DirectX 12 capable GPU. If the visualizer reports missing
runtime libraries, run Visualizer\Engine\Extras\Redist\en-us\UEPrereqSetup_x64.exe.

Documentation and source: https://github.com/BearlyUseful/IoTwin
