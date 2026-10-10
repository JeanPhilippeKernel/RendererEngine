#include <ZEngine/Importers/ImportQueue.h>

namespace ZEngine::Importers
{
    void ImportQueue::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        m_heap.init(arena, 256);
        m_index.init(arena, 256);
    }

    void ImportQueue::SwapAt(uint32_t a, uint32_t b)
    {
        ImportJob tmp                  = m_heap[a];
        m_heap[a]                      = m_heap[b];
        m_heap[b]                      = tmp;
        m_index[m_heap[a].Path.Hash()] = a;
        m_index[m_heap[b].Path.Hash()] = b;
    }

    void ImportQueue::SiftUp(uint32_t pos)
    {
        while (pos > 0)
        {
            uint32_t parent = (pos - 1) / 2;
            if (static_cast<uint8_t>(m_heap[pos].Priority) <= static_cast<uint8_t>(m_heap[parent].Priority))
                break;
            SwapAt(pos, parent);
            pos = parent;
        }
    }

    void ImportQueue::SiftDown(uint32_t pos)
    {
        uint32_t n = static_cast<uint32_t>(m_heap.size());
        while (true)
        {
            uint32_t largest = pos;
            uint32_t left    = 2 * pos + 1;
            uint32_t right   = 2 * pos + 2;
            if (left<n&& static_cast<uint8_t>(m_heap[left].Priority)> static_cast<uint8_t>(m_heap[largest].Priority))
                largest = left;
            if (right<n&& static_cast<uint8_t>(m_heap[right].Priority)> static_cast<uint8_t>(m_heap[largest].Priority))
                largest = right;
            if (largest == pos)
                break;
            SwapAt(pos, largest);
            pos = largest;
        }
    }

    void ImportQueue::Enqueue(ImportJob job)
    {
        std::lock_guard lock(m_mutex);

        uint64_t        hash     = job.Path.Hash();
        uint32_t*       existing = m_index.find(hash);

        if (existing)
        {
            // Upgrade priority if higher, otherwise no-op
            if (static_cast<uint8_t>(job.Priority) > static_cast<uint8_t>(m_heap[*existing].Priority))
            {
                m_heap[*existing].Priority = job.Priority;
                SiftUp(*existing);
            }
            return;
        }

        uint32_t pos = static_cast<uint32_t>(m_heap.size());
        m_heap.push(job);
        m_index.insert(hash, pos);
        SiftUp(pos);
    }

    bool ImportQueue::TryPop(ImportJob& out_job)
    {
        std::lock_guard lock(m_mutex);
        if (m_heap.empty())
            return false;

        out_job = m_heap[0];
        m_index.remove(out_job.Path.Hash());

        uint32_t n = static_cast<uint32_t>(m_heap.size());
        if (n > 1)
        {
            m_heap[0] = m_heap[n - 1];
            m_heap.pop();
            m_index[m_heap[0].Path.Hash()] = 0;
            SiftDown(0);
        }
        else
        {
            m_heap.pop();
        }
        return true;
    }

    bool ImportQueue::IsEmpty() const
    {
        std::lock_guard lock(m_mutex);
        return m_heap.empty();
    }

    uint32_t ImportQueue::Size() const
    {
        std::lock_guard lock(m_mutex);
        return static_cast<uint32_t>(m_heap.size());
    }

    bool ImportQueue::Contains(const Core::VFS::VFSPath& path) const
    {
        std::lock_guard lock(m_mutex);
        return m_index.contains(path.Hash());
    }
} // namespace ZEngine::Importers
