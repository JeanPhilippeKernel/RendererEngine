#include <ZEngine/Cook/CookLibraryExclusion.h>
#include <limits>
#include <utility>

namespace ZEngine::Cook
{
    CookLibraryReadLease::~CookLibraryReadLease()
    {
        Reset();
    }
    CookLibraryReadLease::CookLibraryReadLease(CookLibraryReadLease&& other) noexcept : m_owner(std::exchange(other.m_owner, nullptr)), m_generation(std::exchange(other.m_generation, 0)) {}
    CookLibraryReadLease& CookLibraryReadLease::operator=(CookLibraryReadLease&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_owner      = std::exchange(other.m_owner, nullptr);
            m_generation = std::exchange(other.m_generation, 0);
        }
        return *this;
    }
    void CookLibraryReadLease::Reset() noexcept
    {
        if (m_owner)
            m_owner->ReleaseRead();
        m_owner      = nullptr;
        m_generation = 0;
    }

    CookLibraryMutationLease::~CookLibraryMutationLease()
    {
        Reset();
    }
    CookLibraryMutationLease::CookLibraryMutationLease(CookLibraryMutationLease&& other) noexcept : m_owner(std::exchange(other.m_owner, nullptr)) {}
    CookLibraryMutationLease& CookLibraryMutationLease::operator=(CookLibraryMutationLease&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_owner = std::exchange(other.m_owner, nullptr);
        }
        return *this;
    }
    void CookLibraryMutationLease::Reset() noexcept
    {
        if (m_owner)
            m_owner->ReleaseMutation();
        m_owner = nullptr;
    }

    CookLibraryExclusion::~CookLibraryExclusion()
    {
        ZENGINE_VALIDATE_ASSERT(!m_readers && !m_mutating, "Cook library owner destroyed before its leases.");
    }

    Core::VFS::VFSResult<void> CookLibraryExclusion::TryAcquireRead(CookLibraryReadLease* output)
    {
        if (!output)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        if (output->IsValid())
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::AlreadyExists);
        std::lock_guard lock(m_mutex);
        if (m_mutating)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::AlreadyExists);
        if (m_readers == std::numeric_limits<uint32_t>::max())
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
        ++m_readers;
        output->m_owner      = this;
        output->m_generation = m_generation;
        return Core::VFS::VFSResult<void>::Ok();
    }

    Core::VFS::VFSResult<void> CookLibraryExclusion::TryAcquireMutation(CookLibraryMutationLease* output)
    {
        if (!output)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        if (output->IsValid())
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::AlreadyExists);
        std::lock_guard lock(m_mutex);
        if (m_mutating || m_readers)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::AlreadyExists);
        if (m_generation == std::numeric_limits<uint64_t>::max())
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
        m_mutating      = true;
        output->m_owner = this;
        return Core::VFS::VFSResult<void>::Ok();
    }

    void CookLibraryExclusion::ReleaseRead() noexcept
    {
        std::lock_guard lock(m_mutex);
        ZENGINE_VALIDATE_ASSERT(m_readers && !m_mutating, "Invalid cook read lease release.");
        --m_readers;
    }

    void CookLibraryExclusion::ReleaseMutation() noexcept
    {
        std::lock_guard lock(m_mutex);
        ZENGINE_VALIDATE_ASSERT(m_mutating && !m_readers, "Invalid cook mutation lease release.");
        // Advancing even for an abandoned/no-op mutation conservatively prevents
        // an identity receipt from being reused across a writable-owner interval.
        ++m_generation;
        m_mutating = false;
    }
} // namespace ZEngine::Cook
