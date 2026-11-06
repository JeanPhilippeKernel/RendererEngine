#include <ZEngine/Core/VFS/VFSWorkingMemory.h>
#include <utility>

namespace ZEngine::Core::VFS
{
    VFSResult<VFSRootMemoryRequirements> VFSRootMemoryRequirements::WithMapping(VFSSharedMemoryDomain mapping) const noexcept
    {
        if (!StateBytes || Mapping.Identity || Mapping.Bytes || bool(Storage.Identity) != bool(Storage.Bytes) || !mapping.Identity || !mapping.Bytes || mapping.Identity == Storage.Identity)
        {
            return VFSResult<VFSRootMemoryRequirements>::Fail(VFSError::Unsupported);
        }
        auto result    = *this;
        result.Mapping = mapping;
        return VFSResult<VFSRootMemoryRequirements>::Ok(result);
    }

    VFSWorkingMemoryReservation::~VFSWorkingMemoryReservation()
    {
        Reset();
    }

    VFSWorkingMemoryReservation::VFSWorkingMemoryReservation(VFSWorkingMemoryReservation&& other) noexcept : m_budget(std::exchange(other.m_budget, nullptr)), m_requirements(std::exchange(other.m_requirements, {})) {}

    VFSWorkingMemoryReservation& VFSWorkingMemoryReservation::operator=(VFSWorkingMemoryReservation&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_budget       = std::exchange(other.m_budget, nullptr);
            m_requirements = std::exchange(other.m_requirements, {});
        }
        return *this;
    }

    VFSResult<void> VFSWorkingMemoryReservation::Acquire(IVFSWorkingMemoryBudget& budget, const VFSRootMemoryRequirements& requirements) noexcept
    {
        const auto valid = [](const VFSSharedMemoryDomain& domain) { return bool(domain.Identity) == bool(domain.Bytes); };
        if (m_budget || (!requirements.StateBytes && !requirements.Storage.Bytes && !requirements.Mapping.Bytes) || !valid(requirements.Storage) || !valid(requirements.Mapping) || (requirements.Storage.Identity && requirements.Storage.Identity == requirements.Mapping.Identity))
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        const auto retained = budget.Retain(requirements);
        if (retained.Failed())
        {
            return retained;
        }
        m_budget       = &budget;
        m_requirements = requirements;
        return VFSResult<void>::Ok();
    }

    void VFSWorkingMemoryReservation::Reset() noexcept
    {
        if (m_budget)
        {
            m_budget->Release(m_requirements);
        }
        m_budget       = nullptr;
        m_requirements = {};
    }
} // namespace ZEngine::Core::VFS
