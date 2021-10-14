![build](https://github.com/JeanPhilippeKernel/RendererEngine/workflows/MSBuild/badge.svg?branch=develop)

# Z-Engine

Z-Engine is an open-source 2D - 3D rendering engine written in C++ and using OpenGL as graphic API.
It can be used for activities such as:
  - game prototyping
  - scientific calculation and visualization in the plane and space

### Supported Platforms:
- Windows
- Linux
- MacOS (coming soon)



## Usage
If you want to use Z-Engine in your application, you will need to include the **Z_Engine.lib** library
and the **#include <ZEngine.h>** and  **#include <EntryPoint.h>** header in your code then create an engine's instance and a main layer


Let's create a layer that will be our main layer: ***MainLayer.h*** and ***MainLayer.cpp***

Here is our content of ***MainLayer.h***
```CPP
#pragma once
#include <ZEngine.h>

class MainLayer : public Z_Engine::Layers::Layer {
public:
    MainLayer(const char* name = "main layer") : Layer(name)
	{
	}

	virtual ~MainLayer() =  default;

		virtual void Initialize()							override;
		virtual void Update(Z_Engine::Core::TimeStep dt)	override;

		virtual void Render()								override;
						   
		virtual bool OnEvent(Z_Engine::Event::CoreEvent& e) override;

private:
		Z_Engine::Ref<Z_Engine::Managers::TextureManager>						m_texture_manager; 
		Z_Engine::Ref<Z_Engine::Rendering::Scenes::GraphicScene>				m_scene;
		Z_Engine::Ref<Z_Engine::Rendering::Meshes::Mesh>						quad_mesh_ptr;
		Z_Engine::Ref<Z_Engine::Rendering::Meshes::Mesh>						quad_mesh_ptr_2;
		Z_Engine::Ref<Z_Engine::Rendering::Meshes::Mesh>						quad_mesh_ptr_3;

};
```
then here is our content of ***MainLayer.cpp***
```CPP
#include "MainLayer.h"

using namespace Z_Engine;
using namespace Z_Engine::Rendering::Materials;
using namespace Z_Engine::Rendering::Scenes;
using namespace Z_Engine::Rendering::Renderers;
using namespace Z_Engine::Window;
using namespace Z_Engine::Core;
using namespace Z_Engine::Inputs;
using namespace Z_Engine::Event;
using namespace Z_Engine::Managers;
using namespace Z_Engine::Rendering::Textures;
using namespace Z_Engine::Controllers;
using namespace Z_Engine::Rendering::Meshes;

void MainLayer::Initialize() {
		m_texture_manager.reset(new Z_Engine::Managers::TextureManager());
		m_texture_manager->Load("Assets/Images/free_image.png");
		m_texture_manager->Load("Assets/Images/Crate.png");
		m_texture_manager->Load("Assets/Images/Checkerboard_2.png");


		m_scene.reset(new GraphicScene3D(new OrbitCameraController(GetAttachedWindow(), glm::vec3(0.0f, 20.0f, 50.f), 10.0f, -20.0f)));
		m_scene->Initialize();
		
		quad_mesh_ptr.reset(MeshBuilder::CreateCube({ 0.f, -0.5f, 0.0f }, { 100.f, .0f, 100.f }, 0.0f,  glm::vec3(1.f, 0.0f, 0.0f), m_texture_manager->Obtains("Checkerboard_2")));
		
		quad_mesh_ptr_3.reset(MeshBuilder::CreateCube({ -20.f, 10.f, 0.0f }, { 5.f, 5.0f, 5.f }, {45.0f, 120.0f, 30.0f}, 0.0f, glm::vec3(0.f, 1.0f, 0.0f)));
		
		Ref<MixedTextureMaterial> material(new MixedTextureMaterial{});
		material->SetInterpolateFactor(0.5f);
		material->SetTexture(m_texture_manager->Obtains("free_image"));
		material->SetSecondTexture(m_texture_manager->Obtains("Crate"));
		
		quad_mesh_ptr_2.reset(MeshBuilder::CreateCube({ 0.f, 10.f, 0.0f }, { 10.f, 10.0f, 10.f }, 0.0f, glm::vec3(0.f, 1.0f, 0.0f)));
		quad_mesh_ptr_2->SetMaterial(material);
}

void MainLayer::Update(Z_Engine::Core::TimeStep dt) {
		m_scene->GetCameraController()->Update(dt);

		quad_mesh_ptr_2
			->GetGeometry()
			->ApplyTransform(
				glm::rotate(glm::mat4(1.0f), glm::sin((float)dt) * 0.005f, glm::vec3(0.f, 1.0f, 0.0f)) 
			);
}

bool MainLayer::OnEvent(Z_Engine::Event::CoreEvent& e) {
		m_scene->GetCameraController()->OnEvent(e);
		return false;
}

void MainLayer::Render() {
		m_scene->Add(quad_mesh_ptr);
		m_scene->Add(quad_mesh_ptr_2);
		m_scene->Add(quad_mesh_ptr_3);

		m_scene->Render();
}
```
Let's create an instance of engine and add our main layer : ***my_engine.cpp***

```CPP
#include <EntryPoint.h>
#include "MainLayer.h"

class my_engine : public Z_Engine::Engine {
public:	
	my_engine() {
		PushLayer(new MainLayer());
	}
	~my_engine() = default;
};

Z_Engine::Engine* CreateEngine() { return new my_engine::my_engine(); } 
```
and voilà ! 

## Setup

Before building, make sure your setup is correct : 

### Setup Window machine

- Install Visual Studio 2019 Community or Professional, make sure to add "Desktop development with C++".
- Install [PowerShell Core](https://github.com/PowerShell/PowerShell/releases)  

### Setup Linux machine

- Install lastest version of `Visual Studio Code` or any text editor
- Install [PowerShell Core](https://github.com/PowerShell/PowerShell/releases)
- Install compiler `clang version 7.0.1`

## Building: 

As this project uses differents dependencies, make sure you've cloned the project with the `--recursive` option.
you can also do  `git submodule update --init --recursive`.

1. Install [CMake](https://cmake.org/download/) 3.20 or later
2. Start your favorite terminal and be sure that you can run CMake, you can type `cmake --version` to simply output the current CMake's version installed
3. Change directories to the location where you've cloned the repository.
4. Invoke `cmake -S . -B .\out\build -DSDL_STATIC=ON -DSDL_SHARED=OFF -DSPDLOG_BUILD_SHARED=OFF -DBUILD_STATIC_LIBS=ON`
5. Invoke `cmake --build .\out\build`

There are 2 projects that you can run :
 - Sandbox : This project represents the experimental setup that we use to test 2D features of the engine.
 - Sandbox3D : This project represents the experimental setup that we use to test 3D features of the engine. 

### Dependencies

The project uses the following dependancies : 
 - [SDL2](https://www.libsdl.org/download-2.0.php) for window creation and user input management,
 - [GLM](https://glm.g-truc.net/0.9.9/index.html) for functions and mathematical calculations,
 - [GLEW](http://glew.sourceforge.net/) for openGL functions 
 - [STB](https://github.com/nothings/stb) for loading and manipulating image files for textures.
 - [ImGUI](https://github.com/ocornut/imgui) for GUI components and interaction.
 - [spdlog](https://github.com/gabime/spdlog) for logging

