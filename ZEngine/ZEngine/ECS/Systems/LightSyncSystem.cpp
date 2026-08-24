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
                // Build rotation-only matrix and extract forward direction (column 2).
                // ComposeTransformMatrix uses YXZ Euler order; column 2 = (-sy, sx*cy, cx*cy).
                Mat4f rot       = ComposeTransformMatrix(Vec3f(0.f, 0.f, 0.f), tc.Rotation, Vec3f(1.f, 1.f, 1.f));

                auto& dir       = lights.DirectionalLights[lights.DirectionalCount++];
                dir.Direction.x = rot(0, 2);
                dir.Direction.y = rot(1, 2);
                dir.Direction.z = rot(2, 2);
                dir.Direction.w = 0.f;
                dir.Color.x     = lc.Color[0];
                dir.Color.y     = lc.Color[1];
                dir.Color.z     = lc.Color[2];
                dir.Color.w     = 1.f;
                dir.Intensity   = lc.Intensity;
            }
            else if (lc.LightType == LightComponent::Type::Point && lights.PointCount < 8)
            {
                auto& pt      = lights.PointLights[lights.PointCount++];
                pt.Position.x = tc.Position.x;
                pt.Position.y = tc.Position.y;
                pt.Position.z = tc.Position.z;
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
