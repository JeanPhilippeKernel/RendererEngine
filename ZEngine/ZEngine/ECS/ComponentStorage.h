#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/EntityID.h>
#include <ZEngine/ECS/IComponentStorage.h>
#include <ZEngine/ZEngineDef.h>
#include <cstdint>
#include <cstring>

namespace ZEngine::ECS
{
    template <typename T>
    class ComponentStorage final : public IComponentStorage
    {
    public:
        static constexpr uint32_t INVALID_DENSE = UINT32_MAX;

        void                      Initialize(Core::Memory::ArenaAllocator* arena, uint32_t entity_capacity)
        {
            m_sparse.init(arena, entity_capacity);
            // Fill sparse with INVALID_DENSE — entity_capacity entries
            for (uint32_t i = 0; i < entity_capacity; ++i)
                m_sparse.push(INVALID_DENSE);

            // Dense arrays start empty; reserve reasonable initial capacity
            m_dense.init(arena, 256);
            m_dense_ids.init(arena, 256);
        }

        void Add(EntityID id, T component)
        {
            ZENGINE_VALIDATE_ASSERT(id.IsValid(), "ComponentStorage::Add: invalid EntityID")
            ZENGINE_VALIDATE_ASSERT(id.Index < static_cast<uint32_t>(m_sparse.size()), "ComponentStorage::Add: entity Index out of sparse range")
            ZENGINE_VALIDATE_ASSERT(m_sparse[id.Index] == INVALID_DENSE, "ComponentStorage::Add: entity already has this component")

            uint32_t dense_idx = static_cast<uint32_t>(m_dense.size());
            m_sparse[id.Index] = dense_idx;
            m_dense.push(static_cast<T&&>(component));
            m_dense_ids.push(id);
        }

        T* Get(EntityID id)
        {
            if (!id.IsValid() || id.Index >= static_cast<uint32_t>(m_sparse.size()))
                return nullptr;
            uint32_t dense_idx = m_sparse[id.Index];
            if (dense_idx == INVALID_DENSE)
                return nullptr;
            if (m_dense_ids[dense_idx] != id)
                return nullptr; // stale generation
            return &m_dense[dense_idx];
        }

        const T* Get(EntityID id) const
        {
            if (!id.IsValid() || id.Index >= static_cast<uint32_t>(m_sparse.size()))
                return nullptr;
            uint32_t dense_idx = m_sparse[id.Index];
            if (dense_idx == INVALID_DENSE)
                return nullptr;
            if (m_dense_ids[dense_idx] != id)
                return nullptr;
            return &m_dense[dense_idx];
        }

        bool Has(EntityID id) const
        {
            if (!id.IsValid() || id.Index >= static_cast<uint32_t>(m_sparse.size()))
                return false;
            uint32_t dense_idx = m_sparse[id.Index];
            if (dense_idx == INVALID_DENSE)
                return false;
            return m_dense_ids[dense_idx] == id;
        }

        void Remove(EntityID id)
        {
            RemoveRaw(id);
        }

        // IComponentStorage
        void RemoveRaw(EntityID id) override
        {
            if (!id.IsValid() || id.Index >= static_cast<uint32_t>(m_sparse.size()))
                return;
            uint32_t dense_idx = m_sparse[id.Index];
            if (dense_idx == INVALID_DENSE)
                return;
            if (m_dense_ids[dense_idx] != id)
                return; // stale generation

            // Swap-and-pop: replace with last element, update sparse for the moved entity
            uint32_t last_dense = static_cast<uint32_t>(m_dense.size()) - 1;
            if (dense_idx != last_dense)
            {
                m_dense[dense_idx]                     = static_cast<T&&>(m_dense[last_dense]);
                m_dense_ids[dense_idx]                 = m_dense_ids[last_dense];
                m_sparse[m_dense_ids[dense_idx].Index] = dense_idx;
            }
            m_dense.pop();
            m_dense_ids.pop();
            m_sparse[id.Index] = INVALID_DENSE;
        }

        bool HasRaw(EntityID id) const override
        {
            return Has(id);
        }

        uint32_t Count() const
        {
            return static_cast<uint32_t>(m_dense.size());
        }

        // Iterate all live components. Fn: void(EntityID, T&)
        template <typename Fn>
        void ForEach(Fn&& fn)
        {
            for (size_t i = 0; i < m_dense.size(); ++i)
                fn(m_dense_ids[i], m_dense[i]);
        }

    private:
        Core::Containers::Array<uint32_t> m_sparse;    // entity Index → dense index
        Core::Containers::Array<T>        m_dense;     // packed component data
        Core::Containers::Array<EntityID> m_dense_ids; // EntityID at each dense slot (for generation check)
    };
} // namespace ZEngine::ECS
