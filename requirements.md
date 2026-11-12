# Requirements

This page lists the requirements for building ZEngine from source. The
[source-build guide](https://jeanphilippekernel.github.io/zodiac-engine-docs/development/build-from-source/)
contains the full setup instructions and CMake preset matrix.

## Common requirements

- Git.
- CMake 3.23 or later to use the repository's CMake presets.
- A C and C++ toolchain with C++20 support.
- PowerShell Core (`pwsh`) for `Scripts/BuildEngine.ps1`.
- LLVM `clang-format` for formatting and the standard build-script checks.
- Network access on the first configure/build: CMake FetchContent downloads
  third-party source dependencies.
- A Vulkan-capable graphics driver for running the editor and renderer.

Most third-party C++ libraries are declared and fetched by
`dependencies.cmake`; they generally do not need to be installed separately.
Platform SDKs and native development libraries still need to be available.

## Platform toolchains

| Platform | Toolchain and native dependencies |
|---|---|
| Windows x64 | Visual Studio 2022 or 2026 with C++ desktop development tools and a Windows SDK; CMake; PowerShell Core; NuGet. |
| macOS x64 / arm64 | Xcode and its command-line tools; CMake; PowerShell Core; NuGet. |
| Linux x64 | C/C++ compiler and build tools; Ninja; CMake; `pkg-config`; PowerShell Core; GLib/GIO, Wayland client and scanner development files, and the X11/GLFW native development libraries used by the build. |

LLVM `clang-format` is required when running the default formatting step of
`Scripts/BuildEngine.ps1`. The CI formatting job currently uses LLVM 22.

## Build commands

From the repository root in PowerShell Core:

```powershell
./Scripts/BuildEngine.ps1 -Configurations Debug -RunBuilds $True
```

To configure and format without compiling:

```powershell
./Scripts/BuildEngine.ps1 -Configurations Debug -RunBuilds $False
```

The available presets are defined in `CMakePresets.json`. Use
`cmake --list-presets` to see the presets supported by the installed CMake.

## CI dependencies

GitHub Actions runs on Windows, macOS, and Linux. The Linux build job installs
native audio, GL, X11, Wayland, and XKB development packages before
configuring. See the [CI workflow](workflow.md) and the platform build jobs in
`.github/workflows/` for the maintained CI package list.
