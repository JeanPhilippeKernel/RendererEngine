#pragma once
#include <ZEngine/Cook/CookBudgetAllocator.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/VFS/VFSWorkingMemory.h>

namespace ZEngine::Cook
{
    // Synchronous per-call VFS policy. Exclusive root state is summed; backend
    // pools and mount tables are charged once while any reservation uses them.
    // Common runs use the inline engine hash table and allocate no index storage.
    // Larger domain sets grow geometrically through two checked workspaces, never
    // abandoning old arena blocks. No accounting callback allocates per file.
    class CookVFSWorkingMemory final : public Core::VFS::IVFSWorkingMemoryBudget
    {
    public:
        CookVFSWorkingMemory() = default;
        ~CookVFSWorkingMemory() override;
        CookVFSWorkingMemory(const CookVFSWorkingMemory&)                               = delete;
        CookVFSWorkingMemory&                    operator=(const CookVFSWorkingMemory&) = delete;
        [[nodiscard]] Core::VFS::VFSResult<void> Initialize(CookBudgetAllocator&);
        [[nodiscard]] Core::VFS::VFSResult<void> Reset() noexcept;
        [[nodiscard]] uint64_t                   ReservedBytes() const noexcept
        {
            return m_charge.Bytes();
        }
        [[nodiscard]] size_t ReservationCount() const noexcept
        {
            return m_reservations;
        }
        [[nodiscard]] size_t DomainCount() const noexcept
        {
            return m_domains.size();
        }

    private:
        struct Domain
        {
            uint64_t Bytes      = 0;
            uint64_t References = 0;
        };
        using Domains                                        = Core::Containers::UnorderedHashMap<uintptr_t, Domain>;
        static constexpr size_t                  InlineSlots = 16;
        [[nodiscard]] Core::VFS::VFSResult<void> Retain(const Core::VFS::VFSRootMemoryRequirements&) noexcept override;
        void                                     Release(const Core::VFS::VFSRootMemoryRequirements&) noexcept override;
        [[nodiscard]] Core::VFS::VFSResult<void> EnsureCapacity(size_t additions) noexcept;

        CookBudgetAllocator*                     m_allocator                              = nullptr;
        CookMemoryCharge                         m_self_charge                            = {};
        CookMemoryCharge                         m_charge                                 = {};
        alignas(DEFAULT_ALIGNMENT) uint8_t m_inline[InlineSlots * sizeof(Domains::Entry)] = {};
        Core::Memory::ArenaAllocator m_inline_arena                                       = {};
        CookWorkspace                m_storage[2]                                         = {};
        Domains                      m_domains                                            = {};
        size_t                       m_reservations                                       = 0;
        size_t                       m_erased                                             = 0;
        uint8_t                      m_active_storage                                     = 2;
    };
} // namespace ZEngine::Cook
