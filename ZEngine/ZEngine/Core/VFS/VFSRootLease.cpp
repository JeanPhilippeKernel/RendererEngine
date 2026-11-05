#include <ZEngine/Core/VFS/VFSMountState.h>
#include <ZEngine/Core/VFS/VFSRootLease.h>
#include <utility>

namespace ZEngine::Core::VFS
{
    namespace
    {
        PaddedAtomic<uint64_t> NextBackendIdentity = {.value = 1};
    }

    IVFSRootProvider::IVFSRootProvider() : m_identity(NextBackendIdentity.value.fetch_add(1, std::memory_order_relaxed)) {}

    VFSResult<Helpers::Scope<IVFSRootOperations>> IVFSRootProvider::Acquire(const VFSPath& root, VFSRootAccess access)
    {
        if (!root.IsValid() || access > VFSRootAccess::Publication)
            return VFSResult<Helpers::Scope<IVFSRootOperations>>::Fail(VFSError::InvalidPath);
        std::lock_guard lock(m_mutex);
        if (m_retired)
            return VFSResult<Helpers::Scope<IVFSRootOperations>>::Fail(VFSError::Unsupported);
        return AcquireRoot(root, access);
    }

    void IVFSRootProvider::Retire()
    {
        std::lock_guard lock(m_mutex);
        m_retired = true;
    }

    VFSRootLease::VFSRootLease() noexcept = default;
    VFSRootLease::~VFSRootLease()
    {
        Reset();
    }
    VFSRootLease::VFSRootLease(VFSRootLease&& other) noexcept : m_operations(std::move(other.m_operations)), m_memory(std::move(other.m_memory)), m_mounts(std::move(other.m_mounts)), m_pin(std::exchange(other.m_pin, UINT32_MAX)) {}

    VFSRootLease& VFSRootLease::operator=(VFSRootLease&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_operations = std::move(other.m_operations);
            m_memory     = std::move(other.m_memory);
            m_mounts     = std::move(other.m_mounts);
            m_pin        = std::exchange(other.m_pin, UINT32_MAX);
        }
        return *this;
    }

    bool VFSRootLease::IsValid() const
    {
        return m_operations != nullptr;
    }

    VFSResult<VFSFileIdentity> VFSRootLease::RootIdentity() const
    {
        return m_operations ? m_operations->RootIdentity() : VFSResult<VFSFileIdentity>::Fail(VFSError::Unsupported);
    }

    VFSResult<VFSFileIdentity> VFSRootLease::Identity(const VFSRelativePath& path) const
    {
        if (!path.IsValid())
            return VFSResult<VFSFileIdentity>::Fail(VFSError::InvalidPath);
        return m_operations ? m_operations->Identity(path) : VFSResult<VFSFileIdentity>::Fail(VFSError::Unsupported);
    }

    VFSResult<void> VFSRootLease::VisitRoot(uint64_t maximum_entries, Containers::ArrayView<uint8_t> buffer, VFSRootVisitorFn visitor, void* context) const
    {
        if (!visitor || !buffer.data() || !buffer.size())
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        return m_operations ? m_operations->Visit(nullptr, maximum_entries, buffer, visitor, context) : VFSResult<void>::Fail(VFSError::Unsupported);
    }

    VFSResult<void> VFSRootLease::VisitDirectory(const VFSRelativePath& directory, uint64_t maximum_entries, Containers::ArrayView<uint8_t> buffer, VFSRootVisitorFn visitor, void* context) const
    {
        if (!directory.IsValid() || !visitor || !buffer.data() || !buffer.size())
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        return m_operations ? m_operations->Visit(&directory, maximum_entries, buffer, visitor, context) : VFSResult<void>::Fail(VFSError::Unsupported);
    }

    VFSResult<bool> VFSRootLease::Overlaps(const VFSRootLease& other) const
    {
        return m_operations && other.m_operations ? m_operations->Overlaps(*other.m_operations) : VFSResult<bool>::Fail(VFSError::Unsupported);
    }

    VFSResult<void> VFSRootLease::ReserveWorkingMemory(IVFSWorkingMemoryBudget& budget, VFSWorkingMemoryReservation* output) const
    {
        if (!output || output->Budget())
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        if (!m_operations || !m_mounts)
            return VFSResult<void>::Fail(VFSError::Unsupported);
        if (m_memory.Budget())
            return VFSResult<void>::Fail(VFSError::AlreadyExists);
        auto requirements = m_operations->MemoryRequirements();
        if (requirements.Failed())
            return VFSResult<void>::Fail(requirements.Error());
        const auto mapped = requirements.Value().WithMapping({m_mounts.get(), VFSMountState::NativeStorageBytes()});
        return mapped.Succeeded() ? output->Acquire(budget, mapped.Value()) : VFSResult<void>::Fail(mapped.Error());
    }

    VFSResult<void> VFSRootLease::AcquireChildRoot(const VFSRelativePath& path, VFSRootLease* output, VFSRootAccess access)
    {
        if (!path.IsValid() || !output)
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        if (output->IsValid())
            return VFSResult<void>::Fail(VFSError::AlreadyExists);
        if (!m_operations || !m_mounts)
            return VFSResult<void>::Fail(VFSError::Unsupported);

        // Keep the inherited logical pin as well as the native anchor. Releasing
        // the parent must not permit mount changes beneath a surviving child.
        std::lock_guard lock(m_mounts->PinMutex);
        if (m_pin >= VFSMountState::MaxPins || !m_mounts->Pins[m_pin].Active)
            return VFSResult<void>::Fail(VFSError::Unsupported);
        const auto logical = m_mounts->Pins[m_pin].Root.Append(path.CStr());
        if (logical.Failed())
            return VFSResult<void>::Fail(logical.Error());
        uint32_t pin = 0;
        while (pin < VFSMountState::MaxPins && m_mounts->Pins[pin].Active)
            ++pin;
        if (pin == VFSMountState::MaxPins)
            return VFSResult<void>::Fail(VFSError::SizeLimitExceeded);

        VFSWorkingMemoryReservation memory = {};
        if (auto* budget = m_memory.Budget())
        {
            auto requirements = m_operations->MemoryRequirements();
            if (requirements.Failed())
                return VFSResult<void>::Fail(requirements.Error());
            const auto mapped = requirements.Value().WithMapping({m_mounts.get(), VFSMountState::NativeStorageBytes()});
            if (mapped.Failed())
                return VFSResult<void>::Fail(mapped.Error());
            const auto reserved = memory.Acquire(*budget, mapped.Value());
            if (reserved.Failed())
                return reserved;
        }
        auto operations = m_operations->AcquireChild(path);
        if (operations.Failed())
            return VFSResult<void>::Fail(operations.Error());
        const auto parent   = m_operations->Capabilities();
        const auto child    = operations.Value() ? operations.Value()->Capabilities() : VFSPublicationCapabilities{};
        const auto required = access == VFSRootAccess::ReadOnly ? VFS_READ_GUARANTEES : access == VFSRootAccess::Publication ? VFS_PUBLICATION_GUARANTEES : VFS_SCRATCH_GUARANTEES;
        if (!parent.BackendIdentity || child.BackendIdentity != parent.BackendIdentity || !HasGuarantees(parent.Guarantees, required) || !HasGuarantees(child.Guarantees, required))
            return VFSResult<void>::Fail(VFSError::Unsupported);
        m_mounts->Pins[pin]  = {.Root = logical.Value(), .Active = true};
        output->m_operations = std::move(operations.Value());
        output->m_memory     = std::move(memory);
        output->m_mounts     = m_mounts;
        output->m_pin        = pin;
        return VFSResult<void>::Ok();
    }

    void VFSRootLease::Reset() noexcept
    {
        // Release native state before permitting a conflicting mount mutation.
        m_operations.reset();
        if (m_mounts)
            m_mounts->ReleasePin(m_pin);
        m_pin = UINT32_MAX;
        m_mounts.reset();
        m_memory.Reset();
    }

    VFSPublicationCapabilities VFSRootLease::Capabilities() const
    {
        return m_operations ? m_operations->Capabilities() : VFSPublicationCapabilities{};
    }

    VFSResult<IVFSFile*> VFSRootLease::Open(const VFSRelativePath& path, VFSOpenFlags flags)
    {
        if (!path.IsValid())
            return VFSResult<IVFSFile*>::Fail(VFSError::InvalidPath);
        return m_operations ? m_operations->Open(path, flags) : VFSResult<IVFSFile*>::Fail(VFSError::Unsupported);
    }

    VFSResult<VFSFileStat> VFSRootLease::Stat(const VFSRelativePath& path) const
    {
        if (!path.IsValid())
            return VFSResult<VFSFileStat>::Fail(VFSError::InvalidPath);
        return m_operations ? m_operations->Stat(path) : VFSResult<VFSFileStat>::Fail(VFSError::Unsupported);
    }

    VFSResult<bool> VFSRootLease::Exists(const VFSRelativePath& path) const
    {
        const auto stat = Stat(path);
        if (stat.Succeeded())
            return VFSResult<bool>::Ok(true);
        return stat.Error() == VFSError::NotFound ? VFSResult<bool>::Ok(false) : VFSResult<bool>::Fail(stat.Error());
    }

    VFSResult<void> VFSRootLease::CreateDir(const VFSRelativePath& path)
    {
        if (!path.IsValid())
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        return m_operations ? m_operations->CreateDir(path, false) : VFSResult<void>::Fail(VFSError::Unsupported);
    }

    VFSResult<void> VFSRootLease::CreateDirExclusive(const VFSRelativePath& path)
    {
        if (!path.IsValid())
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        return m_operations ? m_operations->CreateDir(path, true) : VFSResult<void>::Fail(VFSError::Unsupported);
    }

    VFSResult<void> VFSRootLease::Remove(const VFSRelativePath& path)
    {
        if (!path.IsValid())
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        return m_operations ? m_operations->Remove(path) : VFSResult<void>::Fail(VFSError::Unsupported);
    }

    VFSResult<void> VFSRootLease::RenamePaths(const VFSRelativePath& from, const VFSRelativePath& to, bool replace)
    {
        if (!from.IsValid() || !to.IsValid())
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        return m_operations ? m_operations->Rename(from, to, replace) : VFSResult<void>::Fail(VFSError::Unsupported);
    }

    void VFSRootLease::Close(IVFSFile* file)
    {
        (void) CloseChecked(file);
    }

    VFSResult<void> VFSRootLease::CloseChecked(IVFSFile* file)
    {
        if (!file)
            return VFSResult<void>::Ok();
        return m_operations ? m_operations->CloseChecked(file) : VFSResult<void>::Fail(VFSError::Unsupported);
    }
} // namespace ZEngine::Core::VFS
