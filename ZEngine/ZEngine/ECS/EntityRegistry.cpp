#include <ZEngine/ECS/EntityRegistry.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::ECS
{
    void EntityRegistry::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        m_slots.init(arena, MAX_ENTITIES);
        m_free_list.init(arena, MAX_ENTITIES);
        m_alive_count = 0;
        m_head        = 0;
    }

    EntityID EntityRegistry::Create()
    {
        uint32_t index;

        if (!m_free_list.empty())
        {
            index = m_free_list[m_free_list.size() - 1];
            m_free_list.pop();

            EntitySlot& slot = m_slots[index];
            ZENGINE_VALIDATE_ASSERT((slot.Generation & 1) != 0, "EntityRegistry::Create: recycled slot was not freed")
            // Even generation = alive
            slot.Generation += 1; // odd+1 = even
            slot.Mask        = 0;
            ++m_alive_count;
            return EntityID{index, slot.Generation};
        }

        ZENGINE_VALIDATE_ASSERT(m_head < MAX_ENTITIES, "EntityRegistry::Create: MAX_ENTITIES capacity reached")
        index = m_head++;

        // Expand slot array if needed
        if (index >= static_cast<uint32_t>(m_slots.size()))
        {
            EntitySlot s{};
            m_slots.push(s);
        }

        EntitySlot& slot = m_slots[index];
        slot.Generation  = 2; // skip 0 (invalid) and 1 (free); start alive at 2
        slot.Mask        = 0;
        ++m_alive_count;
        return EntityID{index, slot.Generation};
    }

    void EntityRegistry::Destroy(EntityID id)
    {
        ZENGINE_VALIDATE_ASSERT(IsAlive(id), "EntityRegistry::Destroy: entity is not alive")

        EntitySlot& slot = m_slots[id.Index];
        // Increment to odd = free. Skip 0.
        slot.Generation  = (slot.Generation + 1 == 0) ? 1 : slot.Generation + 1;
        slot.Mask        = 0;
        m_free_list.push(id.Index);
        --m_alive_count;
    }

    bool EntityRegistry::IsAlive(EntityID id) const
    {
        if (!id.IsValid())
            return false;
        if (id.Index >= static_cast<uint32_t>(m_slots.size()))
            return false;
        const EntitySlot& slot = m_slots[id.Index];
        return (slot.Generation & 1) == 0 && slot.Generation == id.Generation;
    }

    ArchetypeMask EntityRegistry::GetMask(EntityID id) const
    {
        if (!IsAlive(id))
            return 0;
        return m_slots[id.Index].Mask;
    }

    void EntityRegistry::SetMaskBit(EntityID id, ArchetypeMask bit)
    {
        ZENGINE_VALIDATE_ASSERT(IsAlive(id), "EntityRegistry::SetMaskBit: entity is not alive")
        m_slots[id.Index].Mask |= bit;
    }

    void EntityRegistry::ClearMaskBit(EntityID id, ArchetypeMask bit)
    {
        ZENGINE_VALIDATE_ASSERT(IsAlive(id), "EntityRegistry::ClearMaskBit: entity is not alive")
        m_slots[id.Index].Mask &= ~bit;
    }
} // namespace ZEngine::ECS
