#include <ZEngine/Cook/CookAdapterSupport.h>
#include <ZEngine/Cook/CookFingerprint.h>
#include <algorithm>

namespace ZEngine::Cook::Detail
{
    namespace VFS = Core::VFS;

    VFS::VFSResult<void> AdapterFailure(const CookAssetInput& input, CookStage stage, CookDiagnostics* diagnostics, CookDiagnosticCode code, VFS::VFSError error, cstring message)
    {
        if (auto* entry = diagnostics ? diagnostics->Emplace(stage, code, error) : nullptr)
        {
            entry->Source = input.Source;
            entry->Asset  = input.Identity;
            (void) entry->SetMessage(message);
        }
        return VFS::VFSResult<void>::Fail(CookDiagnosticError(code, error));
    }

    CookDiagnosticCode AdapterErrorCode(VFS::VFSError error, CookDiagnosticCode corrupted) noexcept
    {
        switch (error)
        {
            case VFS::VFSError::SizeLimitExceeded:
                return CookDiagnosticCode::BudgetExceeded;
            case VFS::VFSError::OutOfMemory:
                return CookDiagnosticCode::AllocationFailed;
            case VFS::VFSError::Unsupported:
                return CookDiagnosticCode::UnsupportedVersion;
            case VFS::VFSError::Corrupted:
                return corrupted;
            case VFS::VFSError::InvalidPath:
                return CookDiagnosticCode::InvalidPath;
            default:
                return CookDiagnosticCode::IOFailure;
        }
    }

    VFS::VFSResult<void> ValidateAdapterInput(ICookAllocator& allocator, const CookAssetInput& input, CookAssetKind kind, CookStage stage, CookDiagnostics* diagnostics, Digest256* key)
    {
        if (!key || !input.Identity.IsValid() || input.Identity.Kind != kind || !input.Artifact || !input.Metadata || !input.Limits || !IsCookLogicalPath(input.Source) || (kind == CookAssetKind::EnvironmentMap ? !input.EnvironmentSource : input.EnvironmentSource != nullptr))
        {
            return AdapterFailure(input, stage, diagnostics, CookDiagnosticCode::InvalidRequest, VFS::VFSError::InvalidPath, "Adapter requires typed captures, strict metadata, limits and a logical source.");
        }
        if (input.Metadata->Data.AssetUUID != input.Identity.UUID || (kind != CookAssetKind::EnvironmentMap && input.Metadata->Data.SettingsCount))
        {
            return AdapterFailure(input, stage, diagnostics, CookDiagnosticCode::InvalidMetadata, VFS::VFSError::Corrupted, "Metadata UUID must agree; this adapter revision accepts no extra settings.");
        }
        const auto computed = ComputeCookMetadataKey(allocator, *input.Metadata, key);
        return computed.Failed() ? AdapterFailure(input, stage, diagnostics, computed.Error() == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : AdapterErrorCode(computed.Error(), CookDiagnosticCode::InvalidMetadata), computed.Error(), "Asset sidecar metadata is invalid or its fingerprint reservation was refused.") : computed;
    }

    Importers::AssetCodec::AssetByteSource CapturedByteSource::View() noexcept
    {
        return {this, [](void* context, Core::Containers::ArrayView<uint8_t> bytes, uint64_t offset) { return static_cast<CapturedByteSource*>(context)->m_file->Read(offset, bytes); }, m_file->Size()};
    }

    VFS::VFSResult<void> CopyCapturedAsset(const ICookCapturedFile& file, ICookByteSink& sink, Core::Containers::ArrayView<uint8_t> stream, uint64_t limit, ICookAllocator& allocator)
    {
        const uint64_t size = file.Size();
        if (!size || !stream.data() || !stream.size())
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        if (size > limit)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        CookMemoryCharge fixed   = {};
        const auto       charged = allocator.TryCharge(Core::Hash::SHA256::FixedWorkingBytes() + sizeof(Digest256) + sizeof(fixed), CookMemoryCategory::Codecs, &fixed);
        if (charged.Failed())
        {
            return charged;
        }
        Core::Hash::SHA256 hash = {};
        for (uint64_t offset = 0; offset < size;)
        {
            const size_t count = static_cast<size_t>(std::min<uint64_t>(stream.size(), size - offset));
            const auto   bytes = stream.first(count);
            const auto   read  = ReadCookCapturedRange(file, offset, bytes);
            if (read.Failed())
            {
                return read;
            }
            const auto hashed = hash.Update(bytes);
            if (hashed.Failed())
            {
                return hashed;
            }
            const auto written = sink.Write(bytes);
            if (written.Failed())
            {
                return written;
            }
            offset += count;
        }
        Digest256  digest    = {};
        const auto finalized = hash.Finalize(&digest);
        if (finalized.Failed())
        {
            return finalized;
        }
        return digest == file.Digest() ? VFS::VFSResult<void>::Ok() : VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
    }
} // namespace ZEngine::Cook::Detail
