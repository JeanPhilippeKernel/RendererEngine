#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Systems/TransformSyncSystem.h>
#include <algorithm>
#include <array>

using ZEngine::ECS::Components::MeshComponent;
using ZEngine::ECS::Components::TransformComponent;

namespace ZEngine::ECS::Systems
{
    void SyncECSToRenderScene(Scene& scene, float alpha, Rendering::Scenes::RenderScene& render_scene)
    {
        // Runtime IDs can be sparse/reordered after editing and are not entity
        // indices. A bounded sorted join avoids two O(I) searches per ECS entity
        // without a heap allocation or a persistent cache that raw instance
        // edits could invalidate. Ordering by index preserves first-match
        // behavior for malformed duplicate IDs in legacy owner-thread callers.
        struct Entry
        {
            uint32_t Id    = 0;
            uint32_t Index = 0;
        };
        constexpr auto              capacity = Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS;
        std::array<Entry, capacity> entries  = {};
        const auto                  count    = render_scene.Instances.size();
        if (count <= entries.size())
        {
            for (uint32_t index = 0; index < count; ++index)
            {
                entries[index] = {render_scene.Instances[index].Id, index};
            }
            std::sort(entries.begin(), entries.begin() + count, [](const Entry& a, const Entry& b) { return a.Id < b.Id || (a.Id == b.Id && a.Index < b.Index); });
        }
        scene.ForEach<TransformComponent, MeshComponent>([&](EntityID entity, TransformComponent& tc, MeshComponent& mc) {
            if (mc.RenderInstanceId == UINT32_MAX)
            {
                return;
            }
            if (count <= entries.size())
            {
                const auto end   = entries.begin() + count;
                const auto found = std::lower_bound(entries.begin(), end, mc.RenderInstanceId, [](const Entry& entry, uint32_t id) { return entry.Id < id; });
                if (found != end && found->Id == mc.RenderInstanceId)
                {
                    [[maybe_unused]] const bool updated = render_scene.SetInstanceStateAtIndex(found->Index, tc.WorldTransform, entity);
                    ZENGINE_VALIDATE_ASSERT(updated, "Owner-thread scene join lost its instance index")
                }
                return;
            }
            // Hardened scene admission refuses over-cap input. Preserve the
            // unbounded legacy caller's behavior rather than truncating it.
            render_scene.SetInstanceTransform(mc.RenderInstanceId, tc.WorldTransform);
            render_scene.SetInstanceEntity(mc.RenderInstanceId, entity);
        });
    }
} // namespace ZEngine::ECS::Systems
