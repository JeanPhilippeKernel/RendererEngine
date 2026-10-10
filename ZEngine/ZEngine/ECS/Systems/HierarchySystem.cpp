#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Systems/HierarchySystem.h>

using namespace ZEngine::Core::Maths;
using namespace ZEngine::ECS::Components;

namespace ZEngine::ECS::Systems
{
    void SyncHierarchy(ECS::Scene& scene)
    {
        // Pass 1 — roots: WorldTransform = local matrix
        scene.ForEach<TransformComponent>([&](EntityID id, TransformComponent& tc) {
            if (scene.HasComponent<ParentComponent>(id))
                return;
            tc.WorldTransform = ComposeTransformMatrix(tc.Position, tc.Rotation, tc.Scale);
        });

        // Pass 2+ — children: WorldTransform = parent.World × local
        // Repeat until no change to handle arbitrary nesting depth.
        bool changed = true;
        while (changed)
        {
            changed = false;
            scene.ForEach<TransformComponent, ParentComponent>([&](EntityID, TransformComponent& tc, ParentComponent& pc) {
                if (pc.Parent == INVALID_ENTITY)
                    return;
                TransformComponent* parent_tc = scene.GetComponent<TransformComponent>(pc.Parent);
                if (!parent_tc)
                    return;
                Mat4f local = ComposeTransformMatrix(tc.Position, tc.Rotation, tc.Scale);
                Mat4f world = parent_tc->WorldTransform * local;
                if (world(0, 0) != tc.WorldTransform(0, 0) || world(0, 3) != tc.WorldTransform(0, 3) || world(1, 3) != tc.WorldTransform(1, 3) || world(2, 3) != tc.WorldTransform(2, 3))
                {
                    tc.WorldTransform = world;
                    changed           = true;
                }
            });
        }
    }
} // namespace ZEngine::ECS::Systems
