[![Engine Build and Tests](https://github.com/JeanPhilippeKernel/RendererEngine/actions/workflows/Engine-CI.yml/badge.svg)]

[![Discord Server](https://discord.com/api/guilds/1249429728624906405/widget.png?style=banner2)](https://discord.gg/jC3GPVKKsW)

# ZEngine

ZEngine is an open-source 3D rendering engine written in C++ and using Vulkan as graphic API.
It can be used for activities such as:
  - Gaming
  - Scientific computation and visualization

### Supported Platforms:
- Windows
- macOS (Under active revision as of today)
- Linux (`Debian` or `Ubuntu` are recommended systems) (Under active revision as of today)

## Setup

Before building, make sure your setup is correct : 

### Setup Window machine

- Install Visual Studio 2022 Community or Professional, make sure to add "Desktop development with C++".
    - Install MSVC v143 - VS 2022 C++ x64/x86 build tools and Windows 10/11 SDK.
- Install [PowerShell Core](https://github.com/PowerShell/PowerShell/releases)
- Install [Python](https://www.python.org/ftp/python/3.12.4/python-3.12.4-amd64.exe)
- Install [CMake](https://cmake.org/download/) 3.20 or later.
- Install [DOTNET SDK 8](https://dotnet.microsoft.com/en-us/download/dotnet/8.0) as a VS Build Tool component (if using a standalone implementation, you might need to create a symlink between your custom installation location and the expected location: `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Sdks\Microsoft.NET.Sdk\Sdk`)
- Install [LLVM](https://github.com/llvm/llvm-project/releases/download/llvmorg-20.1.7/LLVM-20.1.7-win64.exe)


### Setup macOS machine

- Install Xcode from the App Store.
- Install Homebrew from a terminal:
```bash
/usr/bin/ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
```

- Install CMake through Homebrew :
```bash
    brew update
    brew install cmake
```

- Install NuGet through Homebrew :
```bash
    brew update
    brew install nuget
```

- Install PowerShell Core through Homebrew:
```bash
    brew update
    brew install --cask powershell
```
- Install [DOTNET SDK 8](https://dotnet.microsoft.com/en-us/download/dotnet/8.0)

- Install ClangFormat through Homebrew:
```bash
    brew update
    brew install llvm@20
```

## Building with CMake Presets (Recommended)

ZEngine now supports CMake Presets for simplified building. This is the modern, cross-platform way to build the project.

### Prerequisites
- CMake 3.17 or later
- Platform-specific build tools (Visual Studio on Windows, Xcode on macOS, GCC on Linux)

### Configure and Build

**Debug Build:**
```bash
# Configure
cmake --preset linux-debug      # On Linux
cmake --preset windows-debug    # On Windows
cmake --preset macos-debug      # On macOS

# Build
cmake --build --preset linux-debug      # On Linux
cmake --build --preset windows-debug    # On Windows
cmake --build --preset macos-debug      # On macOS
```

**Release Build:**
```bash
# Configure
cmake --preset linux-release    # On Linux
cmake --preset windows-release  # On Windows
cmake --preset macos-release    # On macOS

# Build
cmake --build --preset linux-release    # On Linux
cmake --build --preset windows-release  # On Windows
cmake --build --preset macos-release    # On macOS
```

**Launcher Only Build:**
```bash
# For building only the Panzerfaust launcher
cmake --preset launcher-only-debug
cmake --build --preset launcher-only-debug

# Or release version
cmake --preset launcher-only-release
cmake --build --preset launcher-only-release
```

**Running Tests:**
```bash
ctest --preset linux-debug-test      # On Linux
ctest --preset windows-debug-test    # On Windows
ctest --preset macos-debug-test      # On macOS
```

### Available Presets

List all available presets:
```bash
cmake --list-presets=configure
cmake --list-presets=build
cmake --list-presets=test
```

### IDE Integration

CMakePresets.json is supported by:
- Visual Studio 2019 16.10+ and Visual Studio 2022
- Visual Studio Code with CMake Tools extension
- CLion 2021.3+
- Qt Creator 6.0+

Simply open the project folder in your IDE and select the desired preset from the configuration dropdown.

## Building the engine & launcher (Legacy PowerShell Method)

1. Start `Powershell Core` and make sure that you can run CMake, You can type `cmake --version` to simply output the current CMake version installed.
2. Change directories to the location where you've cloned the repository.
3. Run the following command depending on the configuration:
	- Debug version :	`.\Scripts\BuildEngine.ps1 -Configurations Debug -RunBuilds $True`
	- Release version :	`.\Scripts\BuildEngine.ps1 -Configurations Release -RunBuilds $True`

## Building the launcher only (Legacy PowerShell Method)

To only build the Launcher only, you can specify `-LauncherOnly` which will skip building the engine :
- Debug version :	`.\Scripts\BuildEngine.ps1 -Configurations Debug -RunBuilds $True -LauncherOnly`
- Release version :	`.\Scripts\BuildEngine.ps1 -Configurations Release -RunBuilds $True -LauncherOnly`

### Important Notes:
- Setting `-RunBuilds` to `$false` will result to *only* generate the build directory.
- Omitting `-Configuration` will result to generate and build for both `Debug` and `Release` versions.

## Roadmap
See our roadmap here [Roadmap](Roadmap.md)

## Dependencies

The project uses the following dependencies as submodules : 
 - [GLFW](https://github.com/glfw/glfw) for window creation and user input management for Windows, Linux, and MacOS,
 - [GLM](https://glm.g-truc.net/0.9.9/index.html) for functions and mathematical calculations,
 - [STB](https://github.com/nothings/stb) for loading and manipulating image files for textures.
 - [ImGUI](https://github.com/ocornut/imgui) for GUI components and interaction.
 - [SPDLOG](https://github.com/gabime/spdlog) for logging
 - [EnTT](https://github.com/skypjack/entt) for entity component system
 - [Assimp](https://github.com/assimp/assimp) for managing and loading asset 2D -3D models
 - [yaml-cpp](https://github.com/jbeder/yaml-cpp) for parsing and emitting YAML files
