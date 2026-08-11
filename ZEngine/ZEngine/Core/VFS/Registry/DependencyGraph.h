#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/Registry/AssetIndex.h> // UUIDHasher
#include <uuid.h>
#include <shared_mutex>

namespace ZEngine::Core::VFS
{
    constexpr uint32_t DEP_INLINE_CAPACITY = 8;

    struct AdjacencyList
    {
        uuids::uuid                          InlineStorage[DEP_INLINE_CAPACITY] = {};
        uint32_t                             InlineCount                        = 0;
        Core::Containers::Array<uuids::uuid> Overflow                           = {};

        uint32_t                             Count() const
        {
            return InlineCount + static_cast<uint32_t>(Overflow.size());
        }
        bool Empty() const
        {
            return Count() == 0;
        }

        uuids::uuid operator[](uint32_t i) const
        {
            if (i < InlineCount)
                return InlineStorage[i];
            return Overflow[i - InlineCount];
        }

        bool Contains(const uuids::uuid& uuid) const;
        bool Append(const uuids::uuid& uuid, Core::Memory::ArenaAllocator* arena);
        bool Erase(const uuids::uuid& uuid);
    };

    class DependencyGraph
    {
    public:
        DependencyGraph()  = default;
        ~DependencyGraph() = default;

        void     Initialize(Core::Memory::ArenaAllocator* arena);

        // AddEdge(dependent, dependency): dependent uses dependency.
        bool     AddEdge(const uuids::uuid& dependent, const uuids::uuid& dependency);

        bool     RemoveEdge(const uuids::uuid& dependent, const uuids::uuid& dependency);

        // Removes all edges involving uuid (both directions).
        void     RemoveAsset(const uuids::uuid& uuid);

        void     CopyDependencies(const uuids::uuid& uuid, Core::Containers::Array<uuids::uuid>& out) const;
        void     CopyDependents(const uuids::uuid& uuid, Core::Containers::Array<uuids::uuid>& out) const;
        bool     HasEdge(const uuids::uuid& dependent, const uuids::uuid& dependency) const;

        // BFS over reverse edges. `changed` is at index 0. Cycle-safe.
        void     CollectCascade(const uuids::uuid& changed, Core::Containers::Array<uuids::uuid>& out, Core::Memory::ArenaAllocator* scratch) const;

        uint32_t EdgeCount() const;
        uint32_t NodeCount() const;

    private:
        // m_forward[A] = assets A depends on
        // m_reverse[B] = assets that depend on B
        Core::Containers::UnorderedHashMap<uuids::uuid, AdjacencyList> m_forward = {};
        Core::Containers::UnorderedHashMap<uuids::uuid, AdjacencyList> m_reverse = {};

        mutable std::shared_mutex                                      m_mutex   = {};
        Core::Memory::ArenaAllocator*                                  m_arena   = nullptr;
        uint32_t                                                       m_edges   = 0;
    };

} // namespace ZEngine::Core::VFS
