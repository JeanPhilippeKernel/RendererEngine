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
        struct ComponentOwner
        {
            EntityID Entity   = {};
            uint64_t Lifetime = 0;
        };
        static_assert(sizeof(ComponentOwner) == sizeof(EntityID) + sizeof(uint64_t));

    public:
        static constexpr uint32_t INVALID_DENSE = UINT32_MAX;

        /// Conservative arena bound for fresh storage plus geometric dense growth. Used before
        /// reconstructing an unpublished document, not as a serialization/native-layout format.
        static size_t             AllocationUpperBound(uint32_t count, uint32_t entity_capacity)
        {
            if (!count)
            {
                return 0;
            }
            size_t capacity = 256;
            while (capacity < count)
            {
                capacity *= 2;
            }
            return sizeof(ComponentStorage<T>) + size_t(entity_capacity) * sizeof(uint32_t) + 2 * capacity * (sizeof(T) + sizeof(ComponentOwner)) + 16 * (alignof(T) + alignof(ComponentOwner) + DEFAULT_ALIGNMENT);
        }

        void Initialize(Core::Memory::ArenaAllocator* arena, uint32_t entity_capacity)
        {
            m_sparse.init(arena, entity_capacity);
            // Fill sparse with INVALID_DENSE — entity_capacity entries
            for (uint32_t i = 0; i < entity_capacity; ++i)
            {
                m_sparse.push(INVALID_DENSE);
            }

            // Dense arrays start empty; reserve reasonable initial capacity
            m_dense.init(arena, 256);
            m_dense_owners.init(arena, 256);
        }

        /// Upper bound for one append to existing storage. Dense growth may
        /// allocate a full replacement block; no sparse allocation is needed.
        size_t AdditionMemoryBound() const
        {
            size_t bytes = 0;
            if (m_dense.size() == m_dense.capacity())
            {
                bytes += (m_dense.capacity() ? 2 * m_dense.capacity() : 4) * sizeof(T) + alignof(T);
            }
            if (m_dense_owners.size() == m_dense_owners.capacity())
            {
                bytes += (m_dense_owners.capacity() ? 2 * m_dense_owners.capacity() : 4) * sizeof(ComponentOwner) + alignof(ComponentOwner);
            }
            return bytes;
        }

        void Add(EntityID id, T component)
        {
            ZENGINE_VALIDATE_ASSERT(id.IsValid(), "ComponentStorage::Add: invalid EntityID")
            ZENGINE_VALIDATE_ASSERT(id.Index < static_cast<uint32_t>(m_sparse.size()), "ComponentStorage::Add: entity Index out of sparse range")
            ZENGINE_VALIDATE_ASSERT(m_sparse[id.Index] == INVALID_DENSE, "ComponentStorage::Add: entity already has this component")

            uint32_t dense_idx = static_cast<uint32_t>(m_dense.size());
            m_sparse[id.Index] = dense_idx;
            m_dense.push(static_cast<T&&>(component));
            if (++m_lifetime == 0)
            {
                ++m_lifetime;
            }
            m_dense_owners.push({id, m_lifetime});
        }

        T* Get(EntityID id)
        {
            if (!id.IsValid() || id.Index >= static_cast<uint32_t>(m_sparse.size()))
            {
                return nullptr;
            }
            uint32_t dense_idx = m_sparse[id.Index];
            if (dense_idx == INVALID_DENSE)
            {
                return nullptr;
            }
            if (m_dense_owners[dense_idx].Entity != id)
            {
                return nullptr; // stale generation
            }
            return &m_dense[dense_idx];
        }

        const T* Get(EntityID id) const
        {
            if (!id.IsValid() || id.Index >= static_cast<uint32_t>(m_sparse.size()))
            {
                return nullptr;
            }
            uint32_t dense_idx = m_sparse[id.Index];
            if (dense_idx == INVALID_DENSE)
            {
                return nullptr;
            }
            if (m_dense_owners[dense_idx].Entity != id)
            {
                return nullptr;
            }
            return &m_dense[dense_idx];
        }

        bool Has(EntityID id) const
        {
            if (!id.IsValid() || id.Index >= static_cast<uint32_t>(m_sparse.size()))
            {
                return false;
            }
            uint32_t dense_idx = m_sparse[id.Index];
            if (dense_idx == INVALID_DENSE)
            {
                return false;
            }
            return m_dense_owners[dense_idx].Entity == id;
        }

        /// Nonzero identity of this component instance, not its address/value.
        /// Dense growth and swap-and-pop preserve it; remove/add never does.
        uint64_t Lifetime(EntityID id) const
        {
            return Has(id) ? m_dense_owners[m_sparse[id.Index]].Lifetime : 0;
        }

        void Remove(EntityID id)
        {
            RemoveRaw(id);
        }

        // IComponentStorage
        void RemoveRaw(EntityID id) override
        {
            if (!id.IsValid() || id.Index >= static_cast<uint32_t>(m_sparse.size()))
            {
                return;
            }
            uint32_t dense_idx = m_sparse[id.Index];
            if (dense_idx == INVALID_DENSE)
            {
                return;
            }
            if (m_dense_owners[dense_idx].Entity != id)
            {
                return; // stale generation
            }

            // Swap-and-pop: replace with last element, update sparse for the moved entity
            uint32_t last_dense = static_cast<uint32_t>(m_dense.size()) - 1;
            if (dense_idx != last_dense)
            {
                m_dense[dense_idx]                               = static_cast<T&&>(m_dense[last_dense]);
                m_dense_owners[dense_idx]                        = m_dense_owners[last_dense];
                m_sparse[m_dense_owners[dense_idx].Entity.Index] = dense_idx;
            }
            m_dense.pop();
            m_dense_owners.pop();
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
            {
                fn(m_dense_owners[i].Entity, m_dense[i]);
            }
        }

        void AddRaw(EntityID id) override
        {
            if (!id.IsValid() || Has(id))
            {
                return;
            }
            Add(id, T{});
        }

        void* GetRaw(EntityID id) override
        {
            return Has(id) ? static_cast<void*>(Get(id)) : nullptr;
        }
        const void* GetRaw(EntityID id) const override
        {
            return Has(id) ? static_cast<const void*>(Get(id)) : nullptr;
        }

    private:
        Core::Containers::Array<uint32_t>       m_sparse       = {}; // entity Index → dense index
        Core::Containers::Array<T>              m_dense        = {}; // packed component data
        Core::Containers::Array<ComponentOwner> m_dense_owners = {}; // generational entity + component lifetime
        uint64_t                                m_lifetime     = 0;
    };
} // namespace ZEngine::ECS
