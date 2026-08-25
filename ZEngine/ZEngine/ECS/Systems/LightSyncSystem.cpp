#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Systems/LightSyncSystem.h>

using namespace ZEngine::ECS::Components;
using namespace ZEngine::Rendering::Scenes;
using namespace ZEngine::Core::Maths;

namespace ZEngine::ECS::Systems
{
    void SyncECSToLights(Scene& scene, Rendering::Scenes::RenderScene& render_scene)
    {
        LightArrayUBO lights = {};

        scene.ForEach<TransformComponent, LightComponent>([&](EntityID, TransformComponent& tc, LightComponent& lc) {
            if (lc.LightType == LightComponent::Type::Directional && lights.DirectionalCount < 4)
            {
                // Forward direction = column 2 of WorldTransform (already includes parent rotation).
                auto& dir       = lights.DirectionalLights[lights.DirectionalCount++];
                dir.Direction.x = tc.WorldTransform(0, 2);
                dir.Direction.y = tc.WorldTransform(1, 2);
                dir.Direction.z = tc.WorldTransform(2, 2);
                dir.Direction.w = 0.f;
                dir.Color.x     = lc.Color[0];
                dir.Color.y     = lc.Color[1];
                dir.Color.z     = lc.Color[2];
                dir.Color.w     = 1.f;
                dir.Intensity   = lc.Intensity;
            }
            else if (lc.LightType == LightComponent::Type::Point && lights.PointCount < 8)
            {
                // World position = translation column of WorldTransform.
                auto& pt      = lights.PointLights[lights.PointCount++];
                pt.Position.x = tc.WorldTransform(0, 3);
                pt.Position.y = tc.WorldTransform(1, 3);
                pt.Position.z = tc.WorldTransform(2, 3);
                pt.Position.w = 1.f;
                pt.Color.x    = lc.Color[0];
                pt.Color.y    = lc.Color[1];
                pt.Color.z    = lc.Color[2];
                pt.Color.w    = 1.f;
                pt.Intensity  = lc.Intensity;
                pt.Radius     = lc.Range;
            }
        });

        render_scene.PendingLights = lights;
    }
} // namespace ZEngine::ECS::Systems
