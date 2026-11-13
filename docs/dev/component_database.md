# Component Database

`data/` holds the engineering data the simulation and the visualizer build on.

```
data/
  components/<category>/<vendor>-<part>.json   parts and their characteristics
  systems/<setup>.json                         a concrete setup: nodes, buses, links, messages
  dbc/                                         CAN message definitions
  schema/                                      JSON schemas (editor completion and validation)
```

Every file references its schema (`"$schema"`), so editors such as VS Code
offer completion and flag mistakes while typing. `tools/validate_data.py`
checks everything including cross references and runs as CTest
`data_validation` in CI.

## Components

One JSON file per part. Example: `data/components/boards/st-nucleo-g0b1re.json`.

| Field | Meaning |
|-------|---------|
| `id` | unique `<vendor>.<part>`, e.g. `st.stm32g0b1ret6` |
| `category` | microcontroller, board, transceiver, sensor, actuator, lamp, relay, switch, power-supply, ... |
| `sources` | datasheets and manuals; every value refers to one of them |
| `properties` | non-numeric facts (core, package, debugger, ...) |
| `parameters` | numeric characteristics (see below) |
| `interfaces` | e.g. `fdcan` × 2 with tested bitrates |
| `ports` | pins, LEDs, buttons, connectors; `connects_to` maps board ports to MCU pins |
| `contains` | sub-components (the MCU on a board) |
| `simulation` | hints for the simulators and the visualizer |

A parameter has a value **or** a range, a unit, a source and a `verified` flag:

```json
"supply_voltage": { "min": 1.7, "max": 3.6, "unit": "V", "source": "st-product-page", "verified": true },
"run_current_64mhz": { "typ": null, "unit": "A", "source": "ds13560", "verified": false,
                       "note": "to fill from the table 'Current consumption in Run mode'" }
```

`verified: false` marks values that still have to be confirmed or filled in -
the database never pretends to know more than its sources say. Units: `V`, `A`,
`W`, `Hz`, `B`, `bit/s`, `°C`, `s`, `Ω`, `F`, `H`, `lm`, `%`, `1`.

### Current entries

| Component | Status |
|-----------|--------|
| `st.stm32g0b1ret6` | clock, memory, supply ranges, FDCAN (incl. 500 kbit/s measured in IoTwin), used pins; open: run/stop currents, USART count |
| `st.nucleo-g0b1re` | supply inputs, ST-LINK interfaces (VCP, mass storage), LD4/B1/FDCAN pins; open: measured board current, CN10 pin numbers |

## Systems

A system file describes one setup - which nodes take part, which buses and
point-to-point links connect them and which DBC messages they exchange.
`data/systems/light-demo.json` is the demo: light ECU, visualizer and the
NUCLEO-G0B1RE gateway on the 500 kbit/s demo bus, the board connected to the
PC by USB.

This is the input for the planned **2D schematic view** in the visualizer:
nodes become boxes, buses and links become lines, messages from the DBC are
shown live on them. Later, circuit diagrams and wireless networks are
described the same way.

## Adding a component

1. Copy an existing file into `data/components/<category>/`.
2. Fill in `sources` first, then the values with their `source` ids.
3. Run `python tools/validate_data.py`.
