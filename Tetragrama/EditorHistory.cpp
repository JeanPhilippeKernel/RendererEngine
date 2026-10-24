#include <Tetragrama/EditorHistory.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <limits>
#include <memory>

namespace Tetragrama
{
    namespace
    {
        constexpr size_t PayloadAlignment = EditorHistory::PayloadAlignment;

        size_t           AlignedBytes(size_t bytes)
        {
            return (bytes + PayloadAlignment - 1) & ~(PayloadAlignment - 1);
        }
    } // namespace

    bool EditorHistory::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, uint32_t max_entries, size_t payload_bytes)
    {
        if (!arena || m_entries || !max_entries || payload_bytes < PayloadAlignment || payload_bytes > std::numeric_limits<size_t>::max() / 2 || max_entries > 65536)
        {
            return false;
        }
        payload_bytes  &= ~(PayloadAlignment - 1);
        auto* entries   = static_cast<Entry*>(arena->AllocateNoZero(sizeof(Entry) * max_entries, alignof(Entry)));
        auto* payloads  = static_cast<std::byte*>(arena->AllocateNoZero(payload_bytes, PayloadAlignment));
        if (!entries || !payloads)
        {
            return false;
        }
        for (uint32_t i = 0; i < max_entries; ++i)
        {
            std::construct_at(entries + i);
        }
        m_entries        = entries;
        m_payloads       = payloads;
        m_entry_capacity = max_entries;
        m_byte_capacity  = payload_bytes;
        return true;
    }

    bool EditorHistory::CanStore(size_t payload_bytes) const
    {
        return m_entries && payload_bytes > 0 && payload_bytes <= m_byte_capacity;
    }

    bool EditorHistory::ValidOperation(const EditorOperation& operation)
    {
        return operation.Validate && operation.Apply && operation.Revert && operation.Destroy && operation.Name;
    }

    EditorHistory::Entry& EditorHistory::At(uint32_t index)
    {
        return m_entries[(m_first + index) % m_entry_capacity];
    }

    const EditorHistory::Entry& EditorHistory::At(uint32_t index) const
    {
        return m_entries[(m_first + index) % m_entry_capacity];
    }

    std::span<const std::byte> EditorHistory::Payload(const Entry& entry) const
    {
        return {m_payloads + entry.PayloadOffset, entry.PayloadBytes};
    }

    void EditorHistory::Destroy(Entry& entry)
    {
        entry.Operation.Destroy({m_payloads + entry.PayloadOffset, entry.PayloadBytes});
        entry = {};
    }

    void EditorHistory::RemoveLast()
    {
        auto& entry   = At(m_count - 1);
        m_tail        = entry.AllocationStart;
        m_bytes_used -= entry.AllocationBytes;
        Destroy(entry);
        --m_count;
        if (m_cursor > m_count)
        {
            m_cursor = m_count;
        }
        if (!m_count)
        {
            m_tail = 0;
        }
    }

    void EditorHistory::EvictFirst()
    {
        auto& entry   = At(0);
        m_bytes_used -= entry.AllocationBytes;
        Destroy(entry);
        m_first = (m_first + 1) % m_entry_capacity;
        --m_count;
        if (m_cursor)
        {
            --m_cursor;
        }
        if (!m_count)
        {
            m_tail = 0;
        }
    }

    bool EditorHistory::PushAlreadyApplied(const EditorOperation& operation, std::span<const std::byte> payload)
    {
        if (!ValidOperation(operation) || !CanStore(payload.size()) || !payload.data() || m_next_state == std::numeric_limits<uint64_t>::max())
        {
            return false;
        }

        // Release the abandoned redo branch first, reclaiming both metadata and byte storage.
        while (m_count > m_cursor)
        {
            RemoveLast();
        }

        const size_t bytes   = AlignedBytes(payload.size());
        size_t       padding = m_tail + bytes > m_byte_capacity ? m_byte_capacity - m_tail : 0;
        while (m_count && (m_count == m_entry_capacity || bytes + padding > m_byte_capacity - m_bytes_used))
        {
            EvictFirst();
            padding = m_tail + bytes > m_byte_capacity ? m_byte_capacity - m_tail : 0;
        }

        auto& entry           = At(m_count);
        entry.Operation       = operation;
        entry.AllocationStart = m_tail;
        entry.AllocationBytes = bytes + padding;
        entry.PayloadOffset   = padding ? 0 : m_tail;
        entry.PayloadBytes    = payload.size();
        entry.BeforeState     = m_current_state;
        entry.AfterState      = m_next_state++;
        // Aligned ring blocks are disjoint. Only the used semantic payload is copied.
        const int copied      = ZEngine::Helpers::secure_memcpy(m_payloads + entry.PayloadOffset, bytes, payload.data(), payload.size());
        ZENGINE_VALIDATE_ASSERT(copied == ZEngine::Helpers::MEMORY_OP_SUCCESS, "History payload copy failed")
        m_tail           = (entry.PayloadOffset + bytes) % m_byte_capacity;
        m_bytes_used    += entry.AllocationBytes;
        m_current_state  = entry.AfterState;
        ++m_count;
        m_cursor = m_count;
        return true;
    }

    bool EditorHistory::ExecuteAndPush(void* context, const EditorOperation& operation, std::span<const std::byte> payload)
    {
        if (!ValidOperation(operation) || !CanStore(payload.size()) || !payload.data() || !operation.Validate(context, payload, false))
        {
            return false;
        }
        // Copy/reserve before the mutation; Apply is infallible after complete validation.
        if (!PushAlreadyApplied(operation, payload))
        {
            return false;
        }
        const auto& entry = At(m_cursor - 1);
        entry.Operation.Apply(context, Payload(entry));
        return true;
    }

    bool EditorHistory::Undo(void* context)
    {
        if (!m_cursor)
        {
            return false;
        }
        auto&      entry   = At(m_cursor - 1);
        const auto payload = Payload(entry);
        if (!entry.Operation.Validate(context, payload, true))
        {
            return false;
        }
        entry.Operation.Revert(context, payload);
        m_current_state = entry.BeforeState;
        --m_cursor;
        return true;
    }

    bool EditorHistory::Redo(void* context)
    {
        if (m_cursor == m_count)
        {
            return false;
        }
        auto&      entry   = At(m_cursor);
        const auto payload = Payload(entry);
        if (!entry.Operation.Validate(context, payload, false))
        {
            return false;
        }
        entry.Operation.Apply(context, payload);
        m_current_state = entry.AfterState;
        ++m_cursor;
        return true;
    }

    void EditorHistory::ClearEntries()
    {
        while (m_count)
        {
            RemoveLast();
        }
        m_first      = 0;
        m_bytes_used = 0;
    }

    void EditorHistory::Reset(bool saved)
    {
        ClearEntries();
        m_current_state    = m_next_state++;
        m_saved_state      = m_current_state;
        m_checkpoint_valid = saved;
    }

    void EditorHistory::Invalidate()
    {
        Reset(false);
    }

    void EditorHistory::MarkSaved()
    {
        m_saved_state      = m_current_state;
        m_checkpoint_valid = true;
    }

    cstring EditorHistory::UndoName() const
    {
        return m_cursor ? At(m_cursor - 1).Operation.Name(Payload(At(m_cursor - 1))) : "";
    }

    cstring EditorHistory::RedoName() const
    {
        return m_cursor < m_count ? At(m_cursor).Operation.Name(Payload(At(m_cursor))) : "";
    }
} // namespace Tetragrama
