[![ZEngine Window Build](https://github.com/JeanPhilippeKernel/RendererEngine/actions/workflows/window-build.yml/badge.svg)](https://github.com/JeanPhilippeKernel/RendererEngine/actions/workflows/window-build.yml)	[![ZEngine Linux Build](https://github.com/JeanPhilippeKernel/RendererEngine/actions/workflows/linux-build.yml/badge.svg)](https://github.com/JeanPhilippeKernel/RendererEngine/actions/workflows/linux-build.yml) [![ZEngine macOS Build](https://github.com/JeanPhilippeKernel/RendererEngine/actions/workflows/macOS-build.yml/badge.svg)](https://github.com/JeanPhilippeKernel/RendererEngine/actions/workflows/macOS-build.yml) 

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
- Install [PowerShell Core](https://github.com/PowerShell/PowerShell/releases)
- Install [Python](https://www.python.org/ftp/python/3.12.4/python-3.12.4-amd64.exe)

## Building 

1. Install [CMake](https://cmake.org/download/) 3.20 or later.
2. Start `Powershell Core` and make sure that you can run CMake, You can type `cmake --version` to simply output the current CMake version installed.
3. Change directories to the location where you've cloned the repository.
4. Building on different systems
	- Building on Windows : 
		- Debug version :	`.\Scripts\BuildEngine.ps1 -Configurations Debug -RunBuilds $True -VsVersion 2022`
		- Release version :	`.\Scripts\BuildEngine.ps1 -Configurations Release -RunBuilds $True -VsVersion 2022`

- Notes :
	- `RunBuilds` can be omitted as its default value is: `$True`.
	- You can build `Debug` and `Release` versions at once by omitting the `Configuration` parameter
	- On Windows, you can specify the Visual Studio version with `VsVersion`, it can be omitted as its default value is: `2022`
	- If you want to work on the launcher only, you can specify the Launcher only parameter with `LauncherOnly` then only the launcher will be build.

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
