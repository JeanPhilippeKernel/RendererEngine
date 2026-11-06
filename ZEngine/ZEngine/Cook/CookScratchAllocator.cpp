#include <ZEngine/Cook/CookScratchAllocator.h>

namespace ZEngine::Cook
{
    CookScratchAllocator::~CookScratchAllocator()
    {
        Release();
    }

    Core::VFS::VFSResult<void> CookScratchAllocator::Initialize(ICookAllocator& parent, size_t bytes, CookMemoryCategory backing_category)
    {
        if (m_parent || &parent == this)
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        const auto workspace = m_workspace.Initialize(parent, bytes, backing_category);
        if (workspace.Failed())
        {
            return workspace;
        }
        if (!m_slab.TryInit(m_workspace.Arena(), bytes))
        {
            m_workspace.Release();
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
        }
        m_parent           = &parent;
        m_backing_bytes    = bytes;
        m_backing_category = backing_category;
        return Core::VFS::VFSResult<void>::Ok();
    }

    void CookScratchAllocator::Release() noexcept
    {
        ZENGINE_VALIDATE_ASSERT(m_outstanding == 0, "Cook scratch released before its consumers.");
        m_slab.Shutdown();
        m_workspace.Release();
        m_parent           = nullptr;
        m_backing_bytes    = 0;
        m_backing_category = CookMemoryCategory::Codecs;
    }

    Core::VFS::VFSResult<void*> CookScratchAllocator::Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept
    {
        if (!m_parent || !bytes || !alignment || (alignment & (alignment - 1)) || category >= CookMemoryCategory::Count)
        {
            return Core::VFS::VFSResult<void*>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        auto* pointer = m_slab.TryAllocate(bytes, alignment);
        if (!pointer)
        {
            return Core::VFS::VFSResult<void*>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
        }
        ++m_outstanding;
        return Core::VFS::VFSResult<void*>::Ok(pointer);
    }

    void CookScratchAllocator::Free(void* pointer) noexcept
    {
        if (!pointer)
        {
            return;
        }
        const auto* arena   = m_workspace.Arena();
        const auto  address = reinterpret_cast<uintptr_t>(pointer);
        ZENGINE_VALIDATE_ASSERT(arena && m_outstanding && address >= reinterpret_cast<uintptr_t>(m_slab.Backing) && address - reinterpret_cast<uintptr_t>(m_slab.Backing) < arena->m_total_size, "Foreign cook scratch allocation.");
        m_slab.Free(pointer);
        --m_outstanding;
    }

    CookMemoryStatistics CookScratchAllocator::Statistics() const noexcept
    {
        return m_parent ? m_parent->Statistics() : CookMemoryStatistics{};
    }

    Core::VFS::VFSResult<size_t> CookScratchAllocator::AllocationFootprint(size_t bytes, size_t alignment, CookMemoryCategory category) const noexcept
    {
        if (!m_parent || !bytes || !alignment || (alignment & (alignment - 1)) || category >= CookMemoryCategory::Count)
        {
            return Core::VFS::VFSResult<size_t>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        const auto bound = Core::Memory::TLSFSlab::AllocationFootprintBound(bytes, alignment);
        return bound ? Core::VFS::VFSResult<size_t>::Ok(bound) : Core::VFS::VFSResult<size_t>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
    }

    Core::VFS::VFSResult<CookAllocatorMemoryRequirements> CookScratchAllocator::MemoryRequirements() const noexcept
    {
        return Requirements(nullptr);
    }

    Core::VFS::VFSResult<CookAllocatorMemoryRequirements> CookScratchAllocator::UncoveredMemoryRequirements(const ICookMemoryCoverage& coverage) const noexcept
    {
        return Requirements(&coverage);
    }

    Core::VFS::VFSResult<CookAllocatorMemoryRequirements> CookScratchAllocator::Requirements(const ICookMemoryCoverage* coverage) const noexcept
    {
        if (!m_parent)
        {
            return Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        const auto parent = coverage ? m_parent->UncoveredMemoryRequirements(*coverage) : m_parent->MemoryRequirements();
        if (parent.Failed())
        {
            return parent;
        }
        const uint64_t own_bytes = coverage && coverage->Contains(this, sizeof(*this)) ? 0 : sizeof(*this);
        if (parent.Value().StateBytes > UINT64_MAX - own_bytes)
        {
            return Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
        }
        uint64_t bytes = parent.Value().PoolBytes;
        if (!parent.Value().IncludesAllocations)
        {
            const auto backing = m_parent->AllocationFootprint(m_backing_bytes, DEFAULT_ALIGNMENT, m_backing_category);
            if (backing.Failed())
            {
                return Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Fail(backing.Error());
            }
            if (backing.Value() < m_backing_bytes)
            {
                return Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Fail(Core::VFS::VFSError::Corrupted);
            }
            if (backing.Value() > UINT64_MAX - bytes)
            {
                return Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
            }
            bytes += backing.Value();
        }
        return Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Ok({parent.Value().StateBytes + own_bytes, bytes, true});
    }
} // namespace ZEngine::Cook
