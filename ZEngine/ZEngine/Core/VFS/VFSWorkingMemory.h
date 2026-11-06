#pragma once
#include <ZEngine/Core/VFS/VFSError.h>

namespace ZEngine::Core::VFS
{
    // Process-local identities, never serialized. Each retained storage domain
    // has one immutable capacity while roots exist. Empty domains have no cost.
    struct VFSSharedMemoryDomain
    {
        const void*        Identity                                       = nullptr;
        uint64_t           Bytes                                          = 0;
        [[nodiscard]] bool operator==(const VFSSharedMemoryDomain&) const = default;
    };

    struct VFSRootMemoryRequirements
    {
        uint64_t                                           StateBytes = 0;
        VFSSharedMemoryDomain                              Storage    = {};
        VFSSharedMemoryDomain                              Mapping    = {};
        [[nodiscard]] VFSResult<VFSRootMemoryRequirements> WithMapping(VFSSharedMemoryDomain) const noexcept;
    };

    class VFSWorkingMemoryReservation;

    // Synchronous caller policy, independent of Cook. Reserve before root-state
    // allocation/first use of borrowed roots; release after wrappers/roots drain.
    // Implementations deduplicate shared domains, not exclusive StateBytes.
    class IVFSWorkingMemoryBudget
    {
    public:
        virtual ~IVFSWorkingMemoryBudget() = default;

    protected:
        friend class VFSWorkingMemoryReservation;
        [[nodiscard]] virtual VFSResult<void> Retain(const VFSRootMemoryRequirements&) noexcept  = 0;
        virtual void                          Release(const VFSRootMemoryRequirements&) noexcept = 0;
    };

    class VFSWorkingMemoryReservation
    {
    public:
        VFSWorkingMemoryReservation() = default;
        ~VFSWorkingMemoryReservation();
        VFSWorkingMemoryReservation(const VFSWorkingMemoryReservation&)            = delete;
        VFSWorkingMemoryReservation& operator=(const VFSWorkingMemoryReservation&) = delete;
        VFSWorkingMemoryReservation(VFSWorkingMemoryReservation&&) noexcept;
        VFSWorkingMemoryReservation&           operator=(VFSWorkingMemoryReservation&&) noexcept;
        [[nodiscard]] VFSResult<void>          Acquire(IVFSWorkingMemoryBudget&, const VFSRootMemoryRequirements&) noexcept;
        void                                   Reset() noexcept;
        [[nodiscard]] IVFSWorkingMemoryBudget* Budget() const noexcept
        {
            return m_budget;
        }
        [[nodiscard]] const VFSRootMemoryRequirements& Requirements() const noexcept
        {
            return m_requirements;
        }

    private:
        IVFSWorkingMemoryBudget*  m_budget       = nullptr;
        VFSRootMemoryRequirements m_requirements = {};
    };
} // namespace ZEngine::Core::VFS
