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
            Vec3f pos = tc.PreviousPosition + (tc.Position - tc.PreviousPosition) * alpha;
            Mat4f mat = ComposeTransformMatrix(pos, tc.Rotation, tc.Scale);
            render_scene.SetInstanceTransform(mc.RenderInstanceId, mat);
        });
    }
} // namespace ZEngine::ECS::Systems
