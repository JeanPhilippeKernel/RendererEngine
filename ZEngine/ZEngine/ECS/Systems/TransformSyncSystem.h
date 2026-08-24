#pragma once
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>

namespace ZEngine::ECS::Systems
{
    // Propagates ECS TransformComponent + MeshComponent into RenderScene each frame.
    // Uses alpha for fixed-timestep position interpolation (PreviousPosition → Position).
    // Called from Engine::MainThreadRun after the fixed-step loop, before PrepareScene.
    void SyncECSToRenderScene(Scene& scene, float alpha, Rendering::Scenes::RenderScene& render_scene);
} // namespace ZEngine::ECS::Systems
