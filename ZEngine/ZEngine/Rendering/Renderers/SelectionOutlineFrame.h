#pragma once
#include <ZEngine/ECS/EntityID.h>
#include <algorithm>
#include <cstdint>

namespace ZEngine::Rendering::Renderers
{
    /// Immutable editor feedback snapshot carried by a render mailbox slot.
    /// It keeps entity identity opaque to the renderer and uses fixed storage so
    /// selection changes never allocate on the producer or render thread.
    struct SelectionOutlineFrame
    {
        static constexpr uint32_t MaxEntities           = 4096;

        ECS::EntityID             Entities[MaxEntities] = {};
        ECS::EntityID             Hovered               = {};
        uint32_t                  EntityCount           = 0;

        [[nodiscard]] bool        Enabled() const
        {
            return EntityCount != 0 || Hovered.IsValid();
        }

        [[nodiscard]] bool Add(ECS::EntityID entity)
        {
            if (!entity.IsValid() || EntityCount >= MaxEntities)
            {
                return false;
            }
            Entities[EntityCount++] = entity;
            return true;
        }

        void SortAndUnique()
        {
            std::sort(Entities, Entities + EntityCount, Less);
            const auto* const end = std::unique(Entities, Entities + EntityCount);
            EntityCount           = static_cast<uint32_t>(end - Entities);
        }

        [[nodiscard]] bool Contains(ECS::EntityID entity) const
        {
            if (!entity.IsValid() || EntityCount == 0)
            {
                return false;
            }
            return std::binary_search(Entities, Entities + EntityCount, entity, Less);
        }

    private:
        static bool Less(const ECS::EntityID& left, const ECS::EntityID& right)
        {
            return left.Index != right.Index ? left.Index < right.Index : left.Generation < right.Generation;
        }
    };
} // namespace ZEngine::Rendering::Renderers
