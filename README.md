[![ZEngine Window Build](https://github.com/JeanPhilippeKernel/RendererEngine/actions/workflows/window-build.yml/badge.svg)](https://github.com/JeanPhilippeKernel/RendererEngine/actions/workflows/window-build.yml)	[![ZEngine Linux Build](https://github.com/JeanPhilippeKernel/RendererEngine/actions/workflows/linux-build.yml/badge.svg)](https://github.com/JeanPhilippeKernel/RendererEngine/actions/workflows/linux-build.yml)

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
- Install compiler `clang`

### Setup Linux machine

- Install lastest version of `Visual Studio Code` or any text editor
- Install [PowerShell Core](https://github.com/PowerShell/PowerShell/releases)
- Install compiler `clang 7`
- Install debugger `gdb`
- Install packages `mesa-utils`, `libgl1-mesa-dev`, `mesa-common-dev`
- Install [SDL2](https://gigi.nullneuron.net/gigilabs/how-to-set-up-sdl2-on-linux/) 

## Building: 

As this project uses differents dependencies, make sure you've cloned the project with the `--recursive` option.
You can also do  `git submodule update --init --recursive`.

1. Install [CMake](https://cmake.org/download/) 3.20 or later
2. Start your favorite terminal and be sure that you can run CMake, you can type `cmake --version` to simply output the current CMake's version installed
3. Change directories to the location where you've cloned the repository.
4. Building on different systems
	- Building on Windows : 
		- Debug version :	`.\Scripts\BuildEngine.ps1 -SystemNames Windows -Architectures x64 -Configurations Debug -RunBuilds $True`
		- Release version :	`.\Scripts\BuildEngine.ps1 -SystemNames Windows -Architectures x64 -Configurations Release -RunBuilds $True`

	- Building on macOS :
		- Debug version :	`.\Scripts\BuildEngine.ps1 -SystemNames Darwin -Architectures x64 -Configurations Debug -RunBuilds $True`
		- Release version :	`.\Scripts\BuildEngine.ps1 -SystemNames Darwin -Architectures x64 -Configurations Release -RunBuilds $True`

	- Building on Linux :
		- Debug version :	`.\Scripts\BuildEngine.ps1 -SystemNames Linux -Architectures x64 -Configurations Debug -RunBuilds $True`
		- Release version :	`.\Scripts\BuildEngine.ps1 -SystemNames Linux -Architectures x64 -Configurations Release -RunBuilds $True`


## Testing

There are 2 examples projects that you can run/test :
 - Sandbox : Experimental setup that showcases Core 2D features.
 - Sandbox3D : Experimental setup that showcases Core 3D features. 

## Dependencies

The project uses the following dependencies : 
 - [SDL2](https://www.libsdl.org/download-2.0.php) for window creation and user input management,
 - [GLM](https://glm.g-truc.net/0.9.9/index.html) for functions and mathematical calculations,
 - [GLEW](http://glew.sourceforge.net/) for openGL functions 
 - [STB](https://github.com/nothings/stb) for loading and manipulating image files for textures.
 - [ImGUI](https://github.com/ocornut/imgui) for GUI components and interaction.
 - [SPDLOG](https://github.com/gabime/spdlog) for logging
