# Build from source

Use the repository build script for the normal local build. It selects the
platform preset, configures CMake, builds the selected configuration, and
installs the result into the corresponding `Result.*` directory.

## Prerequisites

| Platform | Required development tools |
|---|---|
| Windows | Visual Studio with C++ desktop tooling, a supported Windows SDK, CMake, PowerShell Core, Python, and LLVM/clang-format |
| macOS | Xcode, CMake, NuGet, PowerShell Core, and LLVM/clang-format |
| Linux | Build tools, CMake, Ninja, LLVM/Clang, Python, NuGet, and PowerShell Core |

The [CMake preset matrix](#cmake-presets) is the authoritative list of
source-build targets. Install a Vulkan development environment appropriate to
your platform before attempting local graphics development.

## Build with the repository script

Run from the repository root in PowerShell Core:

```powershell
# Debug
./Scripts/BuildEngine.ps1 -Configurations Debug -RunBuilds $True

# Release
./Scripts/BuildEngine.ps1 -Configurations Release -RunBuilds $True
```

The script builds both Debug and Release when `-Configurations` is omitted.
Use `-RunBuilds $False` when you only need the generated CMake build directory.
The script runs formatting by default; use `-VerifyFormatting $True` to make
formatting failures explicit.

## CMake presets

You can configure and build a preset directly. For example, on Apple silicon:

```sh
cmake --preset Darwin_arm64_Debug
cmake --build --preset Darwin_arm64_Debug
```

| Platform | Debug | Release |
|---|---|---|
| Windows x64 | `Windows_x64_Debug_2022` or `_2026` | `Windows_x64_Release_2022` or `_2026` |
| macOS x64 | `Darwin_x64_Debug` | `Darwin_x64_Release` |
| macOS arm64 | `Darwin_arm64_Debug` | `Darwin_arm64_Release` |
| Linux x64 | `Linux_x64_Debug` | `Linux_x64_Release` |

## What the build produces

The root CMake project builds resources and shaders, the `zEngineLib` static
library, Tetragrama, the `Obelisk` executable, and the `ZEngineTests` target
when tests are enabled. The installed output retains the runtime resources
needed by the application.

Continue to [Run and debug](run-and-debug.md) after a successful build.
