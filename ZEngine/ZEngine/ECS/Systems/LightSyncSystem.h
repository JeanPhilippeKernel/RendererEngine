#pragma once
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>

namespace ZEngine::ECS::Systems
{
    // Builds a LightArrayUBO from all entities with LightComponent + TransformComponent
    // and stores it in RenderScene::PendingLights for upload by GraphicRenderer::DrawScene.
    // Called from Engine::MainThreadRun after the fixed-step loop, before PrepareScene.
    void SyncECSToLights(Scene& scene, Rendering::Scenes::RenderScene& render_scene);
} // namespace ZEngine::ECS::Systems
