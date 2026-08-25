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

        scene.ForEach<TransformComponent, MeshComponent>([&](EntityID, TransformComponent& tc, MeshComponent& mc) {
            if (mc.RenderInstanceId == UINT32_MAX)
                return;
            render_scene.SetInstanceTransform(mc.RenderInstanceId, tc.WorldTransform);
        });
    }
} // namespace ZEngine::ECS::Systems
