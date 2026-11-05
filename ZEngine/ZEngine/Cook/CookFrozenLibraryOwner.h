#pragma once
#include <ZEngine/Cook/CookLibraryExclusion.h>
#include <ZEngine/Cook/CookMemory.h>
#include <ZEngine/Cook/CookTypes.h>
#include <ZEngine/Core/VFS/IVFSContext.h>

namespace ZEngine::Cook
{
    struct CookFrozenRootRequest
    {
        Core::Containers::StringView Key     = {};
        Core::VFS::IVFSContext*      Context = nullptr;
        Core::VFS::VFSPath           Path    = {};
    };

    class CookFrozenLibraryOwner;

    // Inline borrowed read authority. The owner/allocator outlive every reader;
    // this token never depends on the original context/backend facade's lifetime.
    class CookFrozenLibraryReadLease
    {
    public:
        CookFrozenLibraryReadLease() = default;
        ~CookFrozenLibraryReadLease();
        CookFrozenLibraryReadLease(const CookFrozenLibraryReadLease&)            = delete;
        CookFrozenLibraryReadLease& operator=(const CookFrozenLibraryReadLease&) = delete;
        CookFrozenLibraryReadLease(CookFrozenLibraryReadLease&&) noexcept;
        CookFrozenLibraryReadLease& operator=(CookFrozenLibraryReadLease&&) noexcept;

        [[nodiscard]] bool          IsValid() const noexcept
        {
            return m_owner != nullptr;
        }
        [[nodiscard]] uint64_t Generation() const noexcept
        {
            return m_exclusion.Generation();
        }
        [[nodiscard]] uint32_t                                         RootCount() const noexcept;
        [[nodiscard]] Core::Containers::StringView                     RootKey(uint32_t index) const noexcept;
        [[nodiscard]] Core::VFS::VFSResult<Core::VFS::VFSFileIdentity> RootIdentity(uint32_t index) const;
        // Resolves exact logical spelling (key/relative), never provenance/native
        // paths. Root indices remain stable until this token is reset.
        [[nodiscard]] Core::VFS::VFSResult<uint32_t>                   Resolve(const Core::VFS::VFSRelativePath&, Core::VFS::VFSRelativePath* relative) const;
        // Explicit conversion from an already-normalized caller context path.
        // Ambiguous mappings across distinct contexts fail, never choose first.
        [[nodiscard]] Core::VFS::VFSResult<void>                       MapContextPath(const Core::VFS::VFSPath&, Core::VFS::VFSRelativePath*) const;
        [[nodiscard]] Core::VFS::VFSResult<Core::VFS::IVFSFile*>       Open(const Core::VFS::VFSRelativePath&, uint32_t* root_index) const;
        [[nodiscard]] Core::VFS::VFSResult<void>                       CloseChecked(uint32_t root_index, Core::VFS::IVFSFile*) const;
        [[nodiscard]] Core::VFS::VFSResult<Core::VFS::VFSFileStat>     Stat(const Core::VFS::VFSRelativePath&) const;
        [[nodiscard]] Core::VFS::VFSResult<Core::VFS::VFSFileIdentity> Identity(const Core::VFS::VFSRelativePath&) const;
        // Entry paths are relative to the selected root; no mutable root lease
        // escapes (a borrower must not Reset/remap the owner's root authority).
        [[nodiscard]] Core::VFS::VFSResult<void>                       Visit(uint32_t root_index, const Core::VFS::VFSRelativePath* directory, uint64_t maximum_entries, Core::Containers::ArrayView<uint8_t>, Core::VFS::VFSRootVisitorFn, void*) const;
        [[nodiscard]] Core::VFS::VFSResult<void>                       RequireDisjoint(const Core::VFS::VFSRootLease&) const;
        [[nodiscard]] Core::VFS::VFSResult<void>                       ReserveRootWorkingMemory(uint32_t, Core::VFS::IVFSWorkingMemoryBudget&, Core::VFS::VFSWorkingMemoryReservation*) const;
        // Close every opened file before releasing exclusion.
        void                                                           Reset() noexcept;

    private:
        friend class CookFrozenLibraryOwner;
        [[nodiscard]] Core::VFS::VFSReadRootLease* Root(uint32_t index) const noexcept;
        CookFrozenLibraryOwner*                    m_owner     = nullptr;
        CookLibraryReadLease                       m_exclusion = {};
    };

    // Caller grants exclusive cooperating control of an OFFLINE library. Every
    // cooperating writer must hold TryAcquireMutation; this is not an OS sandbox
    // or an editor-import freeze. Configure only under quiescence. One checked
    // allocation holds the actual root count, sorted by logical key; read tokens
    // allocate nothing. Reset/remap and mutations fail busy while readers exist.
    class CookFrozenLibraryOwner
    {
    public:
        CookFrozenLibraryOwner() = default;
        ~CookFrozenLibraryOwner();
        CookFrozenLibraryOwner(const CookFrozenLibraryOwner&)                                 = delete;
        CookFrozenLibraryOwner&                      operator=(const CookFrozenLibraryOwner&) = delete;

        [[nodiscard]] Core::VFS::VFSResult<void>     Initialize(ICookAllocator&, Core::Containers::ArrayView<const CookFrozenRootRequest>);
        [[nodiscard]] Core::VFS::VFSResult<void>     TryAcquireRead(CookFrozenLibraryReadLease*);
        [[nodiscard]] Core::VFS::VFSResult<void>     TryAcquireMutation(CookLibraryMutationLease*);
        [[nodiscard]] Core::VFS::VFSResult<void>     Reset() noexcept;
        // Caller holds this configuration stable for a cook. Root operations,
        // shared VFS pools/mount tables are counted separately by the VFS policy.
        // Deduplicates the covered allocator chain; separately owned binding
        // backing includes full headers/slack/page cost, not just the payload.
        // Optional native-service proof discounts complete fixed objects only;
        // root-binding backing remains charged unless inside a retained pool.
        [[nodiscard]] Core::VFS::VFSResult<uint64_t> FixedWorkingBytes(const ICookAllocator& covered, const ICookMemoryCoverage* coverage = nullptr) const noexcept;

    private:
        friend class CookFrozenLibraryReadLease;
        struct RootBinding
        {
            char                        Key[33]     = {};
            Core::VFS::VFSReadRootLease Lease       = {};
            Core::VFS::VFSPath          ContextPath = {};
        };

        CookLibraryExclusion m_exclusion = {};
        ICookAllocator*      m_allocator = nullptr;
        RootBinding*         m_roots     = nullptr;
        uint32_t             m_count     = 0;
    };
} // namespace ZEngine::Cook
