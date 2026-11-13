# Packaging

`tools/package.ps1` produces a self-contained Windows release:

```
dist/IoTwin-<version>-win64/
  bin/iotwin-bus.exe        static MinGW build, no runtime DLLs needed
  bin/can_sim.exe
  Visualizer/IoTwin.exe     cooked Unreal build (Win64)
  start-demo.bat            starts bus, ECU and visualizer
  README.txt, LICENSE
dist/IoTwin-<version>-win64.zip
```

## Requirements

- MSYS2 UCRT64 GCC, CMake and Ninja on `PATH`
- Unreal Engine 5.6 and Visual Studio 2022 (C++ game workload)

## Usage

```powershell
./tools/package.ps1 -EngineDir "C:\Program Files\Epic Games\UE_5.6\Engine"
```

| Parameter          | Default        | Meaning                                         |
|--------------------|----------------|-------------------------------------------------|
| `-EngineDir`       | `$env:UE_ENGINE_DIR` | `Engine` folder of the UE installation   |
| `-Version`         | `0.1.0`        | used in the package name                        |
| `-Configuration`   | `Development`  | `Shipping` removes logs and the console         |
| `-SkipVisualizer`  |                | reuse `dist/ue-staging` from a previous run     |
| `-SkipTests`       |                | do not run `ctest`                              |
| `-IncludeDebugInfo`|                | keep the visualizer `.pdb` (~350 MB more)       |

The first cook compiles shaders and takes several minutes; later runs are
incremental.

## CI

GitHub Actions builds and tests the core on Linux and Windows for every push
and pull request and uploads the binaries as artifacts. The visualizer is not
built in CI: GitHub-hosted runners have no Unreal Engine installation, so
releases are packaged locally with the script above.

## Repository size

`.uasset`/`.umap` files are stored with Git LFS (see `.gitattributes`).
Install Git LFS before cloning (`git lfs install`), otherwise the Unreal
project only contains pointer files.
