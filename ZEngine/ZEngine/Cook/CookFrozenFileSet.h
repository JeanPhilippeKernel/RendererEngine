#pragma once
#include <ZEngine/Cook/CookCapturedFile.h>
#include <ZEngine/Cook/CookFrozenLibraryOwner.h>

namespace ZEngine::Cook
{
    struct CookFrozenFileRecord
    {
        // A one-component path represents the declared root itself, never a
        // file that an adapter can capture. All other paths are key/relative.
        Core::VFS::VFSRelativePath Path          = {};
        Core::VFS::VFSFileIdentity Identity      = {};
        Core::VFS::VFSFileStat     Stat          = {};
        Digest256                  CaptureDigest = {};
        uint32_t                   RootIndex     = 0;
        bool                       Captured      = false;
    };

    // Run-owned bounded discovery/receipt index. Iterative traversal reuses one
    // caller-charged directory buffer; no recursive native listings or per-file
    // allocation. Checked geometric growth charges both old/new blocks while
    // copying. Published records are stationary and sorted for binary lookup.
    // The caller includes this object's inline state in its fixed run accounting.
    class CookFrozenFileSet
    {
    public:
        CookFrozenFileSet() = default;
        ~CookFrozenFileSet();
        CookFrozenFileSet(const CookFrozenFileSet&)                                                               = delete;
        CookFrozenFileSet&                                                    operator=(const CookFrozenFileSet&) = delete;

        // maximum_records includes roots/directories, not just cook assets. The
        // provider derives this retained-index bound from its working budget;
        // MaxCatalogEntries remains the independent typed-catalog hard cap.
        [[nodiscard]] Core::VFS::VFSResult<void>                              Build(ICookAllocator&, const CookFrozenLibraryReadLease&, size_t maximum_records, Core::Containers::ArrayView<uint8_t> enumeration_buffer);
        [[nodiscard]] Core::Containers::ArrayView<const CookFrozenFileRecord> Records() const noexcept
        {
            return {m_records, m_count};
        }
        [[nodiscard]] const CookFrozenFileRecord* Find(const Core::VFS::VFSRelativePath&) const noexcept;
        // Each exposed capture has a matching source identity/digest receipt.
        // Store/scratch/captures are owned by the run, not by this input index.
        [[nodiscard]] Core::VFS::VFSResult<void>  Capture(const CookFrozenLibraryReadLease&, CookCaptureStore&, const Core::VFS::VFSRelativePath&, uint64_t maximum_bytes, CookCapturedFile*);
        // No retained second index: re-enumerate each known directory, compare
        // identities/counts through binary lookup, then rehash capture receipts.
        // Changed namespace/content is Corrupted for the provider to diagnose as
        // InputChanged. Native I/O/close/allocation errors retain their category.
        // Optional failure source is a changed file or the directory whose
        // membership could not be verified. Success preserves the caller value.
        [[nodiscard]] Core::VFS::VFSResult<void>  Verify(const CookFrozenLibraryReadLease&, CookCaptureStore&, Core::Containers::ArrayView<uint8_t> enumeration_buffer, Core::VFS::VFSRelativePath* failure_source = nullptr) const;
        void                                      Reset() noexcept;

    private:
        [[nodiscard]] Core::VFS::VFSResult<void> Append(const CookFrozenFileRecord&);
        ICookAllocator*                          m_allocator  = nullptr;
        CookFrozenFileRecord*                    m_records    = nullptr;
        size_t                                   m_count      = 0;
        size_t                                   m_capacity   = 0;
        size_t                                   m_limit      = 0;
        uint64_t                                 m_generation = 0;
        uint32_t                                 m_root_count = 0;
    };
} // namespace ZEngine::Cook
