#include <ZEngine/Cook/CookDiagnostics.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/UTF8.h>
#include <algorithm>
#include <limits>
#include <new>

namespace ZEngine::Cook
{
    namespace
    {
        bool ValidKey(Core::Containers::StringView key)
        {
            return key.size() < 128 && Helpers::IsValidUTF8(key.data(), key.size(), false);
        }

        template <size_t Capacity>
        void CopyText(char (&output)[Capacity], Core::Containers::StringView text, size_t count)
        {
            if (count)
                Helpers::secure_memmove(output, sizeof(output), text.data(), count);
            output[count] = '\0';
            if (count + 1 < Capacity)
                Helpers::secure_memset(output + count + 1, 0, Capacity - count - 1, Capacity - count - 1);
        }

        void IncrementSaturated(uint64_t& value)
        {
            if (value != std::numeric_limits<uint64_t>::max())
                ++value;
        }

        CookSeverity NormalizedSeverity(CookDiagnosticCode code, CookSeverity severity)
        {
            return code == CookDiagnosticCode::CacheMiss || code == CookDiagnosticCode::CleanupWarning ? CookSeverity::Warning : severity;
        }
    } // namespace

    bool CookDiagnostic::SetKeys(Core::Containers::StringView schema, Core::Containers::StringView field) noexcept
    {
        if (!ValidKey(schema) || !ValidKey(field))
            return false;
        // Save the schema before touching either output. CopyText handles field
        // overlap in place; the saved schema makes even crossed aliases safe.
        // One key buffer suffices, rather than snapshotting both outputs.
        char schema_copy[128] = {};
        CopyText(schema_copy, schema, schema.size());
        CopyText(FieldKey, field, field.size());
        Helpers::secure_memcpy(SchemaKey, sizeof(SchemaKey), schema_copy, sizeof(schema_copy));
        return true;
    }

    bool CookDiagnostic::SetMessage(Core::Containers::StringView message) noexcept
    {
        if (!Helpers::IsValidUTF8(message.data(), message.size()))
            return false;
        size_t count = std::min(message.size(), sizeof(Message) - 1);
        // Never cut a multibyte scalar when shortening explanatory text.
        while (count < message.size() && count && (static_cast<uint8_t>(message.data()[count]) & 0xc0u) == 0x80u)
            --count;
        CopyText(Message, message, count);
        return true;
    }

    bool CookDiagnostic::SetChain(Core::Containers::ArrayView<const CookAssetIdentity> chain) noexcept
    {
        if (chain.size() && !chain.data())
            return false;
        for (const auto& identity : chain)
            if (!identity.IsValid())
                return false;
        const auto count = std::min(chain.size(), Chain.size());
        for (size_t i = 0; i < count; ++i)
            Chain[i] = chain[i];
        if (chain.size() > Chain.size())
            Chain[Chain.size() - 1] = chain[chain.size() - 1];
        for (size_t i = count; i < Chain.size(); ++i)
            Chain[i] = {};
        ChainCount     = static_cast<uint32_t>(count);
        ChainTruncated = chain.size() > Chain.size();
        return true;
    }

    void CookDiagnostics::Clear() noexcept
    {
        for (size_t i = 0; i < Count; ++i)
        {
            Entries[i].~CookDiagnostic();
            new (&Entries[i]) CookDiagnostic{};
        }
        Count          = 0;
        ErrorCount     = 0;
        DroppedEntries = 0;
    }

    CookDiagnostic* CookDiagnostics::Retain(CookSeverity severity) noexcept
    {
        if (severity == CookSeverity::Error)
            IncrementSaturated(ErrorCount);
        if (Count < Entries.size())
            return &Entries[Count++];
        IncrementSaturated(DroppedEntries);
        return nullptr;
    }

    void CookDiagnostics::Add(const CookDiagnostic& diagnostic) noexcept
    {
        const auto severity = NormalizedSeverity(diagnostic.Code, diagnostic.Severity);
        if (auto* entry = Retain(severity))
        {
            *entry          = diagnostic;
            entry->Severity = severity;
        }
    }

    CookDiagnostic* CookDiagnostics::Emplace(CookStage stage, CookDiagnosticCode code, Core::VFS::VFSError underlying, CookSeverity severity) noexcept
    {
        severity    = NormalizedSeverity(code, severity);
        auto* entry = Retain(severity);
        if (entry)
        {
            entry->~CookDiagnostic();
            new (entry) CookDiagnostic{.Severity = severity, .Stage = stage, .Code = code, .Underlying = underlying};
        }
        return entry;
    }

    Core::VFS::VFSError CookDiagnosticError(CookDiagnosticCode code, Core::VFS::VFSError underlying) noexcept
    {
        switch (code)
        {
            case CookDiagnosticCode::None:
            case CookDiagnosticCode::CacheMiss:
            case CookDiagnosticCode::CleanupWarning:
                return Core::VFS::VFSError::OK;
            case CookDiagnosticCode::InvalidRequest:
            case CookDiagnosticCode::InvalidBudget:
            case CookDiagnosticCode::InvalidPath:
                return Core::VFS::VFSError::InvalidPath;
            case CookDiagnosticCode::InputBusy:
            case CookDiagnosticCode::OutputBusy:
                return Core::VFS::VFSError::AlreadyExists;
            case CookDiagnosticCode::UnsupportedBackend:
            case CookDiagnosticCode::UnsupportedTarget:
            case CookDiagnosticCode::UnsupportedVersion:
            case CookDiagnosticCode::UnsupportedAsset:
                return Core::VFS::VFSError::Unsupported;
            case CookDiagnosticCode::InputChanged:
            case CookDiagnosticCode::IdentityConflict:
            case CookDiagnosticCode::InvalidMetadata:
            case CookDiagnosticCode::InvalidScene:
            case CookDiagnosticCode::InvalidAsset:
            case CookDiagnosticCode::DependencyCycle:
            case CookDiagnosticCode::InvalidManifest:
            case CookDiagnosticCode::CorruptObject:
                return Core::VFS::VFSError::Corrupted;
            case CookDiagnosticCode::MissingDependency:
                return Core::VFS::VFSError::NotFound;
            case CookDiagnosticCode::BudgetExceeded:
                return Core::VFS::VFSError::SizeLimitExceeded;
            case CookDiagnosticCode::AllocationFailed:
                return Core::VFS::VFSError::OutOfMemory;
            case CookDiagnosticCode::IOFailure:
                return underlying == Core::VFS::VFSError::OK ? Core::VFS::VFSError::IOError : underlying;
        }
        return Core::VFS::VFSError::InvalidPath;
    }
} // namespace ZEngine::Cook
