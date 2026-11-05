#include <ZEngine/Cook/CookCodecAllocationRoute.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <algorithm>

namespace ZEngine::Cook
{
    CookCodecAllocationRoute::~CookCodecAllocationRoute()
    {
        ZENGINE_VALIDATE_ASSERT(!m_working.IsActive(), "Codec allocation route destroyed before its fixed working reservations.");
    }

    bool CookCodecAllocationRoute::TryReserveWorkingBytes(size_t bytes) noexcept
    {
        if (m_error != Core::VFS::VFSError::OK)
            return false;
        if (!bytes)
            return true;
        if (bytes > UINT64_MAX - m_working.Bytes())
        {
            m_error = Core::VFS::VFSError::SizeLimitExceeded;
            return false;
        }
        const uint64_t total  = m_working.Bytes() + bytes;
        const auto     status = m_working.IsActive() ? m_working.TryResize(total) : m_allocator->TryCharge(total, CookMemoryCategory::Codecs, &m_working);
        if (status.Failed())
            m_error = status.Error();
        return status.Succeeded();
    }

    void CookCodecAllocationRoute::ReleaseWorkingBytes(size_t bytes) noexcept
    {
        if (!bytes)
            return;
        ZENGINE_VALIDATE_ASSERT(m_working.IsActive() && bytes <= m_working.Bytes(), "Codec fixed working reservation released twice or through the wrong route.");
        [[maybe_unused]] const auto status = m_working.TryResize(m_working.Bytes() - bytes);
        ZENGINE_VALIDATE_ASSERT(status.Succeeded(), "Shrinking a codec fixed working reservation cannot fail.");
    }

    void* CookCodecAllocationRoute::Allocate(size_t bytes) noexcept
    {
        if (m_error != Core::VFS::VFSError::OK)
            return nullptr;
        const auto allocation = m_allocator->Allocate(bytes, DEFAULT_ALIGNMENT, CookMemoryCategory::Codecs);
        if (allocation.Failed())
        {
            m_error = allocation.Error();
            return nullptr;
        }
        if (!allocation.Value())
            m_error = Core::VFS::VFSError::OutOfMemory;
        return allocation.Value();
    }

    void* CookCodecAllocationRoute::Reallocate(void* pointer, size_t old_bytes, size_t new_bytes) noexcept
    {
        if (!pointer)
            return Allocate(new_bytes);
        if (!new_bytes)
        {
            Free(pointer);
            return nullptr;
        }
        void* replacement = Allocate(new_bytes);
        if (!replacement)
            return nullptr;
        const auto count = std::min(old_bytes, new_bytes);
        if (count && Helpers::secure_memcpy(replacement, new_bytes, pointer, count) != Helpers::MEMORY_OP_SUCCESS)
        {
            Free(replacement);
            m_error = Core::VFS::VFSError::Corrupted;
            return nullptr;
        }
        Free(pointer);
        return replacement;
    }

    void CookCodecAllocationRoute::Free(void* pointer) noexcept
    {
        if (pointer)
            m_allocator->Free(pointer);
    }
} // namespace ZEngine::Cook
