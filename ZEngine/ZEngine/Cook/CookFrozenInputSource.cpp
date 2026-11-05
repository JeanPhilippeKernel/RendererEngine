#include <ZEngine/Cook/CookFrozenInputSource.h>
#include <ZEngine/Cook/CookInputPath.h>
#include <ZEngine/Cook/CookServiceMemoryInternal.h>
#include <ZEngine/Cook/CookStorageLayout.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <algorithm>
#include <new>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    size_t Detail::CookNativeServiceMemory::ProviderQueryWorkingBytes() noexcept
    {
        // Native source FixedBytes and frozen-owner FixedWorkingBytes retain
        // their returned values while querying the allocator chain. Include the
        // owner's profile/footprint results and scalar payload/owner totals.
        // The source/provider objects and binding backing are charged separately.
        return CookAllocatorQueryWorkingBytes() + 3 * sizeof(VFS::VFSResult<uint64_t>) + sizeof(VFS::VFSResult<CookAllocatorMemoryRequirements>) + sizeof(VFS::VFSResult<size_t>) + 3 * sizeof(uint64_t) + sizeof(size_t);
    }

    namespace
    {
        Core::Containers::StringView Extension(const VFS::VFSRelativePath& path)
        {
            size_t begin = path.Length();
            while (begin && path.CStr()[begin - 1] != '/' && path.CStr()[begin - 1] != '.')
                --begin;
            return begin && path.CStr()[begin - 1] == '.' ? Core::Containers::StringView(path.CStr() + begin - 1, path.Length() - begin + 1) : Core::Containers::StringView{};
        }

        bool ExtensionEquals(Core::Containers::StringView extension, Core::Containers::StringView expected)
        {
            if (extension.size() != expected.size())
                return false;
            for (size_t i = 0; i < extension.size(); ++i)
            {
                const char value = extension[i];
                if ((value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value) != expected[i])
                    return false;
            }
            return true;
        }

        CookAssetKind AssetKind(const VFS::VFSRelativePath& path)
        {
            const auto extension = Extension(path);
            if (ExtensionEquals(extension, ".zemesh"))
                return CookAssetKind::Mesh;
            if (ExtensionEquals(extension, ".zematerial"))
                return CookAssetKind::Material;
            if (ExtensionEquals(extension, ".hdr") || ExtensionEquals(extension, ".exr"))
                return CookAssetKind::EnvironmentMap;
            // Catalog identity is independent of adapter support. An unsupported
            // imported image/container remains a typed texture; a reachable one
            // must fail its adapter, not disappear or trigger raw reimport.
            constexpr cstring images[] = {".png", ".jpg", ".jpeg", ".ktx", ".ktx2", ".dds", ".bmp", ".tga", ".gif", ".psd", ".pic"};
            for (cstring image : images)
                if (ExtensionEquals(extension, image))
                    return CookAssetKind::Texture;
            return CookAssetKind::Unknown;
        }

        VFS::VFSResult<VFS::VFSRelativePath> SidecarPath(const VFS::VFSRelativePath& path)
        {
            const Core::Containers::StringView suffix                   = {".meta", 5};
            char                               bytes[VFS::VFS_MAX_PATH] = {};
            if (path.Length() + suffix.size() >= sizeof(bytes))
                return VFS::VFSResult<VFS::VFSRelativePath>::Fail(VFS::VFSError::InvalidPath);
            Helpers::secure_memcpy(bytes, sizeof(bytes), path.CStr(), path.Length());
            Helpers::secure_memcpy(bytes + path.Length(), sizeof(bytes) - path.Length(), suffix.data(), suffix.size());
            return VFS::VFSRelativePath::Parse(bytes, path.Length() + suffix.size());
        }

        CookDiagnosticCode FailureCode(VFS::VFSError error)
        {
            switch (error)
            {
                case VFS::VFSError::Corrupted:
                    return CookDiagnosticCode::InputChanged;
                case VFS::VFSError::AlreadyExists:
                    return CookDiagnosticCode::IdentityConflict;
                case VFS::VFSError::InvalidPath:
                    return CookDiagnosticCode::InvalidPath;
                case VFS::VFSError::NotFound:
                    return CookDiagnosticCode::MissingDependency;
                case VFS::VFSError::SizeLimitExceeded:
                    return CookDiagnosticCode::BudgetExceeded;
                case VFS::VFSError::OutOfMemory:
                    return CookDiagnosticCode::AllocationFailed;
                case VFS::VFSError::Unsupported:
                    return CookDiagnosticCode::UnsupportedBackend;
                default:
                    return CookDiagnosticCode::IOFailure;
            }
        }

        VFS::VFSResult<void> ArtifactPath(const CookFrozenLibraryReadLease& read, cstring text, VFS::VFSRelativePath* output)
        {
            const auto parsed = ParseCookInputPath(text);
            if (parsed.Failed())
                return VFS::VFSResult<void>::Fail(parsed.Error());
            if (parsed.Value().Context.IsValid())
                return read.MapContextPath(parsed.Value().Context, output);
            VFS::VFSRelativePath relative = {};
            const auto           root     = read.Resolve(parsed.Value().Logical, &relative);
            if (root.Failed())
                return VFS::VFSResult<void>::Fail(root.Error());
            *output = parsed.Value().Logical;
            return VFS::VFSResult<void>::Ok();
        }
    } // namespace

    CookFrozenInputSource::CookFrozenInputSource(CookFrozenLibraryOwner& owner) noexcept : m_owner(&owner) {}

    CookFrozenInputSource::~CookFrozenInputSource()
    {
        ZENGINE_VALIDATE_ASSERT(!m_bound && !m_store.OutstandingCaptures(), "Frozen input source destroyed before its input lease or captures.");
        (void) ReleaseStorage();
    }

    VFS::VFSResult<void> CookFrozenInputSource::ReleaseStorage() noexcept
    {
        if (m_bound || m_store.OutstandingCaptures())
            return VFS::VFSResult<void>::Fail(VFS::VFSError::AlreadyExists);
        Release();
        if (m_stream.data())
            m_allocator->Free(m_stream.data());
        m_stream         = {};
        m_allocator      = nullptr;
        const auto error = m_store.CleanupError();
        return error == VFS::VFSError::OK ? VFS::VFSResult<void>::Ok() : VFS::VFSResult<void>::Fail(error);
    }

    VFS::VFSResult<void> CookFrozenInputSource::Fail(CookDiagnostics* diagnostics, CookDiagnosticCode code, const VFS::VFSRelativePath& path, VFS::VFSError underlying, cstring message)
    {
        if (auto* diagnostic = diagnostics ? diagnostics->Emplace(CookStage::Capture, code, underlying) : nullptr)
        {
            diagnostic->Source = path;
            (void) diagnostic->SetMessage(message);
        }
        return VFS::VFSResult<void>::Fail(CookDiagnosticError(code, underlying));
    }

    VFS::VFSResult<void> CookFrozenInputSource::Acquire(const CookCaptureContext& context, CookInputLease* output, CookDiagnostics* diagnostics)
    {
        if (!output || !context.Allocator || !context.Limits || !context.Scratch || !context.Scratch->IsValid() || !context.ScratchRun.IsValid())
            return Fail(diagnostics, CookDiagnosticCode::InvalidRequest, {}, VFS::VFSError::InvalidPath, "Frozen input acquisition requires a complete run context.");
        if (m_state != State::Idle || output->IsValid())
            return Fail(diagnostics, CookDiagnosticCode::InvalidRequest, {}, VFS::VFSError::AlreadyExists, "A frozen input source belongs to one run and cannot replace an input lease.");
        const auto limits = ValidateCookLimits(*context.Limits);
        if (limits.Failed())
            return Fail(diagnostics, CookDiagnosticCode::InvalidBudget, {}, limits.Error(), "Cook limits must be valid before input acquisition.");
        CookInputLease pending = {};
        const auto     rooted  = AcquireRoots(&pending, diagnostics);
        if (rooted.Failed())
            return rooted;
        const auto prepared = pending.PrepareCapture(context, diagnostics);
        if (prepared.Failed())
            return prepared;
        *output = std::move(pending);
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookFrozenInputSource::AcquireRoots(CookInputLease* output, CookDiagnostics* diagnostics)
    {
        if (!output)
            return Fail(diagnostics, CookDiagnosticCode::InvalidRequest, {}, VFS::VFSError::InvalidPath, "Root acquisition requires empty caller-owned lease storage.");
        if (m_state != State::Idle || output->IsValid())
            return Fail(diagnostics, CookDiagnosticCode::InvalidRequest, {}, VFS::VFSError::AlreadyExists, "A frozen input source belongs to one run and cannot replace an input lease.");
        const auto acquired = m_owner->TryAcquireRead(&m_read);
        if (acquired.Failed())
            return Fail(diagnostics, acquired.Error() == VFS::VFSError::AlreadyExists ? CookDiagnosticCode::InputBusy : FailureCode(acquired.Error()), {}, acquired.Error(), "Frozen library read exclusion could not be acquired.");
        const auto bound = BindLease(*this, output, CookInputState::Rooted);
        if (bound.Failed())
        {
            m_read.Reset();
            return bound;
        }
        m_bound = true;
        m_state = State::Rooted;
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookFrozenInputSource::RequireDisjoint(const VFS::VFSRootLease& root, CookDiagnostics* diagnostics)
    {
        const auto disjoint = m_read.RequireDisjoint(root);
        return disjoint.Succeeded() ? disjoint : Fail(diagnostics, disjoint.Error() == VFS::VFSError::PermissionDenied ? CookDiagnosticCode::InvalidRequest : FailureCode(disjoint.Error()), {}, disjoint.Error(), "Input roots must be physically disjoint from output and scratch roots.");
    }

    VFS::VFSResult<void> CookFrozenInputSource::PrepareCapture(const CookCaptureContext& context, CookDiagnostics* diagnostics)
    {
        if (m_state != State::Rooted || !m_bound || !context.Allocator || !context.Limits || !context.Scratch || !context.Scratch->IsValid() || !context.ScratchRun.IsValid())
            return Fail(diagnostics, CookDiagnosticCode::InvalidRequest, {}, VFS::VFSError::InvalidPath, "Capture preparation requires rooted input exclusion and a complete run context.");
        const auto limits = ValidateCookLimits(*context.Limits);
        if (limits.Failed())
            return Fail(diagnostics, CookDiagnosticCode::InvalidBudget, {}, limits.Error(), "Cook limits must be valid before capture preparation.");
        m_state            = State::Acquiring;
        m_allocator        = context.Allocator;
        m_limits           = *context.Limits;
        const auto perform = [&]() -> VFS::VFSResult<void> {
            const auto disjoint = m_read.RequireDisjoint(*context.Scratch);
            if (disjoint.Failed())
                return Fail(diagnostics, disjoint.Error() == VFS::VFSError::PermissionDenied ? CookDiagnosticCode::InvalidRequest : FailureCode(disjoint.Error()), {}, disjoint.Error(), "Input and scratch roots must be physically disjoint.");
            if (context.WorkingMemory)
            {
                const auto                count  = m_read.RootCount();
                Detail::CookStorageLayout layout = {};
                size_t                    offset = 0;
                if (!count || !layout.Add<VFS::VFSWorkingMemoryReservation>(count, &offset))
                    return Fail(diagnostics, CookDiagnosticCode::BudgetExceeded, {}, VFS::VFSError::SizeLimitExceeded, "Input root memory reservation size exceeds the working budget.");
                const auto allocated = m_allocator->Allocate(layout.Size(), alignof(VFS::VFSWorkingMemoryReservation), CookMemoryCategory::Captures);
                if (allocated.Failed() || !allocated.Value())
                {
                    const auto error = allocated.Failed() ? allocated.Error() : VFS::VFSError::OutOfMemory;
                    return Fail(diagnostics, FailureCode(error), {}, error, "Could not retain input-root working-memory reservations.");
                }
                m_root_memory = static_cast<VFS::VFSWorkingMemoryReservation*>(allocated.Value());
                for (uint32_t i = 0; i < count; ++i)
                    new (&m_root_memory[i]) VFS::VFSWorkingMemoryReservation{};
                m_root_memory_count = count;
                for (uint32_t i = 0; i < count; ++i)
                {
                    const auto reserved = m_read.ReserveRootWorkingMemory(i, *context.WorkingMemory, &m_root_memory[i]);
                    if (reserved.Failed())
                        return Fail(diagnostics, FailureCode(reserved.Error()), {}, reserved.Error(), "Frozen input root storage cannot fit the checked VFS budget.");
                }
            }
            const uint64_t available = CookAllocationWindow(*m_allocator, m_limits.MaxWorkingBytes);
            if (available < ZKilo(4))
                return Fail(diagnostics, CookDiagnosticCode::BudgetExceeded, {}, VFS::VFSError::SizeLimitExceeded, "The remaining budget cannot hold a bounded directory stream.");
            const size_t bytes  = static_cast<size_t>(std::clamp<uint64_t>(available / 4, ZKilo(4), ZKilo(64)));
            const auto   stream = m_allocator->Allocate(bytes, DEFAULT_ALIGNMENT, CookMemoryCategory::Captures);
            if (stream.Failed() || !stream.Value())
            {
                const auto error = stream.Failed() ? stream.Error() : VFS::VFSError::OutOfMemory;
                return Fail(diagnostics, FailureCode(error), {}, error, "Could not allocate the shared bounded input stream.");
            }
            m_stream           = {static_cast<uint8_t*>(stream.Value()), bytes};
            const auto storage = m_store.Initialize(context, m_stream);
            if (storage.Failed())
                return Fail(diagnostics, FailureCode(storage.Error()), context.ScratchRun, storage.Error(), "Could not initialize run-owned capture storage.");
            const auto maximum    = static_cast<size_t>(m_limits.MaxWorkingBytes / sizeof(CookFrozenFileRecord));
            const auto discovered = m_files.Build(*m_allocator, m_read, maximum, m_stream);
            if (discovered.Failed())
                return Fail(diagnostics, FailureCode(discovered.Error()), {}, discovered.Error(), "Could not discover the bounded frozen input identity set.");
            return BuildCatalog(diagnostics);
        };
        const auto prepared = perform();
        if (prepared.Failed())
        {
            // The failed lease still owns this session. Its Reset releases
            // indexes/exclusion; do not make a live lease dangle by unbinding.
            m_state = State::Terminal;
            return prepared;
        }
        m_state = State::Active;
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookFrozenInputSource::BuildCatalog(CookDiagnostics* diagnostics)
    {
        uint32_t count           = 0;
        uint64_t maximum_sidecar = 0;
        for (const auto& file : m_files.Records())
        {
            if (file.Stat.IsDirectory || AssetKind(file.Path) == CookAssetKind::Unknown)
                continue;
            if (count == m_limits.MaxCatalogEntries)
                return Fail(diagnostics, CookDiagnosticCode::BudgetExceeded, file.Path, VFS::VFSError::SizeLimitExceeded, "Imported catalog entry limit exceeded before retaining metadata.");
            ++count;
            const auto sidecar = SidecarPath(file.Path);
            if (sidecar.Failed())
                return Fail(diagnostics, CookDiagnosticCode::InvalidPath, file.Path, sidecar.Error(), "Sidecar path cannot be represented without truncation.");
            const auto* metadata = m_files.Find(sidecar.Value());
            if (!metadata || metadata->Stat.IsDirectory)
                return Fail(diagnostics, CookDiagnosticCode::InvalidMetadata, sidecar.Value(), VFS::VFSError::NotFound, "Imported asset has no regular read-only sidecar; cooking never creates one.");
            if (metadata->Stat.SizeBytes > m_limits.MaxSidecarBytes)
                return Fail(diagnostics, CookDiagnosticCode::BudgetExceeded, sidecar.Value(), VFS::VFSError::SizeLimitExceeded, "Sidecar exceeds the configured byte limit.");
            maximum_sidecar = std::max(maximum_sidecar, metadata->Stat.SizeBytes);
        }
        const auto initialized = m_catalog.Initialize(*m_allocator, count, m_limits.MaxCatalogEntries);
        if (initialized.Failed())
            return Fail(diagnostics, FailureCode(initialized.Error()), {}, initialized.Error(), "Could not allocate the actual imported catalog capacity.");
        CookWorkspace metadata_workspace = {};
        auto          metadata_bytes     = m_stream;
        if (maximum_sidecar > metadata_bytes.size())
        {
            const auto allocated = metadata_workspace.Initialize(*m_allocator, static_cast<size_t>(maximum_sidecar), CookMemoryCategory::CatalogGraph);
            if (allocated.Failed())
                return Fail(diagnostics, FailureCode(allocated.Error()), {}, allocated.Error(), "Could not allocate the largest observed sidecar workspace.");
            metadata_bytes = {metadata_workspace.Arena()->m_memory, static_cast<size_t>(maximum_sidecar)};
        }
        for (const auto& file : m_files.Records())
        {
            if (file.Stat.IsDirectory)
                continue;
            const auto kind = AssetKind(file.Path);
            if (kind == CookAssetKind::Unknown)
                continue;
            const auto       metadata        = SidecarPath(file.Path);
            CookCapturedFile captured        = {};
            const auto       captured_result = CaptureFile(metadata.Value(), m_limits.MaxSidecarBytes, &captured, diagnostics);
            if (captured_result.Failed())
                return captured_result;
            const auto bytes  = Core::Containers::ArrayView<uint8_t>{metadata_bytes.data(), static_cast<size_t>(captured.Size())};
            const auto loaded = ReadCookCapturedRange(captured, 0, bytes);
            if (loaded.Failed())
                return Fail(diagnostics, FailureCode(loaded.Error()), metadata.Value(), loaded.Error(), "Could not read the owned sidecar capture.");
            VFS::StrictMetaFileData parsed        = {};
            const auto              parsed_result = VFS::MetaFileIO::ParseStrict({bytes.data(), bytes.size()}, &parsed);
            if (parsed_result.Failed())
                return Fail(diagnostics, parsed_result.Error() == VFS::VFSError::SizeLimitExceeded ? CookDiagnosticCode::BudgetExceeded : CookDiagnosticCode::InvalidMetadata, metadata.Value(), parsed_result.Error(), "Imported sidecar is not valid strict metadata.");
            CookAssetCatalogEntry entry = {
                .Identity = {.Kind = kind, .UUID = parsed.Data.AssetUUID},
                  .Artifact = file.Path, .Metadata = metadata.Value()
            };
            if (kind == CookAssetKind::EnvironmentMap)
            {
                if (!parsed.Has(VFS::MetaFileField::ArtifactPath) || !parsed.Has(VFS::MetaFileField::SourceHash))
                    return Fail(diagnostics, CookDiagnosticCode::InvalidMetadata, metadata.Value(), VFS::VFSError::Corrupted, "Environment metadata requires a declared artifact path and source hash.");
                const auto resolved = ArtifactPath(m_read, parsed.Data.ArtifactPath, &entry.Artifact);
                if (resolved.Failed())
                    return Fail(diagnostics, CookDiagnosticCode::InvalidMetadata, metadata.Value(), resolved.Error(), "Environment artifact path must resolve exactly within declared input roots.");
                const auto* artifact = m_files.Find(entry.Artifact);
                if (!artifact || artifact->Stat.IsDirectory || !ExtensionEquals(Extension(entry.Artifact), ".zenvmap"))
                    return Fail(diagnostics, CookDiagnosticCode::InvalidMetadata, metadata.Value(), VFS::VFSError::NotFound, "Environment artifact is absent or not a declared .zenvmap file.");
                entry.EnvironmentSource = file.Path;
            }
            const auto appended = m_catalog.Append(entry, diagnostics);
            if (appended.Failed())
                return appended;
            const auto cleanup = captured.Reset();
            if (cleanup.Failed())
                return Fail(diagnostics, CookDiagnosticCode::IOFailure, metadata.Value(), cleanup.Error(), "Could not clean up a temporary catalog capture before sealing.");
        }
        return m_catalog.Finalize(diagnostics);
    }

    VFS::VFSResult<void> CookFrozenInputSource::CaptureFile(const VFS::VFSRelativePath& path, uint64_t maximum_bytes, CookCapturedFile* output, CookDiagnostics* diagnostics)
    {
        const auto* record = m_files.Find(path);
        if (record && !record->Captured && record->Stat.SizeBytes > m_limits.MaxCapturedBytes - m_unique_captured_bytes)
            return Fail(diagnostics, CookDiagnosticCode::BudgetExceeded, path, VFS::VFSError::SizeLimitExceeded, "Aggregate unique captured input limit exceeded.");
        const bool first_capture = record && !record->Captured;
        const auto captured      = m_files.Capture(m_read, m_store, path, maximum_bytes, output);
        if (captured.Failed())
            return Fail(diagnostics, FailureCode(captured.Error()), path, captured.Error(), "Could not capture the frozen input generation.");
        if (first_capture)
            m_unique_captured_bytes += record->Stat.SizeBytes;
        return captured;
    }

    VFS::VFSResult<void> CookFrozenInputSource::MapContextPath(const VFS::VFSPath& path, VFS::VFSRelativePath* output, CookDiagnostics* diagnostics)
    {
        const auto mapped = m_read.MapContextPath(path, output);
        return mapped.Succeeded() ? mapped : Fail(diagnostics, FailureCode(mapped.Error()), {}, mapped.Error(), "Context source path is outside the declared roots or maps ambiguously.");
    }

    VFS::VFSResult<void> CookFrozenInputSource::FindAsset(const CookAssetIdentity& identity, CookAssetCatalogEntry* output, CookDiagnostics* diagnostics)
    {
        const auto* record = m_catalog.Find(identity);
        if (!record)
            return Fail(diagnostics, CookDiagnosticCode::MissingDependency, {}, VFS::VFSError::NotFound, "Typed asset UUID is absent from the frozen catalog.");
        *output = *record;
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookFrozenInputSource::EnumerateCatalog(CookCatalogVisitorFn visitor, void* context, CookDiagnostics*)
    {
        return m_catalog.Enumerate(visitor, context);
    }

    VFS::VFSResult<void> CookFrozenInputSource::Capture(const VFS::VFSRelativePath& path, CookCapturedFile* output, CookDiagnostics* diagnostics)
    {
        if (m_error != VFS::VFSError::OK)
            return Fail(diagnostics, FailureCode(m_error), m_error_source, m_error, "This input generation already failed; individual files cannot be retried.");
        const auto     extension = Extension(path);
        const uint64_t maximum   = ExtensionEquals(extension, ".meta") ? m_limits.MaxSidecarBytes : ExtensionEquals(extension, ".zematerial") ? m_limits.MaxMaterialBytes : ExtensionEquals(extension, ".yaml") || ExtensionEquals(extension, ".yml") || ExtensionEquals(extension, ".scene") || ExtensionEquals(extension, ".zescene") ? m_limits.SceneLimits.MaxFileBytes : m_limits.MaxEncodedAssetBytes;
        const auto     captured  = CaptureFile(path, maximum, output, diagnostics);
        if (captured.Failed())
        {
            m_error        = captured.Error();
            m_error_source = path;
        }
        return captured;
    }

    VFS::VFSResult<void> CookFrozenInputSource::Seal(CookDiagnostics* diagnostics)
    {
        const auto verified = m_error == VFS::VFSError::OK ? m_files.Verify(m_read, m_store, m_stream, &m_error_source) : VFS::VFSResult<void>::Fail(m_error);
        m_state             = State::Terminal;
        m_read.Reset();
        if (verified.Failed())
            return Fail(diagnostics, FailureCode(verified.Error()), m_error_source, verified.Error(), "Frozen input identities, catalog membership, or captured source digests changed before sealing.");
        return verified;
    }

    void CookFrozenInputSource::Release() noexcept
    {
        m_read.Reset();
        m_catalog.Reset();
        m_files.Reset();
        for (uint32_t i = 0; i < m_root_memory_count; ++i)
            m_root_memory[i].~VFSWorkingMemoryReservation();
        if (m_root_memory)
            m_allocator->Free(m_root_memory);
        m_root_memory       = nullptr;
        m_root_memory_count = 0;
        m_bound             = false;
        m_state             = State::Released;
    }
} // namespace ZEngine::Cook
