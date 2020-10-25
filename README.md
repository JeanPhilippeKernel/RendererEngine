# Z-Engine

Z-Engine is an open-source 2D - 3D rendering engine written in C ++.
It can be used for activities such as:
  - game prototyping
  - scientific calculation and visualization in the plane and space

You can download the engine by using cloning the repository or downloading it as zip.

### Supported Platforms:
- Windows 10 Desktop (x64) [Visual Studio 2019 +]



## Usage
If you want to use Z-Engine in your application, you will need to include the **Z_Engine.lib** library
and the **#include <Z_Engine.h>** and  **#include <EntryPoint.h>** header in your code then create an engine's instance and a main layer


Let's create a layer that will be our main layer: ***MainLayer.h*** and ***MainLayer.cpp***

Here is our content of ***MainLayer.h***
```CPP
#pragma once
#include <Z_Engine.h>

class MainLayer : public Z_Engine::Layers::Layer {
public:
    MainLayer(const char* name = "main layer") : Layer(name)
	{
	}

	virtual ~MainLayer() =  default;

	virtual void Initialize()				override;
	virtual void Update(Z_Engine::Core::TimeStep dt)	override;
	virtual void Render()					override;
	virtual bool OnEvent(Z_Engine::Event::CoreEvent& e) 	override;

private:
	Z_Engine::Ref<Z_Engine::Rendering::Renderers::GraphicRenderer2D> 	m_renderer;
	Z_Engine::Ref<Z_Engine::Controllers::OrthographicCameraController> 	m_camera_controller;
	Z_Engine::Ref<Z_Engine::Rendering::Meshes::Mesh> 			m_quad_mesh_ptr;

};
```
then here is our content of ***MainLayer.cpp***
```CPP
#include "MainLayer.h"
#include <glm/gtc/type_ptr.hpp>	

using namespace Z_Engine::Rendering::Renderers;
using namespace Z_Engine::Rendering::Cameras;
using namespace Z_Engine::Window;
using namespace Z_Engine::Core;
using namespace Z_Engine::Inputs;

using namespace Z_Engine::Controllers;

void MainLayer::Initialize() {

	m_camera_controller.reset(new OrthographicCameraController(GetAttachedWindow(), true));
	
    	m_renderer.reset(new GraphicRenderer2D());
		
	m_camera_controller->Initialize();
	m_renderer->Initialize();
		
	quad_mesh_ptr.reset(MeshBuilder::CreateQuad({0.0f, 0.0f}, {0.5f, 0.5f}, {25.f, 10.f, 60.f},  glm::radians(60.0f)));
}

void MainLayer::Update(Z_Engine::Core::TimeStep dt) {
	m_camera_controller->Update(dt);
	// Your logic here
}

bool MainLayer::OnEvent(Z_Engine::Event::CoreEvent& e) {
	m_camera_controller->OnEvent(e);
	return false;
}

void MainLayer::Render() {
	// your drawing logic here
	//Set the color you want to use to clear the screen window
	RendererCommand::SetClearColor({ 0.2f, 0.2f, 0.2f, 1.0f });
	//clear the screen
	RendererCommand::Clear();

	//Prepare the renderer to batch all geometry we can to render
	m_renderer->StartScene(m_camera_controller->GetCamera());
	
	m_renderer->Draw(quad_mesh_ptr);
	
	//Send object to GPU to effectively rendere them on the screen
	m_renderer->EndScene();
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

## How to build: 

To build **Z-Engine**, you'll have to use Visual Studio and the provided solution file.
There are 2 projects that you can run :
 - Sandbox : This project represents the experimental setup that we use to test 2D features of the engine.
 - Sandbox3D : This project represents the experimental setup that we use to test 3D features of the engine. 

You just have to set either as startup project and press F5 in Visual Studio to build and run. 
For optimal performance, choose Release mode, for the best debugging experience, choose Debug mode.

### Dependancies

The project uses the following dependancies : 
 - [SDL2](https://www.libsdl.org/download-2.0.php) for window creation and user input management,
 - [GLM](https://glm.g-truc.net/0.9.9/index.html) for functions and mathematical calculations,
 - [GLEW](http://glew.sourceforge.net/) for openGL functions 
 - [STB](https://github.com/nothings/stb) for loading and manipulating image files for textures.
 - [ImGUI](https://github.com/ocornut/imgui) for GUI components and interaction.


### Building

You have just to clone the repository and build it
