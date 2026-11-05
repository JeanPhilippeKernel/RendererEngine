#pragma once
#include <ZEngine/Cook/CookTypes.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/VFS/VFSRelativePath.h>

namespace ZEngine::Cook
{
    enum class CookStage : uint8_t
    {
        Setup = 0,
        Capture,
        Discover,
        Encode,
        Publish,
        Cleanup,
    };

    enum class CookSeverity : uint8_t
    {
        Info = 0,
        Warning,
        Error,
    };

    enum class CookDiagnosticCode : uint8_t
    {
        None = 0,
        InvalidRequest,
        InvalidBudget,
        InvalidPath,
        InputBusy,
        OutputBusy,
        UnsupportedBackend,
        UnsupportedTarget,
        UnsupportedVersion,
        UnsupportedAsset,
        InputChanged,
        IdentityConflict,
        InvalidMetadata,
        InvalidScene,
        InvalidAsset,
        DependencyCycle,
        InvalidManifest,
        CorruptObject,
        MissingDependency,
        BudgetExceeded,
        AllocationFailed,
        IOFailure,
        CacheMiss,
        CleanupWarning,
    };

    struct CookDiagnostic
    {
        CookSeverity                                        Severity       = CookSeverity::Error;
        CookStage                                           Stage          = CookStage::Setup;
        CookDiagnosticCode                                  Code           = CookDiagnosticCode::None;
        Core::VFS::VFSError                                 Underlying     = Core::VFS::VFSError::OK;
        Core::VFS::VFSRelativePath                          Source         = {};
        uuids::uuid                                         SceneUUID      = {};
        uuids::uuid                                         EntityUUID     = {};
        char                                                SchemaKey[128] = {};
        char                                                FieldKey[128]  = {};
        CookAssetIdentity                                   Asset          = {};
        Core::Containers::FixedArray<CookAssetIdentity, 32> Chain          = {};
        uint32_t                                            ChainCount     = 0;
        bool                                                ChainTruncated = false;
        char                                                Message[256]   = {};

        // Only explanatory text may be shortened. Identity keys are validated in
        // full and never silently truncated; failed assignment preserves them.
        [[nodiscard]] bool                                  SetKeys(Core::Containers::StringView schema, Core::Containers::StringView field) noexcept;
        [[nodiscard]] bool                                  SetMessage(Core::Containers::StringView message) noexcept;
        [[nodiscard]] bool                                  SetChain(Core::Containers::ArrayView<const CookAssetIdentity> chain) noexcept;
    };

    // Caller-owned fixed storage; include sizeof(CookDiagnostics) in the run's
    // checked ledger. Overflow cannot allocate, wrap counters, or hide an error.
    struct CookDiagnostics
    {
        Core::Containers::FixedArray<CookDiagnostic, 64> Entries        = {};
        uint64_t                                         DroppedEntries = 0;
        uint64_t                                         ErrorCount     = 0;
        uint32_t                                         Count          = 0;

        void                                             Clear() noexcept;
        void                                             Add(const CookDiagnostic& diagnostic) noexcept;
        // Construct directly in retained caller storage, without allocating or
        // creating a second record. Full buffers return null but still count the
        // error/drop with the same warning normalization and saturation as Add.
        // The pointer lasts until Clear/destruction. Fill provenance/message in
        // place; keep Code/Severity unchanged because counters are already final.
        [[nodiscard]] CookDiagnostic*                    Emplace(CookStage stage, CookDiagnosticCode code, Core::VFS::VFSError underlying = Core::VFS::VFSError::OK, CookSeverity severity = CookSeverity::Error) noexcept;
        [[nodiscard]] bool                               HasErrors() const noexcept
        {
            return ErrorCount != 0;
        }

    private:
        [[nodiscard]] CookDiagnostic* Retain(CookSeverity severity) noexcept;
    };

    [[nodiscard]] Core::VFS::VFSError CookDiagnosticError(CookDiagnosticCode code, Core::VFS::VFSError underlying = Core::VFS::VFSError::OK) noexcept;
} // namespace ZEngine::Cook
