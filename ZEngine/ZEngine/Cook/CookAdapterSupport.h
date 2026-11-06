#pragma once
#include <ZEngine/Cook/CookAdapter.h>
#include <ZEngine/Importers/AssetByteSource.h>

namespace ZEngine::Cook::Detail
{
    [[nodiscard]] Core::VFS::VFSResult<void> AdapterFailure(const CookAssetInput&, CookStage, CookDiagnostics*, CookDiagnosticCode, Core::VFS::VFSError, cstring message);
    [[nodiscard]] CookDiagnosticCode         AdapterErrorCode(Core::VFS::VFSError, CookDiagnosticCode corrupted = CookDiagnosticCode::InvalidAsset) noexcept;
    [[nodiscard]] Core::VFS::VFSResult<void> ValidateAdapterInput(ICookAllocator&, const CookAssetInput&, CookAssetKind expected_kind, CookStage, CookDiagnostics*, Digest256* metadata_key);

    // Borrowed bridge, not an allocation or file owner. Do not move/copy this
    // object while a View is in use. The capture outlives both bridge and view.
    class CapturedByteSource
    {
    public:
        explicit CapturedByteSource(const ICookCapturedFile& file) noexcept : m_file(&file) {}
        CapturedByteSource(const CapturedByteSource&)                                             = delete;
        CapturedByteSource&                                  operator=(const CapturedByteSource&) = delete;
        [[nodiscard]] Importers::AssetCodec::AssetByteSource View() noexcept;

    private:
        const ICookCapturedFile* m_file = nullptr;
    };

    // Reuses caller-owned charged stream storage. Separately charges principal
    // fixed hash state through allocator before reading/writing. Exact reads,
    // complete writes and captured digest verification; never allocate or publish. A late read/
    // digest/write failure may leave partial bytes only in the caller's staging
    // sink, which must be discarded before manifest commit.
    [[nodiscard]] Core::VFS::VFSResult<void> CopyCapturedAsset(const ICookCapturedFile&, ICookByteSink&, Core::Containers::ArrayView<uint8_t> stream, uint64_t max_encoded_bytes, ICookAllocator& allocator);
} // namespace ZEngine::Cook::Detail
