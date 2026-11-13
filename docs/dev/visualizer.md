# Visualizer (Unreal Engine)

The Unreal project lives in `src/ue/IoTwin` (UE 5.6, C++ module `IoTwin`).

## Bus integration (`Source/IoTwin/Bus`)

| Class                   | Role                                                                 |
|-------------------------|----------------------------------------------------------------------|
| `FCanFrame`             | CAN frame struct + text codec (same format as the C core)            |
| `FIoTwinBusConnection`  | worker thread: TCP connection, auto-reconnect, line framing          |
| `UIoTwinBusSubsystem`   | game-instance subsystem: `SendFrame`, `OnFrameReceived`, `OnConnectionChanged`, last value per id |
| `UCanSignalComponent`   | maps one bit of a frame to a `bool` (Receive or Transmit)            |

Connection settings (`Config/DefaultGame.ini`):

```ini
[/Script/IoTwin.IoTwinBusSubsystem]
Host=127.0.0.1
Port=8080
bEnabled=True
```

Command line overrides: `-IoTwinBusHost=<host>`, `-IoTwinBusPort=<port>`,
`-IoTwinBusDisabled`.

### Binding a signal in Blueprint

Add a **Can Signal** component to any actor, set *Direction*, *Can Id* (hex,
e.g. `100`), *Byte Index* and *Bit Index*.

- *Receive*: bind to **On Value Changed** or read **Get Value**.
- *Transmit*: call **Set Value**; the frame is sent on change, after a
  reconnect, and optionally every *Transmit Cycle Seconds*.

For full frames use the subsystem directly (**Get IoTwin Bus Subsystem** →
**Send Frame** / **On Frame Received**).

## Electrical model (`Source/IoTwin/Systems`)

- `UElectricalPortComponent`: wiring is a symmetric list of ports stored on
  both ends (saved with the level). `HasPower()` searches the wiring graph for
  a supplying port; cycles are handled.
- `APowerSourceActor`: supply with an output relay. With *Controlled By Can*
  the relay follows its `RelayControl` signal (default `0x100` byte 0 bit 0).
- `AElectricalCableActor`: two conducting ends, drawn with `CableComponent`;
  `ConnectBetween(A, B)` positions and wires it.
- `ALampActor`: port → `UElectricalDeviceComponent` → `ULightEmitterComponent`
  and a bulb that changes colour.
- `AWallSwitchActor`: clickable switch that transmits its position (`0x200`).

Older assets that only stored `ConnectedPort` are migrated automatically when
the component is registered.

## Demo scene

`AGM_Simulation` (the default game mode) builds the light demo at runtime from
engine basic shapes, so it works in any map and needs no extra assets. To build
your own scene, create a Blueprint subclass of `GM_Simulation`, untick
*Spawn Demo Scene* (the relay and switch ids are configurable there as well)
and select it as *GameMode Override* in the map's World Settings.

## Tests and smoke runs

```powershell
# Automation tests (CAN codec)
UnrealEditor-Cmd.exe src/ue/IoTwin/IoTwin.uproject -ExecCmds="Automation RunTests IoTwin;Quit" -unattended -nullrhi

# Unattended game run: screenshot after 10 s, quit after 15 s
UnrealEditor.exe src/ue/IoTwin/IoTwin.uproject -game -IoTwinScreenshot=10 -IoTwinQuitAfter=15
```

The screenshot is written to `Saved/Screenshots/IoTwinDemo.png`.
