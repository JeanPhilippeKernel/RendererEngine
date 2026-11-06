#pragma once
#include <ZEngine/Core/VFS/VFSError.h>
#include <mutex>

namespace ZEngine::Cook
{
    class CookLibraryExclusion;

    // Non-forgeable cooperation tokens, not a claim of OS protection against
    // native writers. The offline owner must control every cooperating writer.
    class CookLibraryReadLease
    {
    public:
        CookLibraryReadLease() = default;
        ~CookLibraryReadLease();
        CookLibraryReadLease(const CookLibraryReadLease&)            = delete;
        CookLibraryReadLease& operator=(const CookLibraryReadLease&) = delete;
        CookLibraryReadLease(CookLibraryReadLease&& other) noexcept;
        CookLibraryReadLease& operator=(CookLibraryReadLease&& other) noexcept;
        [[nodiscard]] bool    IsValid() const noexcept
        {
            return m_owner != nullptr;
        }
        [[nodiscard]] uint64_t Generation() const noexcept
        {
            return m_generation;
        }
        void Reset() noexcept;

    private:
        friend class CookLibraryExclusion;
        CookLibraryExclusion* m_owner      = nullptr;
        uint64_t              m_generation = 0;
    };

    class CookLibraryMutationLease
    {
    public:
        CookLibraryMutationLease() = default;
        ~CookLibraryMutationLease();
        CookLibraryMutationLease(const CookLibraryMutationLease&)            = delete;
        CookLibraryMutationLease& operator=(const CookLibraryMutationLease&) = delete;
        CookLibraryMutationLease(CookLibraryMutationLease&& other) noexcept;
        CookLibraryMutationLease& operator=(CookLibraryMutationLease&& other) noexcept;
        [[nodiscard]] bool        IsValid() const noexcept
        {
            return m_owner != nullptr;
        }
        void Reset() noexcept;

    private:
        friend class CookLibraryExclusion;
        CookLibraryExclusion* m_owner = nullptr;
    };

    // Inline owner state: no heap, waiting for another lease, or callback on
    // release. A mutex protects the compound state; separate atomics (padded or
    // otherwise) would not make read-vs-mutation acquisition atomic. Independent
    // cook readers may coexist. The owner must outlive all of its tokens.
    class CookLibraryExclusion
    {
    public:
        CookLibraryExclusion() = default;
        ~CookLibraryExclusion();
        CookLibraryExclusion(const CookLibraryExclusion&)                               = delete;
        CookLibraryExclusion&                    operator=(const CookLibraryExclusion&) = delete;

        [[nodiscard]] Core::VFS::VFSResult<void> TryAcquireRead(CookLibraryReadLease* output);
        [[nodiscard]] Core::VFS::VFSResult<void> TryAcquireMutation(CookLibraryMutationLease* output);

    private:
        friend class CookLibraryReadLease;
        friend class CookLibraryMutationLease;
        void       ReleaseRead() noexcept;
        void       ReleaseMutation() noexcept;
        std::mutex m_mutex      = {};
        uint64_t   m_generation = 1;
        uint32_t   m_readers    = 0;
        bool       m_mutating   = false;
    };
} // namespace ZEngine::Cook
