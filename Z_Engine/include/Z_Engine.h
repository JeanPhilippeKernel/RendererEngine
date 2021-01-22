#pragma once
#include "Z_EngineDef.h"

#include "Engine.h"


#include "Rendering/Shaders/Shader.h"
#include "Rendering/Buffers/VertexBuffer.h"
#include "Rendering/Buffers/IndexBuffer.h"
#include "Rendering/Buffers/VertexArray.h"


#include "Rendering/Scenes/GraphicScene.h"
#include "Rendering/Scenes/GraphicScene2D.h"
#include "Rendering/Scenes/GraphicScene3D.h"

#include "Rendering/Renderers/GraphicRenderer.h"
#include "Rendering/Renderers/GraphicRenderer2D.h"
#include "Rendering/Renderers/GraphicRenderer3D.h"

#include "Rendering/Cameras/Camera.h"
#include "Rendering/Cameras/OrthographicCamera.h"
#include "Rendering/Cameras/PerspectiveCamera.h"
#include "Rendering/Cameras/OrbitCamera.h"
#include "Rendering/Cameras/FirstPersonShooterCamera.h"


#include "Rendering/Geometries/IGeometry.h"
#include "Rendering/Geometries/CubeGeometry.h"
#include "Rendering/Geometries/QuadGeometry.h"
#include "Rendering/Geometries/SquareGeometry.h"

#include "Rendering/Materials/ShaderMaterial.h"
#include "Rendering/Materials/StandardMaterial.h"
#include "Rendering/Materials/MixedTextureMaterial.h"


#include "Rendering/Meshes/Mesh.h"
#include "Rendering/Meshes/MeshBuilder.h"
										
#include "Rendering/Textures/Texture.h"

#include "Inputs/IDevice.h"
#include "Inputs/Keyboard.h"
#include "Inputs/KeyCode.h"
#include "Inputs/Mouse.h"


#include "Managers/IManager.h"
#include "Managers/ShaderManager.h"
#include "Managers/TextureManager.h"

#include "Controllers/IController.h"
#include "Controllers/ICameraController.h"
#include "Controllers/OrthographicCameraController.h"
#include "Controllers/PerspectiveCameraController.h"
#include "Controllers/OrbitCameraController.h"
#include "Controllers/FirstPersonShooterCameraController.h"


#include "Core/TimeStep.h"
#include "Core/Utility.h"
 
#include "Layers/ImguiLayer.h"


//#include "EntryPoint.h"