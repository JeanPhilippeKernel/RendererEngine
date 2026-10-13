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
        // Roots include missing/invalid parent links. Treating those as roots keeps a
        // malformed or concurrently deleted parent from leaving a stale world matrix.
        scene.ForEach<TransformComponent>([&](EntityID id, TransformComponent& tc) {
            const ParentComponent* parent = scene.GetComponent<ParentComponent>(id);
            if (!parent || !parent->Parent.IsValid() || !scene.IsAlive(parent->Parent) || !scene.HasComponent<TransformComponent>(parent->Parent))
                tc.WorldTransform = ComposeTransformMatrix(tc.Position, tc.Rotation, tc.Scale);
        });

        // Resolve children top-down. Entity creation order is not hierarchy order, so
        // several passes may be needed. The bounded pass count prevents a malformed
        // parent cycle from turning the main loop into an infinite loop.
        const uint32_t max_passes = scene.AliveCount();
        for (uint32_t pass = 0; pass < max_passes; ++pass)
        {
            bool changed = false;
            scene.ForEach<TransformComponent, ParentComponent>([&](EntityID, TransformComponent& tc, ParentComponent& pc) {
                if (!pc.Parent.IsValid() || !scene.IsAlive(pc.Parent))
                    return;
                TransformComponent* parent_tc = scene.GetComponent<TransformComponent>(pc.Parent);
                if (!parent_tc)
                    return;
                Mat4f local   = ComposeTransformMatrix(tc.Position, tc.Rotation, tc.Scale);
                Mat4f world   = parent_tc->WorldTransform * local;
                bool  differs = false;
                for (size_t row = 0; row < 4 && !differs; ++row)
                {
                    for (size_t column = 0; column < 4; ++column)
                    {
                        if (world(row, column) != tc.WorldTransform(row, column))
                        {
                            differs = true;
                            break;
                        }
                    }
                }
                if (differs)
                {
                    tc.WorldTransform = world;
                    changed           = true;
                }
            });
            if (!changed)
                break;
        }
    }
} // namespace ZEngine::ECS::Systems
