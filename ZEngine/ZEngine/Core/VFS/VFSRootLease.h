#pragma once
#include <ZEngine/Core/VFS/IVFSFile.h>
#include <ZEngine/Core/VFS/VFSRelativePath.h>
#include <ZEngine/Core/VFS/VFSWorkingMemory.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <mutex>

namespace ZEngine::Core::VFS
{
    enum class VFSPublicationGuarantees : uint32_t
    {
        None                     = 0,
        ExclusiveFileCreate      = BIT(0),
        ExclusiveDirectoryCreate = BIT(1),
        AtomicReplace            = BIT(2),
        AtomicNoReplace          = BIT(3),
        ReplaceCompatibleReads   = BIT(4),
        CheckedClose             = BIT(5),
        StableRootedPaths        = BIT(6),
        BoundedEnumeration       = BIT(7),
        StableFileIdentity       = BIT(8),
    };

    constexpr VFSPublicationGuarantees operator|(VFSPublicationGuarantees a, VFSPublicationGuarantees b)
    {
        return static_cast<VFSPublicationGuarantees>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    constexpr bool HasGuarantees(VFSPublicationGuarantees value, VFSPublicationGuarantees required)
    {
        return (static_cast<uint32_t>(value) & static_cast<uint32_t>(required)) == static_cast<uint32_t>(required);
    }

    inline constexpr auto VFS_SCRATCH_GUARANTEES     = VFSPublicationGuarantees::ExclusiveFileCreate | VFSPublicationGuarantees::ExclusiveDirectoryCreate | VFSPublicationGuarantees::CheckedClose | VFSPublicationGuarantees::StableRootedPaths;
    inline constexpr auto VFS_PUBLICATION_GUARANTEES = VFS_SCRATCH_GUARANTEES | VFSPublicationGuarantees::AtomicReplace | VFSPublicationGuarantees::AtomicNoReplace | VFSPublicationGuarantees::ReplaceCompatibleReads;
    inline constexpr auto VFS_READ_GUARANTEES        = VFSPublicationGuarantees::CheckedClose | VFSPublicationGuarantees::StableRootedPaths | VFSPublicationGuarantees::ReplaceCompatibleReads | VFSPublicationGuarantees::BoundedEnumeration | VFSPublicationGuarantees::StableFileIdentity;

    enum class VFSRootAccess : uint8_t
    {
        ReadOnly = 0,
        Scratch,
        Publication,
    };

    struct VFSRootEntry
    {
        VFSRelativePath Path     = {};
        VFSFileStat     Stat     = {};
        VFSFileIdentity Identity = {};
    };
    using VFSRootVisitorFn = VFSResult<void> (*)(void*, const VFSRootEntry&);

    struct VFSPublicationCapabilities
    {
        uint64_t                 BackendIdentity = 0;
        VFSPublicationGuarantees Guarantees      = VFSPublicationGuarantees::None;
    };

    // Backend operational state, not the backend facade or its arena. Only mount
    // acquisition can install it in a lease. Implementations retain native anchors.
    struct IVFSRootOperations
    {
        virtual ~IVFSRootOperations() = default;
        virtual VFSResult<VFSRootMemoryRequirements> MemoryRequirements() const
        {
            return VFSResult<VFSRootMemoryRequirements>::Fail(VFSError::Unsupported);
        }
        virtual VFSPublicationCapabilities Capabilities() const                                                 = 0;
        virtual VFSResult<IVFSFile*>       Open(const VFSRelativePath&, VFSOpenFlags)                           = 0;
        virtual VFSResult<VFSFileStat>     Stat(const VFSRelativePath&) const                                   = 0;
        virtual VFSResult<void>            CreateDir(const VFSRelativePath&, bool exclusive)                    = 0;
        virtual VFSResult<void>            Remove(const VFSRelativePath&)                                       = 0;
        virtual VFSResult<void>            Rename(const VFSRelativePath&, const VFSRelativePath&, bool replace) = 0;
        virtual VFSResult<void>            CloseChecked(IVFSFile*)                                              = 0;
        virtual VFSResult<VFSFileIdentity> RootIdentity() const
        {
            return VFSResult<VFSFileIdentity>::Fail(VFSError::Unsupported);
        }
        virtual VFSResult<VFSFileIdentity> Identity(const VFSRelativePath&) const
        {
            return VFSResult<VFSFileIdentity>::Fail(VFSError::Unsupported);
        }
        // Null directory selects the acquired root, not an invalid-path sentinel.
        // No callback is made for entry maximum_entries + 1. The buffer belongs
        // to the caller's budget and is reused; callbacks must not overwrite it.
        virtual VFSResult<void> Visit(const VFSRelativePath*, uint64_t, Containers::ArrayView<uint8_t>, VFSRootVisitorFn, void*) const
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        virtual VFSResult<bool> Overlaps(const IVFSRootOperations&) const
        {
            return VFSResult<bool>::Fail(VFSError::Unsupported);
        }
        // Acquire an existing descendant from this anchor, without live mount
        // resolution or creating directories. Authority may not be promoted.
        virtual VFSResult<Helpers::Scope<IVFSRootOperations>> AcquireChild(const VFSRelativePath&)
        {
            return VFSResult<Helpers::Scope<IVFSRootOperations>>::Fail(VFSError::Unsupported);
        }
    };

    // Retained by mounts before the facade can be destroyed. Retire and acquisition
    // serialize; existing operations remain usable after retirement. No arena borrow.
    class IVFSRootProvider : public Helpers::RefCounted
    {
    public:
        [[nodiscard]] VFSResult<Helpers::Scope<IVFSRootOperations>> Acquire(const VFSPath& relative_root, VFSRootAccess access);
        [[nodiscard]] virtual VFSResult<VFSRootMemoryRequirements>  MemoryRequirements() const
        {
            return VFSResult<VFSRootMemoryRequirements>::Fail(VFSError::Unsupported);
        }
        void                   Retire();
        [[nodiscard]] uint64_t BackendIdentity() const
        {
            return m_identity;
        }

    protected:
        IVFSRootProvider();
        virtual VFSResult<Helpers::Scope<IVFSRootOperations>> AcquireRoot(const VFSPath&, VFSRootAccess) = 0;

    private:
        std::mutex m_mutex    = {};
        bool       m_retired  = false;
        uint64_t   m_identity = 0;
    };

    struct VFSMountState;
    struct VFSMountTable;

    class VFSRootLease
    {
    public:
        VFSRootLease(const VFSRootLease&)                                       = delete;
        VFSRootLease&                            operator=(const VFSRootLease&) = delete;

        [[nodiscard]] bool                       IsValid() const;
        // All wrappers must be closed and owned cleanup finished before release.
        void                                     Reset() noexcept;
        [[nodiscard]] VFSResult<IVFSFile*>       Open(const VFSRelativePath&, VFSOpenFlags);
        [[nodiscard]] VFSResult<VFSFileStat>     Stat(const VFSRelativePath&) const;
        [[nodiscard]] VFSResult<bool>            Exists(const VFSRelativePath&) const;
        [[nodiscard]] VFSResult<void>            CreateDir(const VFSRelativePath&);
        [[nodiscard]] VFSResult<void>            CreateDirExclusive(const VFSRelativePath&);
        [[nodiscard]] VFSResult<void>            Remove(const VFSRelativePath&);
        void                                     Close(IVFSFile*);
        [[nodiscard]] VFSResult<void>            CloseChecked(IVFSFile*);
        [[nodiscard]] VFSResult<VFSFileIdentity> RootIdentity() const;
        [[nodiscard]] VFSResult<VFSFileIdentity> Identity(const VFSRelativePath&) const;
        [[nodiscard]] VFSResult<void>            VisitRoot(uint64_t maximum_entries, Containers::ArrayView<uint8_t> buffer, VFSRootVisitorFn visitor, void* context) const;
        [[nodiscard]] VFSResult<void>            VisitDirectory(const VFSRelativePath&, uint64_t maximum_entries, Containers::ArrayView<uint8_t> buffer, VFSRootVisitorFn visitor, void* context) const;
        [[nodiscard]] VFSResult<bool>            Overlaps(const VFSRootLease& other) const;
        // Account borrowed authority without changing its allocator/lifetime.
        [[nodiscard]] VFSResult<void>            ReserveWorkingMemory(IVFSWorkingMemoryBudget&, VFSWorkingMemoryReservation*) const;

    private:
        friend class VFSPublicationRootLease;
        friend class VFSScratchRootLease;
        friend class VFSReadRootLease;
        friend struct VFSMountTable;
        // Only the final authority types can construct/move this base. A
        // scratch lease must not be promoted by assigning through a base reference.
        VFSRootLease() noexcept;
        ~VFSRootLease();
        VFSRootLease(VFSRootLease&& other) noexcept;
        VFSRootLease&                            operator=(VFSRootLease&& other) noexcept;
        [[nodiscard]] VFSPublicationCapabilities Capabilities() const;
        [[nodiscard]] VFSResult<void>            RenamePaths(const VFSRelativePath&, const VFSRelativePath&, bool replace);
        [[nodiscard]] VFSResult<void>            AcquireChildRoot(const VFSRelativePath&, VFSRootLease*, VFSRootAccess);
        Helpers::Scope<IVFSRootOperations>       m_operations = {};
        VFSWorkingMemoryReservation              m_memory     = {};
        Helpers::Ref<VFSMountState>              m_mounts     = {};
        uint32_t                                 m_pin        = UINT32_MAX;
    };

    // Private inheritance exposes no create/remove/rename/write authority and
    // prevents promotion/assignment through the writable base.
    class VFSReadRootLease final : private VFSRootLease
    {
    public:
        VFSReadRootLease()                                       = default;
        VFSReadRootLease(VFSReadRootLease&&) noexcept            = default;
        VFSReadRootLease& operator=(VFSReadRootLease&&) noexcept = default;
        using VFSRootLease::Close;
        using VFSRootLease::CloseChecked;
        using VFSRootLease::Exists;
        using VFSRootLease::Identity;
        using VFSRootLease::IsValid;
        using VFSRootLease::ReserveWorkingMemory;
        using VFSRootLease::Reset;
        using VFSRootLease::RootIdentity;
        using VFSRootLease::Stat;
        using VFSRootLease::VisitDirectory;
        using VFSRootLease::VisitRoot;
        [[nodiscard]] VFSResult<IVFSFile*> Open(const VFSRelativePath& path)
        {
            return VFSRootLease::Open(path, VFSOpenFlags::Read | VFSOpenFlags::AllowReplace);
        }
        [[nodiscard]] VFSResult<bool> Overlaps(const VFSRootLease& other) const
        {
            return VFSRootLease::Overlaps(other);
        }
        [[nodiscard]] VFSResult<bool> Overlaps(const VFSReadRootLease& other) const
        {
            return VFSRootLease::Overlaps(other);
        }
        [[nodiscard]] VFSResult<void> AcquireChildRoot(const VFSRelativePath& path, VFSReadRootLease* output)
        {
            return VFSRootLease::AcquireChildRoot(path, output, VFSRootAccess::ReadOnly);
        }

    private:
        friend struct VFSMountTable;
    };

    class VFSScratchRootLease final : public VFSRootLease
    {
    public:
        VFSScratchRootLease()                                                   = default;
        VFSScratchRootLease(VFSScratchRootLease&&) noexcept                     = default;
        VFSScratchRootLease&          operator=(VFSScratchRootLease&&) noexcept = default;
        [[nodiscard]] VFSResult<void> AcquireChildRoot(const VFSRelativePath& path, VFSScratchRootLease* output)
        {
            return VFSRootLease::AcquireChildRoot(path, output, VFSRootAccess::Scratch);
        }
    };

    class VFSPublicationRootLease final : public VFSRootLease
    {
    public:
        VFSPublicationRootLease()                                                              = default;
        VFSPublicationRootLease(VFSPublicationRootLease&&) noexcept                            = default;
        VFSPublicationRootLease&                 operator=(VFSPublicationRootLease&&) noexcept = default;
        [[nodiscard]] VFSPublicationCapabilities QueryPublicationCapabilities() const
        {
            return Capabilities();
        }
        [[nodiscard]] VFSResult<void> Rename(const VFSRelativePath& from, const VFSRelativePath& to)
        {
            return RenamePaths(from, to, true);
        }
        [[nodiscard]] VFSResult<void> RenameNoReplace(const VFSRelativePath& from, const VFSRelativePath& to)
        {
            return RenamePaths(from, to, false);
        }
        [[nodiscard]] VFSResult<void> AcquireChildRoot(const VFSRelativePath& path, VFSPublicationRootLease* output)
        {
            return VFSRootLease::AcquireChildRoot(path, output, VFSRootAccess::Publication);
        }
    };
} // namespace ZEngine::Core::VFS
