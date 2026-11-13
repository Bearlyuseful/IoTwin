# Build Instructions

IoTwin has two parts that are built separately:

1. **Core** (C, CMake): `iotwin-bus`, `can_sim` and the tests.
2. **Visualizer** (Unreal Engine 5.6 project in `src/ue/IoTwin`).

---

## Core

### Prerequisites

| Tool     | Version | Notes                                                       |
|----------|---------|-------------------------------------------------------------|
| GCC      | 11+     | the build enforces GCC; on Windows MSYS2 UCRT64 GCC          |
| CMake    | 3.22+   |                                                             |
| Ninja    |         | used by the CMake presets                                   |
| Git      |         | Catch2 is fetched during configure (tests)                  |

**Linux (Debian/Ubuntu):**

```bash
sudo apt-get install -y build-essential cmake ninja-build git
```

**Windows:** install [MSYS2](https://www.msys2.org/), then in the *UCRT64* shell:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja git
```

Make sure `C:\msys64\ucrt64\bin` is on `PATH` if you build from PowerShell
or Git Bash. The Windows executables are linked statically and run without
MSYS2 installed.

### Build and test

```bash
cmake --preset gcc-debug          # or gcc-release
cmake --build --preset gcc-debug
ctest --preset gcc-debug
```

Binaries are placed in `build/<preset>/bin/`:

| Binary        | Purpose                                     |
|---------------|---------------------------------------------|
| `iotwin-bus`  | virtual CAN bus server                      |
| `can_sim`     | simulated light ECU                         |
| `iotwin_tests`| Catch2 test runner (used by `ctest`)        |

Add `-DIOTWIN_WARNINGS_AS_ERRORS=ON` to the configure step to treat
warnings as errors (CI does this).

### Run

```bash
build/gcc-debug/bin/iotwin-bus
build/gcc-debug/bin/can_sim        # second terminal, type 'help'
```

All options: `--help`. Real-time options are described in
[Real-Time Operation](realtime.md).

---

## Visualizer (Unreal Engine)

### Prerequisites

- Unreal Engine **5.6** (Epic Games Launcher or source build)
- Windows: Visual Studio 2022 with the "Game development with C++" workload

### Open in the editor

Open `src/ue/IoTwin/IoTwin.uproject`. On first open Unreal asks to rebuild
the `IoTwin` module - confirm with *Yes*. Start the core first, then press
*Play*: the demo scene connects to `127.0.0.1:8080`.

### Build from the command line

```powershell
& "<UE>\Engine\Build\BatchFiles\Build.bat" IoTwinEditor Win64 Development `
    -Project="$PWD\src\ue\IoTwin\IoTwin.uproject" -WaitMutex
```

Packaging a standalone build is described in [Packaging](packaging.md).
