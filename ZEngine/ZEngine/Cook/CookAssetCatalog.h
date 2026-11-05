#pragma once
#include <ZEngine/Cook/CookInputLease.h>

namespace ZEngine::Cook
{
    // Owned copy-out records, never borrowed registry nodes. Capacity is the
    // actual bounded enumeration count, not an automatic hard-cap reservation.
    // One checked allocation contains records and four compact sorted indexes;
    // records never move while sealing/looking up/enumerating the catalog.
    class CookAssetCatalog
    {
    public:
        CookAssetCatalog() = default;
        ~CookAssetCatalog();
        CookAssetCatalog(const CookAssetCatalog&)                                   = delete;
        CookAssetCatalog&                        operator=(const CookAssetCatalog&) = delete;

        [[nodiscard]] Core::VFS::VFSResult<void> Initialize(ICookAllocator& allocator, uint32_t capacity, uint32_t maximum_entries);
        [[nodiscard]] Core::VFS::VFSResult<void> Append(const CookAssetCatalogEntry& entry, CookDiagnostics* diagnostics);
        // Multi-entry indexes share one charged, constant-storage iterative sort.
        // A reservation failure leaves the catalog building and retryable.
        [[nodiscard]] Core::VFS::VFSResult<void> Finalize(CookDiagnostics* diagnostics);
        void                                     Reset() noexcept;

        [[nodiscard]] bool                       IsFinalized() const noexcept;
        [[nodiscard]] uint32_t                   Count() const noexcept
        {
            return m_count;
        }
        [[nodiscard]] const CookAssetCatalogEntry* Find(const CookAssetIdentity& identity) const noexcept;
        [[nodiscard]] const CookAssetCatalogEntry* FindArtifact(const Core::VFS::VFSRelativePath& path) const noexcept;
        [[nodiscard]] Core::VFS::VFSResult<void>   Enumerate(CookCatalogVisitorFn visitor, void* context) const;

    private:
        enum class State : uint8_t
        {
            Empty = 0,
            Building,
            Finalized,
            Failed,
        };

        ICookAllocator*        m_allocator   = nullptr;
        CookAssetCatalogEntry* m_records     = nullptr;
        uint32_t*              m_by_identity = nullptr;
        uint32_t*              m_by_uuid     = nullptr;
        uint32_t*              m_by_artifact = nullptr;
        uint32_t*              m_by_metadata = nullptr;
        uint32_t               m_count       = 0;
        uint32_t               m_capacity    = 0;
        State                  m_state       = State::Empty;
    };
} // namespace ZEngine::Cook
