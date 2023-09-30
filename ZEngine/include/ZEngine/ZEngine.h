#pragma once
#include "ZEngineDef.h"

#include "Engine.h"

#include "Core/Coroutine.h"

#include "Rendering/Shaders/Shader.h"
#include "Rendering/Buffers/VertexBuffer.h"
#include "Rendering/Buffers/IndexBuffer.h"
#include "Rendering/Buffers/Framebuffer.h"
#include "Rendering/Specifications/FrameBufferSpecification.h"

#include "Rendering/Scenes/GraphicScene.h"

#include "Serializers/GraphicSceneSerializer.h"
#include "Serializers/GraphicScene3DSerializer.h"

#include "Rendering/Entities/GraphicSceneEntity.h"

#include "Rendering/Components/NameComponent.h"
#include "Rendering/Components/TransformComponent.h"
#include "Rendering/Components/GeometryComponent.h"
#include "Rendering/Components/MaterialComponent.h"
#include "Rendering/Components/LightComponent.h"
#include "Rendering/Components/CameraComponent.h"
#include "Rendering/Components/ValidComponent.h"

#include "Rendering/Renderers/GraphicRenderer.h"
#include "Rendering/Renderers/ImGUIRenderer.h"
#include "Rendering/Renderers/SceneRenderer.h"
#include "Rendering/ResourceTypes.h"

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
#include "Rendering/Materials/BasicMaterial.h"
#include "Rendering/Materials/StandardMaterial.h"

#include "Rendering/Meshes/Mesh.h"
#include "Rendering/Lights/Light.h"
#include "Rendering/Lights/DirectionalLight.h"
#include "Rendering/Meshes/MeshBuilder.h"
#include "Rendering/Textures/Texture.h"

#include "Inputs/IDevice.h"
#include "Inputs/Keyboard.h"
#include "Inputs/KeyCode.h"
#include "Inputs/Mouse.h"
#include "Inputs/KeyCodeDefinition.h"

#include "Managers/IManager.h"
#include "Managers/ShaderManager.h"
#include "Managers/TextureManager.h"

#include "Controllers/IController.h"
#include "Controllers/ICameraController.h"
#include "Controllers/OrthographicCameraController.h"
#include "Controllers/PerspectiveCameraController.h"
#include "Controllers/OrbitCameraController.h"
#include "Controllers/FirstPersonShooterCameraController.h"

#include "Maths/Math.h"

#include "Core/TimeStep.h"
#include "Core/Utility.h"

#include "Layers/ImguiLayer.h"

#include "Components/UIComponent.h"
#include "Components/UIComponentEvent.h"

#include "Event/EventCategory.h"
#include "Event/EventType.h"

#include <ImGuizmo/ImGuizmo.h>
