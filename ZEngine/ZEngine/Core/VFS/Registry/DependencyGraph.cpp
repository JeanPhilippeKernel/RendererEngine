#include <ZEngine/Core/VFS/Registry/DependencyGraph.h>
#include <ZEngine/ZEngineDef.h>
#include <cstring>

namespace ZEngine::Core::VFS
{
    // AdjacencyList

    bool AdjacencyList::Contains(const uuids::uuid& uuid) const
    {
        for (uint32_t i = 0; i < InlineCount; ++i)
            if (InlineStorage[i] == uuid)
                return true;
        for (size_t i = 0; i < Overflow.size(); ++i)
            if (Overflow[i] == uuid)
                return true;
        return false;
    }

    bool AdjacencyList::Append(const uuids::uuid& uuid, Core::Memory::ArenaAllocator* arena)
    {
        if (Contains(uuid))
            return false;
        if (InlineCount < DEP_INLINE_CAPACITY)
        {
            InlineStorage[InlineCount++] = uuid;
            return true;
        }
        if (Overflow.m_allocator == nullptr)
            Overflow.init(arena, DEP_INLINE_CAPACITY);
        Overflow.push(uuid);
        return true;
    }

    bool AdjacencyList::Erase(const uuids::uuid& uuid)
    {
        // Check inline storage
        for (uint32_t i = 0; i < InlineCount; ++i)
        {
            if (InlineStorage[i] == uuid)
            {
                if (!Overflow.empty())
                {
                    InlineStorage[i] = Overflow[Overflow.size() - 1];
                    Overflow.pop();
                }
                else if (i != InlineCount - 1)
                {
                    InlineStorage[i] = InlineStorage[--InlineCount];
                }
                else
                {
                    --InlineCount;
                }
                return true;
            }
        }
        // Check overflow (swap-and-pop)
        for (size_t i = 0; i < Overflow.size(); ++i)
        {
            if (Overflow[i] == uuid)
            {
                Overflow[i] = Overflow[Overflow.size() - 1];
                Overflow.pop();
                return true;
            }
        }
        return false;
    }

    // DependencyGraph

    void DependencyGraph::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        m_arena = arena;
        m_forward.init(arena, 1024);
        m_reverse.init(arena, 1024);
    }

    bool DependencyGraph::AddEdge(const uuids::uuid& dependent, const uuids::uuid& dependency)
    {
        std::unique_lock lock(m_mutex);

        AdjacencyList*   fwd = m_forward.find(dependent);
        if (!fwd)
        {
            m_forward.insert(dependent, AdjacencyList{});
            fwd = m_forward.find(dependent);
        }
        if (!fwd->Append(dependency, m_arena))
            return false;

        AdjacencyList* rev = m_reverse.find(dependency);
        if (!rev)
        {
            m_reverse.insert(dependency, AdjacencyList{});
            rev = m_reverse.find(dependency);
        }
        rev->Append(dependent, m_arena);

        ++m_edges;
        return true;
    }

    bool DependencyGraph::RemoveEdge(const uuids::uuid& dependent, const uuids::uuid& dependency)
    {
        std::unique_lock lock(m_mutex);

        AdjacencyList*   fwd = m_forward.find(dependent);
        if (!fwd || !fwd->Erase(dependency))
            return false;

        AdjacencyList* rev = m_reverse.find(dependency);
        if (rev)
            rev->Erase(dependent);

        --m_edges;
        return true;
    }

    void DependencyGraph::RemoveAsset(const uuids::uuid& uuid)
    {
        std::unique_lock lock(m_mutex);

        // Forward cleanup: for each dep that uuid depends on, remove uuid from its reverse list
        AdjacencyList*   fwd = m_forward.find(uuid);
        if (fwd)
        {
            uint32_t count = fwd->Count();
            for (uint32_t i = 0; i < count; ++i)
            {
                AdjacencyList* rev = m_reverse.find((*fwd)[i]);
                if (rev)
                    rev->Erase(uuid);
                --m_edges;
            }
            m_forward.remove(uuid);
        }

        // Reverse cleanup: for each asset that depends on uuid, remove uuid from its forward list
        AdjacencyList* rev = m_reverse.find(uuid);
        if (rev)
        {
            uint32_t count = rev->Count();
            for (uint32_t i = 0; i < count; ++i)
            {
                AdjacencyList* fwd2 = m_forward.find((*rev)[i]);
                if (fwd2)
                    fwd2->Erase(uuid);
                --m_edges;
            }
            m_reverse.remove(uuid);
        }
    }

    void DependencyGraph::CopyDependencies(const uuids::uuid& uuid, Core::Containers::Array<uuids::uuid>& out) const
    {
        std::shared_lock     lock(m_mutex);
        const AdjacencyList* fwd = m_forward.find(uuid);
        if (!fwd)
            return;
        for (uint32_t i = 0; i < fwd->Count(); ++i)
            out.push((*fwd)[i]);
    }

    void DependencyGraph::CopyDependents(const uuids::uuid& uuid, Core::Containers::Array<uuids::uuid>& out) const
    {
        std::shared_lock     lock(m_mutex);
        const AdjacencyList* rev = m_reverse.find(uuid);
        if (!rev)
            return;
        for (uint32_t i = 0; i < rev->Count(); ++i)
            out.push((*rev)[i]);
    }

    bool DependencyGraph::HasEdge(const uuids::uuid& dependent, const uuids::uuid& dependency) const
    {
        std::shared_lock     lock(m_mutex);
        const AdjacencyList* fwd = m_forward.find(dependent);
        return fwd && fwd->Contains(dependency);
    }

    void DependencyGraph::CollectCascade(const uuids::uuid& changed, Core::Containers::Array<uuids::uuid>& out, Core::Memory::ArenaAllocator* scratch) const
    {
        // Inline open-addressing visited set backed by scratch arena.
        uint32_t     visited_cap   = 256;
        uint32_t     visited_count = 0;
        uuids::uuid* visited       = ZPushArray(scratch, uuids::uuid, visited_cap);
        std::memset(visited, 0, visited_cap * sizeof(uuids::uuid));

        auto visited_rehash = [&]() {
            uint32_t     new_cap   = visited_cap * 2;
            uuids::uuid* new_table = ZPushArray(scratch, uuids::uuid, new_cap);
            std::memset(new_table, 0, new_cap * sizeof(uuids::uuid));
            for (uint32_t i = 0; i < visited_cap; ++i)
            {
                if (visited[i].is_nil())
                    continue;
                uint64_t h = UUIDHasher{}(visited[i]) % new_cap;
                for (uint32_t p = 0; p < new_cap; ++p)
                {
                    uint64_t slot = (h + p) % new_cap;
                    if (new_table[slot].is_nil())
                    {
                        new_table[slot] = visited[i];
                        break;
                    }
                }
            }
            visited     = new_table;
            visited_cap = new_cap;
        };

        auto visited_contains = [&](const uuids::uuid& id) -> bool {
            uint64_t h = UUIDHasher{}(id) % visited_cap;
            for (uint32_t p = 0; p < visited_cap; ++p)
            {
                uint64_t slot = (h + p) % visited_cap;
                if (visited[slot].is_nil())
                    return false;
                if (visited[slot] == id)
                    return true;
            }
            return false;
        };

        auto visited_insert = [&](const uuids::uuid& id) {
            if (visited_count >= visited_cap * 2 / 3)
                visited_rehash();
            uint64_t h = UUIDHasher{}(id) % visited_cap;
            for (uint32_t p = 0; p < visited_cap; ++p)
            {
                uint64_t slot = (h + p) % visited_cap;
                if (visited[slot].is_nil() || visited[slot] == id)
                {
                    visited[slot] = id;
                    ++visited_count;
                    return;
                }
            }
        };

        Core::Containers::Array<uuids::uuid> queue;
        queue.init(scratch, 64);

        out.push(changed);
        visited_insert(changed);
        queue.push(changed);

        std::shared_lock lock(m_mutex);

        while (!queue.empty())
        {
            uuids::uuid current = queue[queue.size() - 1];
            queue.pop();

            const AdjacencyList* rev = m_reverse.find(current);
            if (!rev)
                continue;

            for (uint32_t i = 0; i < rev->Count(); ++i)
            {
                const uuids::uuid& dep = (*rev)[i];
                if (!visited_contains(dep))
                {
                    visited_insert(dep);
                    out.push(dep);
                    queue.push(dep);
                }
            }
        }
    }

    uint32_t DependencyGraph::EdgeCount() const
    {
        std::shared_lock lock(m_mutex);
        return m_edges;
    }

    uint32_t DependencyGraph::NodeCount() const
    {
        std::shared_lock lock(m_mutex);
        return static_cast<uint32_t>(m_forward.size() + m_reverse.size());
    }

} // namespace ZEngine::Core::VFS
