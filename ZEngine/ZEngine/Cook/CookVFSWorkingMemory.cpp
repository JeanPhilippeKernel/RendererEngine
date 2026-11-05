#include <ZEngine/Cook/CookVFSWorkingMemory.h>
#include <utility>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    CookVFSWorkingMemory::~CookVFSWorkingMemory()
    {
        ZENGINE_VALIDATE_ASSERT(!m_reservations, "VFS budget destroyed before its root reservations.");
        (void) Reset();
    }

    VFS::VFSResult<void> CookVFSWorkingMemory::Initialize(CookBudgetAllocator& allocator)
    {
        if (m_allocator)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::AlreadyExists);
        const auto charged = allocator.TryCharge(sizeof(*this), CookMemoryCategory::StagingManifest, &m_self_charge);
        if (charged.Failed())
            return charged;
        if (!m_inline_arena.InitializeBorrowed(m_inline, sizeof(m_inline), "CookVFSWorkingMemory") || !m_domains.try_init(&m_inline_arena, InlineSlots))
        {
            m_inline_arena.Shutdown();
            m_self_charge.Release();
            return VFS::VFSResult<void>::Fail(VFS::VFSError::OutOfMemory);
        }
        m_allocator = &allocator;
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookVFSWorkingMemory::Reset() noexcept
    {
        if (m_reservations)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::AlreadyExists);
        m_domains = {};
        m_storage[0].Release();
        m_storage[1].Release();
        m_inline_arena.Shutdown();
        m_charge.Release();
        m_self_charge.Release();
        m_allocator      = nullptr;
        m_active_storage = 2;
        m_erased         = 0;
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookVFSWorkingMemory::EnsureCapacity(size_t additions) noexcept
    {
        // Reference-only acquisition must not allocate/compact an unchanged
        // domain set, even if earlier unrelated releases left tombstones.
        if (!additions)
            return VFS::VFSResult<void>::Ok();
        const size_t current = m_domains.capacity();
        if (additions > SIZE_MAX - m_domains.size())
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        const size_t needed = m_domains.size() + additions;
        if (needed <= current / 2 && m_erased < current / 4)
            return VFS::VFSResult<void>::Ok();
        size_t slots = current;
        while (needed > slots / 2)
        {
            if (slots > SIZE_MAX / 2)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
            slots *= 2;
        }
        if (slots > SIZE_MAX / sizeof(Domains::Entry))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        const uint8_t target      = m_active_storage == 0 ? 1 : 0;
        const auto    initialized = m_storage[target].Initialize(*m_allocator, slots * sizeof(Domains::Entry), CookMemoryCategory::StagingManifest);
        if (initialized.Failed())
            return initialized;
        Domains replacement = {};
        if (!replacement.try_init(m_storage[target].Arena(), slots))
        {
            m_storage[target].Release();
            return VFS::VFSResult<void>::Fail(VFS::VFSError::OutOfMemory);
        }
        for (const auto& entry : m_domains)
            replacement.insert(entry.first, entry.second);
        m_domains = std::move(replacement);
        if (m_active_storage < 2)
            m_storage[m_active_storage].Release();
        m_active_storage = target;
        m_erased         = 0;
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookVFSWorkingMemory::Retain(const VFS::VFSRootMemoryRequirements& requirements) noexcept
    {
        if (!m_allocator)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        if (m_reservations == SIZE_MAX)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        const VFS::VFSSharedMemoryDomain domains[] = {requirements.Storage, requirements.Mapping};
        uint64_t                         bytes     = requirements.StateBytes;
        size_t                           additions = 0;
        for (const auto& domain : domains)
        {
            if (!domain.Identity)
                continue;
            if (const auto* found = m_domains.find(reinterpret_cast<uintptr_t>(domain.Identity)))
            {
                if (found->Bytes != domain.Bytes)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                if (found->References == UINT64_MAX)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
            }
            else
            {
                if (domain.Bytes > UINT64_MAX - bytes)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                bytes += domain.Bytes;
                ++additions;
            }
        }
        if (bytes > UINT64_MAX - m_charge.Bytes())
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        const auto capacity = EnsureCapacity(additions);
        if (capacity.Failed())
            return capacity;
        const auto charged = m_charge.IsActive() ? m_charge.TryResize(m_charge.Bytes() + bytes) : m_allocator->TryCharge(bytes, CookMemoryCategory::StagingManifest, &m_charge);
        if (charged.Failed())
            return charged;
        for (const auto& domain : domains)
        {
            if (!domain.Identity)
                continue;
            const auto key = reinterpret_cast<uintptr_t>(domain.Identity);
            if (auto* found = m_domains.find(key))
                ++found->References;
            else
                m_domains.insert(key, {.Bytes = domain.Bytes, .References = 1});
        }
        ++m_reservations;
        return VFS::VFSResult<void>::Ok();
    }

    void CookVFSWorkingMemory::Release(const VFS::VFSRootMemoryRequirements& requirements) noexcept
    {
        ZENGINE_VALIDATE_ASSERT(m_reservations && requirements.StateBytes <= m_charge.Bytes(), "Foreign VFS reservation release.");
        uint64_t                         bytes     = requirements.StateBytes;
        const VFS::VFSSharedMemoryDomain domains[] = {requirements.Storage, requirements.Mapping};
        for (const auto& domain : domains)
        {
            if (!domain.Identity)
                continue;
            const auto key   = reinterpret_cast<uintptr_t>(domain.Identity);
            auto*      found = m_domains.find(key);
            ZENGINE_VALIDATE_ASSERT(found && found->References && found->Bytes == domain.Bytes, "Foreign shared VFS storage release.");
            if (!--found->References)
            {
                bytes += found->Bytes;
                m_domains.remove(key);
                ++m_erased;
            }
        }
        ZENGINE_VALIDATE_ASSERT(bytes <= m_charge.Bytes(), "Invalid VFS working-memory total.");
        const auto resized = m_charge.TryResize(m_charge.Bytes() - bytes);
        ZENGINE_VALIDATE_ASSERT(resized.Succeeded(), "Shrinking a VFS charge must not fail.");
        --m_reservations;
        if (m_domains.empty())
        {
            m_domains.clear();
            m_erased = 0;
        }
    }
} // namespace ZEngine::Cook
