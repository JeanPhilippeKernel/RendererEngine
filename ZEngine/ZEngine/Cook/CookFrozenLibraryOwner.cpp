#include <ZEngine/Cook/CookFrozenLibraryOwner.h>
#include <ZEngine/Cook/CookSort.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <algorithm>
#include <new>
#include <utility>

namespace ZEngine::Cook
{
    CookFrozenLibraryReadLease::~CookFrozenLibraryReadLease()
    {
        Reset();
    }

    CookFrozenLibraryReadLease::CookFrozenLibraryReadLease(CookFrozenLibraryReadLease&& other) noexcept : m_owner(std::exchange(other.m_owner, nullptr)), m_exclusion(std::move(other.m_exclusion)) {}

    CookFrozenLibraryReadLease& CookFrozenLibraryReadLease::operator=(CookFrozenLibraryReadLease&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_owner     = std::exchange(other.m_owner, nullptr);
            m_exclusion = std::move(other.m_exclusion);
        }
        return *this;
    }

    void CookFrozenLibraryReadLease::Reset() noexcept
    {
        m_owner = nullptr;
        m_exclusion.Reset();
    }

    uint32_t CookFrozenLibraryReadLease::RootCount() const noexcept
    {
        return m_owner ? m_owner->m_count : 0;
    }

    Core::Containers::StringView CookFrozenLibraryReadLease::RootKey(uint32_t index) const noexcept
    {
        return index < RootCount() ? Core::Containers::StringView(m_owner->m_roots[index].Key) : Core::Containers::StringView{};
    }

    Core::VFS::VFSReadRootLease* CookFrozenLibraryReadLease::Root(uint32_t index) const noexcept
    {
        return index < RootCount() ? &m_owner->m_roots[index].Lease : nullptr;
    }

    Core::VFS::VFSResult<Core::VFS::VFSFileIdentity> CookFrozenLibraryReadLease::RootIdentity(uint32_t index) const
    {
        const auto* root = Root(index);
        return root ? root->RootIdentity() : Core::VFS::VFSResult<Core::VFS::VFSFileIdentity>::Fail(Core::VFS::VFSError::InvalidPath);
    }

    Core::VFS::VFSResult<uint32_t> CookFrozenLibraryReadLease::Resolve(const Core::VFS::VFSRelativePath& logical, Core::VFS::VFSRelativePath* relative) const
    {
        if (!relative || !IsValid() || !IsCookLogicalPath(logical))
        {
            return Core::VFS::VFSResult<uint32_t>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        size_t length = 0;
        while (length < logical.Length() && logical.CStr()[length] != '/')
        {
            ++length;
        }
        char key[33] = {};
        Helpers::secure_memcpy(key, sizeof(key), logical.CStr(), length);
        uint32_t first = 0, last = m_owner->m_count;
        while (first < last)
        {
            const auto middle = first + (last - first) / 2;
            if (Helpers::secure_strcmp(m_owner->m_roots[middle].Key, key) < 0)
            {
                first = middle + 1;
            }
            else
            {
                last = middle;
            }
        }
        if (first == m_owner->m_count || RootKey(first) != key)
        {
            return Core::VFS::VFSResult<uint32_t>::Fail(Core::VFS::VFSError::NotFound);
        }
        const auto parsed = Core::VFS::VFSRelativePath::Parse(logical.CStr() + length + 1, logical.Length() - length - 1);
        if (parsed.Failed())
        {
            return Core::VFS::VFSResult<uint32_t>::Fail(parsed.Error());
        }
        *relative = parsed.Value();
        return Core::VFS::VFSResult<uint32_t>::Ok(first);
    }

    Core::VFS::VFSResult<void> CookFrozenLibraryReadLease::ReserveRootWorkingMemory(uint32_t index, Core::VFS::IVFSWorkingMemoryBudget& budget, Core::VFS::VFSWorkingMemoryReservation* output) const
    {
        const auto* root = Root(index);
        return root ? root->ReserveWorkingMemory(budget, output) : Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
    }

    Core::VFS::VFSResult<void> CookFrozenLibraryReadLease::RequireDisjoint(const Core::VFS::VFSRootLease& other) const
    {
        if (!IsValid() || !other.IsValid())
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        for (uint32_t i = 0; i < RootCount(); ++i)
        {
            const auto overlap = Root(i)->Overlaps(other);
            if (overlap.Failed())
            {
                return Core::VFS::VFSResult<void>::Fail(overlap.Error());
            }
            if (overlap.Value())
            {
                return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::PermissionDenied);
            }
        }
        return Core::VFS::VFSResult<void>::Ok();
    }

    Core::VFS::VFSResult<void> CookFrozenLibraryReadLease::MapContextPath(const Core::VFS::VFSPath& path, Core::VFS::VFSRelativePath* output) const
    {
        if (!IsValid() || !output || !path.IsValid() || path.IsRoot())
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        uint32_t selected = UINT32_MAX;
        // Root counts are small; do not allocate a second index for caller-path
        // conversion. Catalog identity/path lookups remain binary searches.
        for (uint32_t i = 0; i < m_owner->m_count; ++i)
        {
            const auto& root = m_owner->m_roots[i].ContextPath;
            if (!root.IsPrefixOf(path))
            {
                continue;
            }
            if (root == path)
            {
                return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
            }
            if (selected != UINT32_MAX)
            {
                return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::AlreadyExists);
            }
            selected = i;
        }
        if (selected == UINT32_MAX)
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::NotFound);
        }
        const auto&  root     = m_owner->m_roots[selected];
        const size_t offset   = root.ContextPath.IsRoot() ? 1 : root.ContextPath.Length() + 1;
        const auto   relative = Core::VFS::VFSRelativePath::Parse(path.CStr() + offset, path.Length() - offset);
        const auto   key      = Core::VFS::VFSRelativePath::Parse(root.Key);
        if (relative.Failed() || key.Failed())
        {
            return Core::VFS::VFSResult<void>::Fail(relative.Failed() ? relative.Error() : key.Error());
        }
        const auto logical = key.Value().Append(relative.Value());
        if (logical.Failed())
        {
            return Core::VFS::VFSResult<void>::Fail(logical.Error());
        }
        *output = logical.Value();
        return Core::VFS::VFSResult<void>::Ok();
    }

    Core::VFS::VFSResult<Core::VFS::IVFSFile*> CookFrozenLibraryReadLease::Open(const Core::VFS::VFSRelativePath& path, uint32_t* root_index) const
    {
        if (!root_index)
        {
            return Core::VFS::VFSResult<Core::VFS::IVFSFile*>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        Core::VFS::VFSRelativePath relative = {};
        const auto                 resolved = Resolve(path, &relative);
        if (resolved.Failed())
        {
            return Core::VFS::VFSResult<Core::VFS::IVFSFile*>::Fail(resolved.Error());
        }
        const auto opened = Root(resolved.Value())->Open(relative);
        if (opened.Succeeded())
        {
            *root_index = resolved.Value();
        }
        return opened;
    }

    Core::VFS::VFSResult<void> CookFrozenLibraryReadLease::CloseChecked(uint32_t root_index, Core::VFS::IVFSFile* file) const
    {
        const auto root = Root(root_index);
        return root ? root->CloseChecked(file) : Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
    }

    Core::VFS::VFSResult<Core::VFS::VFSFileStat> CookFrozenLibraryReadLease::Stat(const Core::VFS::VFSRelativePath& path) const
    {
        Core::VFS::VFSRelativePath relative = {};
        const auto                 resolved = Resolve(path, &relative);
        return resolved.Succeeded() ? Root(resolved.Value())->Stat(relative) : Core::VFS::VFSResult<Core::VFS::VFSFileStat>::Fail(resolved.Error());
    }

    Core::VFS::VFSResult<Core::VFS::VFSFileIdentity> CookFrozenLibraryReadLease::Identity(const Core::VFS::VFSRelativePath& path) const
    {
        Core::VFS::VFSRelativePath relative = {};
        const auto                 resolved = Resolve(path, &relative);
        return resolved.Succeeded() ? Root(resolved.Value())->Identity(relative) : Core::VFS::VFSResult<Core::VFS::VFSFileIdentity>::Fail(resolved.Error());
    }

    Core::VFS::VFSResult<void> CookFrozenLibraryReadLease::Visit(uint32_t root_index, const Core::VFS::VFSRelativePath* directory, uint64_t maximum_entries, Core::Containers::ArrayView<uint8_t> buffer, Core::VFS::VFSRootVisitorFn visitor, void* context) const
    {
        const auto* root = Root(root_index);
        if (!root)
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        return directory ? root->VisitDirectory(*directory, maximum_entries, buffer, visitor, context) : root->VisitRoot(maximum_entries, buffer, visitor, context);
    }

    CookFrozenLibraryOwner::~CookFrozenLibraryOwner()
    {
        const auto reset = Reset();
        ZENGINE_VALIDATE_ASSERT(reset.Succeeded(), "Frozen cook owner destroyed before its leases.");
    }

    Core::VFS::VFSResult<uint64_t> CookFrozenLibraryOwner::FixedWorkingBytes(const ICookAllocator& covered, const ICookMemoryCoverage* coverage) const noexcept
    {
        if (!m_allocator || !m_roots)
        {
            return Core::VFS::VFSResult<uint64_t>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        const auto additional = CookUncoveredAllocatorBytes(*m_allocator, covered, coverage);
        if (additional.Failed())
        {
            return additional;
        }
        const uint64_t state = coverage && coverage->Contains(this, sizeof(*this)) ? 0 : sizeof(*this);
        if (additional.Value() > UINT64_MAX - state)
        {
            return Core::VFS::VFSResult<uint64_t>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
        }
        uint64_t   bytes        = state + additional.Value();
        const auto requirements = m_allocator->MemoryRequirements();
        if (requirements.Failed())
        {
            return Core::VFS::VFSResult<uint64_t>::Fail(requirements.Error());
        }
        const size_t payload   = static_cast<size_t>(m_count) * sizeof(RootBinding);
        const auto   footprint = m_allocator->AllocationFootprint(payload, alignof(RootBinding), CookMemoryCategory::CatalogGraph);
        if (footprint.Failed())
        {
            return Core::VFS::VFSResult<uint64_t>::Fail(footprint.Error());
        }
        if (footprint.Value() < payload)
        {
            return Core::VFS::VFSResult<uint64_t>::Fail(Core::VFS::VFSError::Corrupted);
        }
        if (!requirements.Value().IncludesAllocations)
        {
            if (footprint.Value() > UINT64_MAX - bytes)
            {
                return Core::VFS::VFSResult<uint64_t>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
            }
            bytes += footprint.Value();
        }
        return Core::VFS::VFSResult<uint64_t>::Ok(bytes);
    }

    Core::VFS::VFSResult<void> CookFrozenLibraryOwner::Initialize(ICookAllocator& allocator, Core::Containers::ArrayView<const CookFrozenRootRequest> requests)
    {
        if (!requests.data() || !requests.size())
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        if (requests.size() > UINT32_MAX || requests.size() > SIZE_MAX / sizeof(RootBinding))
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
        }
        CookLibraryMutationLease mutation = {};
        const auto               acquired = m_exclusion.TryAcquireMutation(&mutation);
        if (acquired.Failed())
        {
            return acquired;
        }
        if (m_roots)
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::AlreadyExists);
        }
        // Validate before acquiring pins/allocating; failures never publish a
        // partially configured root set or retain a caller's key/context views.
        for (const auto& request : requests)
        {
            if (!IsCookRootKey(request.Key) || !request.Context || !request.Path.IsValid())
            {
                return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
            }
        }
        CookMemoryCharge fixed = {};
        if (requests.size() > 1)
        {
            const auto charged = allocator.TryCharge(sizeof(Detail::CookSortState<RootBinding>) + sizeof(fixed), CookMemoryCategory::CatalogGraph, &fixed);
            if (charged.Failed())
            {
                return charged;
            }
        }
        const auto allocation = allocator.Allocate(requests.size() * sizeof(RootBinding), alignof(RootBinding), CookMemoryCategory::CatalogGraph);
        if (allocation.Failed())
        {
            return Core::VFS::VFSResult<void>::Fail(allocation.Error());
        }
        if (!allocation.Value())
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::OutOfMemory);
        }
        if (reinterpret_cast<uintptr_t>(allocation.Value()) % alignof(RootBinding))
        {
            allocator.Free(allocation.Value());
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        auto*      roots = static_cast<RootBinding*>(allocation.Value());
        const auto count = static_cast<uint32_t>(requests.size());
        for (uint32_t i = 0; i < count; ++i)
        {
            new (&roots[i]) RootBinding{};
        }
        const auto release_pending = [&] {
            for (uint32_t i = 0; i < count; ++i)
            {
                roots[i].~RootBinding();
            }
            allocator.Free(roots);
        };
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto& request = requests[i];
            Helpers::secure_memcpy(roots[i].Key, sizeof(roots[i].Key), request.Key.data(), request.Key.size());
            roots[i].ContextPath = request.Path;
            const auto pinned    = request.Context->AcquireReadRoot(request.Path, &roots[i].Lease);
            if (pinned.Failed())
            {
                release_pending();
                return pinned;
            }
        }
        if (count > 1)
        {
            Detail::CookSortState<RootBinding> sort = {};
            Core::Containers::HeapSort<RootBinding>({roots, count}, sort, [](const RootBinding& a, const RootBinding& b) { return Helpers::secure_strcmp(a.Key, b.Key) < 0; });
        }
        fixed.Release();
        for (uint32_t i = 0; i < count; ++i)
        {
            if (i && Core::Containers::StringView(roots[i - 1].Key) == Core::Containers::StringView(roots[i].Key))
            {
                release_pending();
                return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::AlreadyExists);
            }
            const auto identity = roots[i].Lease.RootIdentity();
            if (identity.Failed() || !identity.Value().IsValid())
            {
                const auto error = identity.Failed() ? identity.Error() : Core::VFS::VFSError::Unsupported;
                release_pending();
                return Core::VFS::VFSResult<void>::Fail(error);
            }
            for (uint32_t j = 0; j < i; ++j)
            {
                const auto overlap = roots[i].Lease.Overlaps(roots[j].Lease);
                if (overlap.Failed() || overlap.Value())
                {
                    const auto error = overlap.Failed() ? overlap.Error() : Core::VFS::VFSError::PermissionDenied;
                    release_pending();
                    return Core::VFS::VFSResult<void>::Fail(error);
                }
            }
        }
        m_allocator = &allocator;
        m_roots     = roots;
        m_count     = count;
        return Core::VFS::VFSResult<void>::Ok();
    }

    Core::VFS::VFSResult<void> CookFrozenLibraryOwner::TryAcquireRead(CookFrozenLibraryReadLease* output)
    {
        if (!output)
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        if (output->IsValid())
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::AlreadyExists);
        }
        CookLibraryReadLease exclusion = {};
        const auto           acquired  = m_exclusion.TryAcquireRead(&exclusion);
        if (acquired.Failed())
        {
            return acquired;
        }
        if (!m_roots)
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Unsupported);
        }
        output->m_owner     = this;
        output->m_exclusion = std::move(exclusion);
        return Core::VFS::VFSResult<void>::Ok();
    }

    Core::VFS::VFSResult<void> CookFrozenLibraryOwner::TryAcquireMutation(CookLibraryMutationLease* output)
    {
        return m_exclusion.TryAcquireMutation(output);
    }

    Core::VFS::VFSResult<void> CookFrozenLibraryOwner::Reset() noexcept
    {
        CookLibraryMutationLease mutation = {};
        const auto               acquired = m_exclusion.TryAcquireMutation(&mutation);
        if (acquired.Failed())
        {
            return acquired;
        }
        for (uint32_t i = 0; i < m_count; ++i)
        {
            m_roots[i].~RootBinding();
        }
        if (m_roots)
        {
            m_allocator->Free(m_roots);
        }
        m_roots     = nullptr;
        m_allocator = nullptr;
        m_count     = 0;
        return Core::VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Cook
