#include <ZEngine/Core/VFS/VFSMountState.h>
#include <ZEngine/Core/VFS/VFSMountTable.h>
#include <mutex>
#include <new>

namespace ZEngine::Core::VFS
{
    bool VFSMountState::HasPins() const
    {
        std::lock_guard lock(PinMutex);
        for (const auto& pin : Pins)
        {
            if (pin.Active)
            {
                return true;
            }
        }
        return false;
    }

    bool VFSMountState::OverlapsPin(const VFSPath& path) const
    {
        std::lock_guard lock(PinMutex);
        for (const auto& pin : Pins)
        {
            if (pin.Active && (pin.Root.IsPrefixOf(path) || path.IsPrefixOf(pin.Root)))
            {
                return true;
            }
        }
        return false;
    }

    void VFSMountState::ReleasePin(uint32_t index) noexcept
    {
        std::lock_guard lock(PinMutex);
        if (index < MaxPins)
        {
            Pins[index] = {};
        }
        ReleaseClosedMappings();
    }

    void VFSMountState::Retire()
    {
        std::lock_guard lock(PinMutex);
        Closed = true;
        ReleaseClosedMappings();
    }

    void VFSMountState::ReleaseClosedMappings()
    {
        if (!Closed)
        {
            return;
        }
        for (const auto& pin : Pins)
        {
            if (pin.Active)
            {
                return;
            }
        }
        for (size_t i = 0; i < Count; ++i)
        {
            Mounts[i] = {};
        }
        Count = 0;
    }

    VFSMountTable::VFSMountTable() : m_state() {}
    VFSMountTable::~VFSMountTable() = default;

    void VFSMountTable::Initialize(Memory::ArenaAllocator*, size_t)
    {
        std::unique_lock lock(m_mutex);
        // A shutdown domain with live pins cannot be replaced by a fresh mapping.
        if (m_state && m_state->HasPins())
        {
            return;
        }
        m_state = new (std::nothrow) VFSMountState();
    }

    size_t VFSMountTable::Count() const
    {
        std::shared_lock lock(m_mutex);
        return m_state && !m_state->Closed ? m_state->Count : 0;
    }

    void VFSMountTable::Clear()
    {
        std::unique_lock lock(m_mutex);
        if (!m_state)
        {
            return;
        }
        m_state->Retire();
        // Preserve the actual mapping and operational providers until pins drain.
        if (!m_state->HasPins())
        {
            m_state.reset();
        }
    }

    VFSResult<void> VFSMountTable::Mount(IVFSBackend* const backend, const VFSPath& logical_root, int priority)
    {
        std::unique_lock lock(m_mutex);
        if (!backend || !logical_root.IsValid())
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        if (!m_state || m_state->Closed)
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        if (m_state->OverlapsPin(logical_root))
        {
            return VFSResult<void>::Fail(VFSError::AlreadyExists);
        }
        for (size_t i = 0; i < m_state->Count; ++i)
        {
            if (m_state->Mounts[i].LogicalRoot == logical_root)
            {
                return VFSResult<void>::Fail(VFSError::AlreadyExists);
            }
        }
        if (m_state->Count == kMaxMountPoints)
        {
            return VFSResult<void>::Fail(VFSError::SizeLimitExceeded);
        }

        size_t insert_index = m_state->Count;
        for (size_t i = 0; i < m_state->Count; ++i)
        {
            if (m_state->Mounts[i].Priority < priority)
            {
                insert_index = i;
                break;
            }
        }
        for (size_t i = m_state->Count; i > insert_index; --i)
        {
            m_state->Mounts[i] = std::move(m_state->Mounts[i - 1]);
        }
        m_state->Mounts[insert_index] = {.Backend = backend, .LogicalRoot = logical_root, .Priority = priority, .RootProvider = backend->RootProvider()};
        ++m_state->Count;
        return VFSResult<void>::Ok();
    }

    VFSResult<void> VFSMountTable::Unmount(const VFSPath& logical_root)
    {
        std::unique_lock lock(m_mutex);
        if (!logical_root.IsValid())
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        if (!m_state || m_state->Closed)
        {
            return VFSResult<void>::Fail(VFSError::NotFound);
        }
        if (m_state->OverlapsPin(logical_root))
        {
            return VFSResult<void>::Fail(VFSError::AlreadyExists);
        }
        for (size_t i = 0; i < m_state->Count; ++i)
        {
            if (m_state->Mounts[i].LogicalRoot == logical_root)
            {
                for (size_t j = i + 1; j < m_state->Count; ++j)
                {
                    m_state->Mounts[j - 1] = std::move(m_state->Mounts[j]);
                }
                m_state->Mounts[--m_state->Count] = {};
                return VFSResult<void>::Ok();
            }
        }
        return VFSResult<void>::Fail(VFSError::NotFound);
    }

    bool VFSMountTable::IsPrefixOf(const VFSPath& prefix, const VFSPath& path)
    {
        return prefix.IsPrefixOf(path);
    }

    VFSResult<VFSPath> VFSMountTable::StripPrefix(const VFSPath& prefix, const VFSPath& path)
    {
        cstring path_buf = path.CStr();
        size_t  skip     = prefix.Length();
        if (path_buf[skip] == '/')
        {
            ++skip;
        }
        return path_buf[skip] == '\0' ? VFSResult<VFSPath>::Ok(VFSPath::Root()) : VFSPath::Parse(path_buf + skip);
    }

    size_t VFSMountTable::FindMount(const VFSPath& path) const
    {
        size_t best = SIZE_MAX, prefix_length = 0;
        if (m_state && !m_state->Closed)
        {
            for (size_t i = 0; i < m_state->Count; ++i)
            {
                const auto& root = m_state->Mounts[i].LogicalRoot;
                if (root.Length() > prefix_length && IsPrefixOf(root, path))
                {
                    best          = i;
                    prefix_length = root.Length();
                }
            }
        }
        return best;
    }

    VFSResult<ResolveResult> VFSMountTable::Resolve(const VFSPath& path) const
    {
        std::shared_lock lock(m_mutex);
        if (!path.IsValid())
        {
            return VFSResult<ResolveResult>::Fail(VFSError::InvalidPath);
        }
        const auto index = FindMount(path);
        if (index == SIZE_MAX)
        {
            return VFSResult<ResolveResult>::Fail(VFSError::NotFound);
        }
        auto relative = StripPrefix(m_state->Mounts[index].LogicalRoot, path);
        if (relative.Failed())
        {
            return VFSResult<ResolveResult>::Fail(relative.Error());
        }
        return VFSResult<ResolveResult>::Ok({m_state->Mounts[index].Backend, relative.Value()});
    }

    VFSResult<void> VFSMountTable::ResolveAll(const VFSPath& path, Containers::Array<ResolveResult>& output) const
    {
        std::shared_lock lock(m_mutex);
        if (!path.IsValid())
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        if (!m_state || m_state->Closed)
        {
            return VFSResult<void>::Fail(VFSError::NotFound);
        }
        for (size_t i = 0; i < m_state->Count; ++i)
        {
            if (IsPrefixOf(m_state->Mounts[i].LogicalRoot, path))
            {
                auto relative = StripPrefix(m_state->Mounts[i].LogicalRoot, path);
                if (relative.Failed())
                {
                    return VFSResult<void>::Fail(relative.Error());
                }
                output.push({m_state->Mounts[i].Backend, relative.Value()});
            }
        }
        return VFSResult<void>::Ok();
    }

    VFSResult<void> VFSMountTable::AcquirePublicationRoot(const VFSPath& root, VFSPublicationRootLease* output, IVFSWorkingMemoryBudget* budget)
    {
        return AcquireRoot(root, output, VFSRootAccess::Publication, budget);
    }

    VFSResult<void> VFSMountTable::AcquireScratchRoot(const VFSPath& root, VFSScratchRootLease* output, IVFSWorkingMemoryBudget* budget)
    {
        return AcquireRoot(root, output, VFSRootAccess::Scratch, budget);
    }

    VFSResult<void> VFSMountTable::AcquireReadRoot(const VFSPath& root, VFSReadRootLease* output, IVFSWorkingMemoryBudget* budget)
    {
        return AcquireRoot(root, output, VFSRootAccess::ReadOnly, budget);
    }

    VFSResult<void> VFSMountTable::AcquireRoot(const VFSPath& root, VFSRootLease* output, VFSRootAccess access, IVFSWorkingMemoryBudget* budget)
    {
        std::unique_lock lock(m_mutex);
        if (!root.IsValid() || !output)
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        if (output->IsValid())
        {
            return VFSResult<void>::Fail(VFSError::AlreadyExists);
        }
        const auto index = FindMount(root);
        if (index == SIZE_MAX)
        {
            return VFSResult<void>::Fail(VFSError::NotFound);
        }
        const auto& mount = m_state->Mounts[index];
        if (!mount.RootProvider)
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        // A lease may not bypass a more-specific mount inside its relative tree.
        for (size_t i = 0; i < m_state->Count; ++i)
        {
            if (i != index && root.IsPrefixOf(m_state->Mounts[i].LogicalRoot))
            {
                return VFSResult<void>::Fail(VFSError::Unsupported);
            }
        }
        const auto relative = StripPrefix(mount.LogicalRoot, root);
        if (relative.Failed())
        {
            return VFSResult<void>::Fail(relative.Error());
        }

        std::lock_guard pin_lock(m_state->PinMutex);
        uint32_t        pin = 0;
        while (pin < VFSMountState::MaxPins && m_state->Pins[pin].Active)
        {
            ++pin;
        }
        if (pin == VFSMountState::MaxPins)
        {
            return VFSResult<void>::Fail(VFSError::SizeLimitExceeded);
        }
        VFSWorkingMemoryReservation memory = {};
        if (budget)
        {
            auto requirements = mount.RootProvider->MemoryRequirements();
            if (requirements.Failed())
            {
                return VFSResult<void>::Fail(requirements.Error());
            }
            const auto mapped = requirements.Value().WithMapping({m_state.get(), VFSMountState::NativeStorageBytes()});
            if (mapped.Failed())
            {
                return VFSResult<void>::Fail(mapped.Error());
            }
            const auto reserved = memory.Acquire(*budget, mapped.Value());
            if (reserved.Failed())
            {
                return reserved;
            }
        }
        auto operations = mount.RootProvider->Acquire(relative.Value(), access);
        if (operations.Failed())
        {
            return VFSResult<void>::Fail(operations.Error());
        }
        const auto capabilities = operations.Value() ? operations.Value()->Capabilities() : VFSPublicationCapabilities{};
        const auto required     = access == VFSRootAccess::ReadOnly ? VFS_READ_GUARANTEES : access == VFSRootAccess::Publication ? VFS_PUBLICATION_GUARANTEES : VFS_SCRATCH_GUARANTEES;
        if (!capabilities.BackendIdentity || !HasGuarantees(capabilities.Guarantees, required))
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        m_state->Pins[pin]   = {.Root = root, .Active = true};
        output->m_operations = std::move(operations.Value());
        output->m_memory     = std::move(memory);
        output->m_mounts     = m_state;
        output->m_pin        = pin;
        return VFSResult<void>::Ok();
    }
} // namespace ZEngine::Core::VFS
