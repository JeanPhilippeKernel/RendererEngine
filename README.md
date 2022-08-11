[![ZEngine Window Build](https://github.com/JeanPhilippeKernel/RendererEngine/actions/workflows/window-build.yml/badge.svg)](https://github.com/JeanPhilippeKernel/RendererEngine/actions/workflows/window-build.yml)	[![ZEngine Linux Build](https://github.com/JeanPhilippeKernel/RendererEngine/actions/workflows/linux-build.yml/badge.svg)](https://github.com/JeanPhilippeKernel/RendererEngine/actions/workflows/linux-build.yml) [![ZEngine macOS Build](https://github.com/JeanPhilippeKernel/RendererEngine/actions/workflows/macOS-build.yml/badge.svg)](https://github.com/JeanPhilippeKernel/RendererEngine/actions/workflows/macOS-build.yml)

# ZEngine

ZEngine is an open-source 2D - 3D rendering engine written in C++ and using OpenGL as graphic API.
It can be used for activities such as:
  - game prototyping
  - scientific computation and visualization

### Supported Platforms:
- Windows
- macOS
- Linux (`Debian` or `Ubuntu` are recommended systems)

## Setup

Before building, make sure your setup is correct : 

### Setup Window machine

- Install Visual Studio 2019 Community or Professional, make sure to add "Desktop development with C++".
- Install [PowerShell Core](https://github.com/PowerShell/PowerShell/releases)  

### Setup macOS machine

- Install lastest version of `Visual Studio Code` or any text editor
- Install [PowerShell Core](https://github.com/PowerShell/PowerShell/releases)
- Install compiler `Apple Clang` (most recent version)

### Setup Linux machine

- Install lastest version of `Visual Studio Code` or any text editor
- Install [PowerShell Core](https://github.com/PowerShell/PowerShell/releases)
- Install compiler `gcc-11`
- Install debugger `gdb`
- Install packages
 	`libxext-dev libasound2-dev libgl1-mesa-dev libpulse-dev`
	`libudev-dev libdbus-1-dev libx11-dev libxcursor-dev libxi-dev`
	`libxinerama-dev libxrandr-dev libxss-dev libxt-dev libxxf86vm-dev`

## Building 

As this project uses differents dependencies, make sure you've cloned the project with the `--recursive` option.
You can also do  `git submodule update --init --recursive`.

1. Install [CMake](https://cmake.org/download/) 3.20 or later.
2. Start `Powershell Core` and make sure that you can run CMake, you can type `cmake --version` to simply output the current CMake's version installed.
3. Change directories to the location where you've cloned the repository.
4. Building on different systems
	- Building on Windows : 
		- Debug version :	`.\Scripts\BuildEngine.ps1 -Configurations Debug -RunBuilds $True -VsVersion 2019`
		- Release version :	`.\Scripts\BuildEngine.ps1 -Configurations Release -RunBuilds $True -VsVersion 2019`

	- Building on macOS and Linux :
		- Debug version :	`.\Scripts\BuildEngine.ps1 -Configurations Debug -RunBuilds $True`
		- Release version :	`.\Scripts\BuildEngine.ps1 -Configurations Release -RunBuilds $True`


- Notes :
	- `RunBuilds` can be omitted as its default value is : `$True`.
	- You can build `Debug` and `Release` versions at once by omitting the `Configuration` parameter
	- On Windows, you can specify the Visual Studio version with `VsVersion`, it can be omitted as its default value is : `2019`

## Dependencies

The project uses the following dependencies : 
 - [SDL2](https://www.libsdl.org/download-2.0.php) for window creation and user input management for Linux,
 - [GLFW](https://github.com/glfw/glfw) for window creation and user input management for Window and MacOS,
 - [GLM](https://glm.g-truc.net/0.9.9/index.html) for functions and mathematical calculations,
 - [GLAD](https://glad.dav1d.de/) for openGL functions 
 - [STB](https://github.com/nothings/stb) for loading and manipulating image files for textures.
 - [ImGUI](https://github.com/ocornut/imgui) for GUI components and interaction.
 - [SPDLOG](https://github.com/gabime/spdlog) for logging
 - [EnTT](https://github.com/skypjack/entt) for entity component system
 - [Assimp](https://github.com/assimp/assimp) for managing and loading assest 2D -3D models
