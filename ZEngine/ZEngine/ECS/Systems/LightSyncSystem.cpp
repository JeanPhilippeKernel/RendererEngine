#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Systems/LightSyncSystem.h>
#include <cmath>

using namespace ZEngine::ECS::Components;
using namespace ZEngine::Rendering::Scenes;
using namespace ZEngine::Core::Maths;

namespace ZEngine::ECS::Systems
{
    void SyncECSToLights(Scene& scene, Rendering::Scenes::RenderScene& render_scene)
    {
        LightArrayUBO     lights          = {};
        SkyCelestialLight celestial_light = {};

        scene.ForEach<TransformComponent, LightComponent>([&](EntityID entity, TransformComponent& tc, LightComponent& lc) {
            if (lc.LightType == LightComponent::Type::Directional)
            {
                // Forward direction = column 2 of WorldTransform (already includes parent rotation).
                const float forward_x = tc.WorldTransform(0, 2);
                const float forward_y = tc.WorldTransform(1, 2);
                const float forward_z = tc.WorldTransform(2, 2);
                if (lights.DirectionalCount < 4)
                {
                    auto& dir       = lights.DirectionalLights[lights.DirectionalCount++];
                    dir.Direction.x = forward_x;
                    dir.Direction.y = forward_y;
                    dir.Direction.z = forward_z;
                    dir.Direction.w = 0.f;
                    dir.Color.x     = lc.Color[0];
                    dir.Color.y     = lc.Color[1];
                    dir.Color.z     = lc.Color[2];
                    dir.Color.w     = 1.f;
                    dir.Intensity   = lc.Intensity;
                }

                const UUIDComponent* identity = scene.GetComponent<UUIDComponent>(entity);
                if (render_scene.Sky.PrimaryCelestialLight.is_nil() || !identity || identity->Value != render_scene.Sky.PrimaryCelestialLight)
                    return;

                const float length_squared = forward_x * forward_x + forward_y * forward_y + forward_z * forward_z;
                if (!std::isfinite(length_squared) || length_squared <= 1.0e-8f)
                    return;

                const float reciprocal_length       = 1.0f / std::sqrt(length_squared);
                celestial_light.DirectionToLight[0] = -forward_x * reciprocal_length;
                celestial_light.DirectionToLight[1] = -forward_y * reciprocal_length;
                celestial_light.DirectionToLight[2] = -forward_z * reciprocal_length;
                celestial_light.IsAvailable         = true;
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
        if (!render_scene.CelestialLight.Matches(celestial_light))
        {
            render_scene.CelestialLight = celestial_light;
            render_scene.MarkSkyDirty();
        }
    }
} // namespace ZEngine::ECS::Systems
