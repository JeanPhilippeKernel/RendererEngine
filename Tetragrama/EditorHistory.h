#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <cstddef>
#include <span>

namespace Tetragrama
{
    /// @brief Data-only semantic operations. Callbacks must not throw or mutate during validation.
    /// Apply/Revert run only after the entire operation validates, in the session mutation phase.
    struct EditorOperation
    {
        bool (*Validate)(void* context, std::span<const std::byte> payload, bool reverting) = nullptr;
        void (*Apply)(void* context, std::span<const std::byte> payload)                    = nullptr;
        void (*Revert)(void* context, std::span<const std::byte> payload)                   = nullptr;
        void (*Destroy)(std::span<std::byte> payload)                                       = nullptr;
        cstring (*Name)(std::span<const std::byte> payload)                                 = nullptr;
    };

    /// @brief Main-thread shared history. Two bounded rings, no heap or arena growth after init.
    /// Payloads are semantic operation data, not arbitrary component-memory snapshots.
    class EditorHistory
    {
    public:
        // max_align_t is only 8-byte aligned on Apple arm64, while authored
        // transform values require 16. Every block (including after wrap) must
        // support those semantic payloads, not only fundamental scalar types.
        static constexpr size_t PayloadAlignment = alignof(std::max_align_t) > 16 ? alignof(std::max_align_t) : 16;
        static_assert((PayloadAlignment & (PayloadAlignment - 1)) == 0);

        [[nodiscard]] bool Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, uint32_t max_entries = 256, size_t payload_bytes = ZMega(16));
        [[nodiscard]] bool CanStore(size_t payload_bytes) const;
        [[nodiscard]] bool ExecuteAndPush(void* context, const EditorOperation& operation, std::span<const std::byte> payload);
        /// @brief Caller has already validated/applied a transaction's after-state. No second apply.
        [[nodiscard]] bool PushAlreadyApplied(const EditorOperation& operation, std::span<const std::byte> payload);
        [[nodiscard]] bool Undo(void* context);
        [[nodiscard]] bool Redo(void* context);

        void               Reset(bool saved = true);
        void               Invalidate();
        void               MarkSaved();

        [[nodiscard]] bool Dirty() const
        {
            return !m_checkpoint_valid || m_current_state != m_saved_state;
        }
        [[nodiscard]] uint32_t UndoCount() const
        {
            return m_cursor;
        }
        [[nodiscard]] uint32_t RedoCount() const
        {
            return m_count - m_cursor;
        }
        [[nodiscard]] size_t BytesUsed() const
        {
            return m_bytes_used;
        }
        [[nodiscard]] size_t ByteCapacity() const
        {
            return m_byte_capacity;
        }
        [[nodiscard]] cstring UndoName() const;
        [[nodiscard]] cstring RedoName() const;

    private:
        struct Entry
        {
            EditorOperation Operation       = {};
            size_t          AllocationStart = 0;
            size_t          AllocationBytes = 0;
            size_t          PayloadOffset   = 0;
            size_t          PayloadBytes    = 0;
            uint64_t        BeforeState     = 0;
            uint64_t        AfterState      = 0;
        };

        Entry&                     At(uint32_t index);
        const Entry&               At(uint32_t index) const;
        std::span<const std::byte> Payload(const Entry& entry) const;
        void                       Destroy(Entry& entry);
        void                       RemoveLast();
        void                       EvictFirst();
        void                       ClearEntries();
        static bool                ValidOperation(const EditorOperation& operation);

        Entry*                     m_entries          = nullptr;
        std::byte*                 m_payloads         = nullptr;
        size_t                     m_byte_capacity    = 0;
        size_t                     m_bytes_used       = 0;
        size_t                     m_tail             = 0;
        uint32_t                   m_entry_capacity   = 0;
        uint32_t                   m_first            = 0;
        uint32_t                   m_count            = 0;
        uint32_t                   m_cursor           = 0;
        uint64_t                   m_current_state    = 0;
        uint64_t                   m_saved_state      = 0;
        uint64_t                   m_next_state       = 1;
        bool                       m_checkpoint_valid = true;
    };
} // namespace Tetragrama
