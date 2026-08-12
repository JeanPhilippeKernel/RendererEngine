#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/ArchetypeMask.h>
#include <ZEngine/ECS/EntityID.h>
#include <cstdint>

namespace ZEngine::ECS
{
    struct EntitySlot
    {
        uint32_t      Generation = 0; // 0 = never used / free
        ArchetypeMask Mask       = 0;
    };

    class EntityRegistry
    {
    public:
        static constexpr uint32_t MAX_ENTITIES = 65536;

        void                      Initialize(Core::Memory::ArenaAllocator* arena);

        EntityID                  Create();
        void                      Destroy(EntityID id);
        bool                      IsAlive(EntityID id) const;

        ArchetypeMask             GetMask(EntityID id) const;
        void                      SetMaskBit(EntityID id, ArchetypeMask bit);
        void                      ClearMaskBit(EntityID id, ArchetypeMask bit);

        uint32_t                  AliveCount() const
        {
            return m_alive_count;
        }
        uint32_t Capacity() const
        {
            return MAX_ENTITIES;
        }

        // Visit all live entities. Fn: void(EntityID)
        template <typename Fn>
        void ForEachAlive(Fn&& fn) const
        {
            for (uint32_t i = 0; i < static_cast<uint32_t>(m_slots.size()); ++i)
            {
                const EntitySlot& slot = m_slots[i];
                if (slot.Generation == 0)
                    continue;
                if ((slot.Generation & 1) != 0)
                    continue; // odd generation = free slot
                fn(EntityID{i, slot.Generation});
            }
        }

    private:
        Core::Containers::Array<EntitySlot> m_slots;
        Core::Containers::Array<uint32_t>   m_free_list;
        uint32_t                            m_alive_count = 0;
        uint32_t                            m_head        = 0; // high-water mark
    };
} // namespace ZEngine::ECS
