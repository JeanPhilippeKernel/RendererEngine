#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Systems/TransformSyncSystem.h>

namespace ZEngine::ECS::Systems
{
    void SyncECSToRenderScene(Scene& scene, float alpha, Rendering::Scenes::RenderScene& render_scene)
    {
        using namespace Components;
        using namespace Core::Maths;

        // Use Position directly — interpolation between PreviousPosition and Position
        // is deferred until fixed-timestep physics/simulation systems are active.
        // Applying alpha here conflicts with immediate gizmo-driven position updates.
        scene.ForEach<TransformComponent, MeshComponent>([&](EntityID, TransformComponent& tc, MeshComponent& mc) {
            if (mc.RenderInstanceId == UINT32_MAX)
                return;
            Mat4f mat = ComposeTransformMatrix(tc.Position, tc.Rotation, tc.Scale);
            render_scene.SetInstanceTransform(mc.RenderInstanceId, mat);
        });
    }
} // namespace ZEngine::ECS::Systems
