#include <ZEngine/Cook/CookManifestInternal.h>
#include <ZEngine/Cook/CookPublication.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <algorithm>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        VFS::VFSRelativePath PublicationPath(cstring path)
        {
            return VFS::VFSRelativePath::Parse(path).Value();
        }

        CookDiagnosticCode PublicationError(VFS::VFSError error, CookDiagnosticCode corrupt = CookDiagnosticCode::CorruptObject)
        {
            if (error == VFS::VFSError::Corrupted || error == VFS::VFSError::NotAFile)
            {
                return corrupt;
            }
            if (error == VFS::VFSError::SizeLimitExceeded)
            {
                return CookDiagnosticCode::BudgetExceeded;
            }
            if (error == VFS::VFSError::OutOfMemory)
            {
                return CookDiagnosticCode::AllocationFailed;
            }
            if (error == VFS::VFSError::Unsupported)
            {
                return CookDiagnosticCode::UnsupportedVersion;
            }
            return CookDiagnosticCode::IOFailure;
        }

        struct StagingSink final : ICookByteSink
        {
            VFS::VFSRootAtomicFileWriter*        Writer      = nullptr;
            Core::Containers::ArrayView<uint8_t> Buffer      = {};
            uint64_t*                            Staged      = nullptr;
            uint64_t                             TotalLimit  = 0;
            uint64_t                             ObjectLimit = 0;
            uint64_t                             Reserved    = 0;
            size_t                               Used        = 0;
            Core::Hash::SHA256*                  Hash        = nullptr;
            VFS::VFSError                        Error       = VFS::VFSError::OK;

            VFS::VFSResult<void>                 Flush()
            {
                if (Error != VFS::VFSError::OK)
                {
                    return VFS::VFSResult<void>::Fail(Error);
                }
                if (Used)
                {
                    const auto written = Writer->Write(Buffer.first(Used));
                    if (written.Failed())
                    {
                        return VFS::VFSResult<void>::Fail(Error = written.Error());
                    }
                    Used = 0;
                }
                return VFS::VFSResult<void>::Ok();
            }

            VFS::VFSResult<void> Write(Core::Containers::ArrayView<const uint8_t> bytes) override
            {
                if (Error != VFS::VFSError::OK)
                {
                    return VFS::VFSResult<void>::Fail(Error);
                }
                if (bytes.size() && !bytes.data())
                {
                    return VFS::VFSResult<void>::Fail(Error = VFS::VFSError::InvalidPath);
                }
                if (bytes.size() > ObjectLimit - Reserved || bytes.size() > TotalLimit - *Staged)
                {
                    return VFS::VFSResult<void>::Fail(Error = VFS::VFSError::SizeLimitExceeded);
                }
                Reserved += bytes.size();
                *Staged  += bytes.size();
                if (Hash)
                {
                    const auto hashed = Hash->Update(bytes);
                    if (hashed.Failed())
                    {
                        return VFS::VFSResult<void>::Fail(Error = hashed.Error());
                    }
                }
                size_t offset = 0;
                while (offset < bytes.size())
                {
                    if (!Used && bytes.size() - offset >= Buffer.size())
                    {
                        const auto written = Writer->Write(bytes.subspan(offset));
                        if (written.Failed())
                        {
                            return VFS::VFSResult<void>::Fail(Error = written.Error());
                        }
                        return VFS::VFSResult<void>::Ok();
                    }
                    const size_t count = std::min(bytes.size() - offset, Buffer.size() - Used);
                    (void) Helpers::secure_memcpy(Buffer.data() + Used, Buffer.size() - Used, bytes.data() + offset, count);
                    Used   += count;
                    offset += count;
                    if (Used == Buffer.size() && Flush().Failed())
                    {
                        return VFS::VFSResult<void>::Fail(Error);
                    }
                }
                return VFS::VFSResult<void>::Ok();
            }
        };

        struct ManifestComparison final : ICookByteSink
        {
            VFS::IVFSFile*                       File      = nullptr;
            Core::Containers::ArrayView<uint8_t> Buffer    = {};
            uint64_t                             Size      = 0;
            uint64_t                             Position  = 0;
            size_t                               Cursor    = 0;
            size_t                               Available = 0;
            bool                                 Equal     = true;

            VFS::VFSResult<void>                 Write(Core::Containers::ArrayView<const uint8_t> bytes) override
            {
                size_t offset = 0;
                while (Equal && offset < bytes.size())
                {
                    if (Cursor == Available)
                    {
                        if (Position == Size)
                        {
                            Equal = false;
                            break;
                        }
                        const auto read = File->Read(Buffer.first(static_cast<size_t>(std::min<uint64_t>(Buffer.size(), Size - Position))), Position);
                        if (read.Failed())
                        {
                            return VFS::VFSResult<void>::Fail(read.Error());
                        }
                        if (!read.Value() || read.Value() > std::min<uint64_t>(Buffer.size(), Size - Position))
                        {
                            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                        }
                        Cursor    = 0;
                        Available = read.Value();
                    }
                    const size_t count  = std::min(bytes.size() - offset, Available - Cursor);
                    Equal               = Helpers::secure_memcmp(bytes.data() + offset, bytes.size() - offset, Buffer.data() + Cursor, Available - Cursor, count) == 0;
                    Position           += count;
                    Cursor             += count;
                    offset             += count;
                }
                Position += bytes.size() - offset;
                return VFS::VFSResult<void>::Ok();
            }
        };
    } // namespace

    CookPublication::~CookPublication()
    {
        (void) Reset();
    }

    VFS::VFSResult<void> CookPublication::Fail(CookStage stage, CookDiagnosticCode code, VFS::VFSError error, cstring message, CookDiagnostics* diagnostics, const VFS::VFSRelativePath* source)
    {
        if (m_locked && !m_finished && m_error == VFS::VFSError::OK)
        {
            m_error = error;
        }
        if (auto* entry = diagnostics ? diagnostics->Emplace(stage, code, error) : nullptr)
        {
            if (source)
            {
                entry->Source = *source;
            }
            (void) entry->SetMessage(message);
        }
        return VFS::VFSResult<void>::Fail(error);
    }

    void CookPublication::Warning(VFS::VFSError error, const VFS::VFSRelativePath& source, cstring message, CookDiagnostics* diagnostics) const noexcept
    {
        if (auto* entry = diagnostics ? diagnostics->Emplace(CookStage::Cleanup, CookDiagnosticCode::CleanupWarning, error) : nullptr)
        {
            entry->Source = source;
            (void) entry->SetMessage(message);
        }
    }

    VFS::VFSResult<void> CookPublication::Reset(CookDiagnostics* diagnostics) noexcept
    {
        m_reuse.Reset();
        m_previous.Reset();
        VFS::VFSError error = VFS::VFSError::OK;
        if (m_locked)
        {
            const auto lock = PublicationPath(CookWriterLockName);
            error           = m_input.Root->Remove(lock).Error();
            if (error != VFS::VFSError::OK)
            {
                Warning(error, lock, "Could not remove this run's writer directory; explicit operator recovery may be required.", diagnostics);
            }
        }
        m_allocator    = nullptr;
        m_input        = {};
        m_stream       = {};
        m_staged_bytes = 0;
        m_error        = VFS::VFSError::OK;
        m_locked = m_finished = m_objects = false;
        return error == VFS::VFSError::OK ? VFS::VFSResult<void>::Ok() : VFS::VFSResult<void>::Fail(error);
    }

    void CookPublication::CleanupStage(VFS::VFSRootAtomicFileWriter& writer, uint64_t reserved, CookDiagnostics* diagnostics) noexcept
    {
        const auto path  = writer.TemporaryPath();
        const bool owned = writer.OwnsTemporary();
        const auto reset = writer.Reset();
        if (owned && reset.Succeeded())
        {
            m_staged_bytes -= reserved;
        }
        if (reset.Failed())
        {
            Warning(reset.Error(), path, "Owned temporary cleanup failed; its conservatively charged bytes remain part of this run, and no unowned file is removed.", diagnostics);
        }
    }

    VFS::VFSResult<void> CookPublication::Initialize(ICookAllocator& allocator, const CookPublicationInput& input, Core::Containers::ArrayView<uint8_t> stream, CookDiagnostics* diagnostics)
    {
        if (m_locked)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        if (!input.Root || !input.Root->IsValid() || !input.Inputs || input.Inputs->State() != CookInputState::Sealed || !input.Graph || !input.Graph->IsFinalized() || !input.Graph->SceneCount() || !input.Limits || ValidateCookLimits(*input.Limits).Failed() || !stream.data() || !stream.size())
        {
            return Fail(CookStage::Setup, CookDiagnosticCode::InvalidRequest, VFS::VFSError::InvalidPath, "Publication requires sealed inputs, a finalized nonempty scene graph, pinned output, valid limits and charged stream storage.", diagnostics);
        }
        if (!VFS::HasGuarantees(input.Root->QueryPublicationCapabilities().Guarantees, VFS::VFS_PUBLICATION_GUARANTEES))
        {
            return Fail(CookStage::Setup, CookDiagnosticCode::UnsupportedBackend, VFS::VFSError::Unsupported, "Output root does not provide all required publication guarantees.", diagnostics);
        }
        const auto lock     = PublicationPath(CookWriterLockName);
        const auto acquired = input.Root->CreateDirExclusive(lock);
        if (acquired.Failed())
        {
            return Fail(CookStage::Setup, acquired.Error() == VFS::VFSError::AlreadyExists ? CookDiagnosticCode::OutputBusy : CookDiagnosticCode::IOFailure, acquired.Error(), "Cannot acquire the output writer directory; an existing lock is never stolen or removed.", diagnostics, &lock);
        }
        m_allocator    = &allocator;
        m_input        = input;
        m_input.Inputs = nullptr; // Sealing is proven; exclusion may now be released.
        m_stream       = stream.first(std::min<size_t>(stream.size(), ZKilo(64)));
        m_locked       = true;
        auto status    = ReadPrevious(allocator, diagnostics);
        if (status.Succeeded())
        {
            status = m_reuse.Initialize(allocator, {input.Graph, input.Recipe, &m_previous, CookObjectReader(*input.Root), input.Limits, input.ForceRebuild}, m_stream);
        }
        if (status.Failed())
        {
            if (m_error == VFS::VFSError::OK)
            {
                (void) Fail(CookStage::Setup, PublicationError(status.Error()), status.Error(), "Cannot initialize bounded publication/reuse state.", diagnostics);
            }
            (void) Reset(diagnostics);
        }
        return status;
    }

    VFS::VFSResult<void> CookPublication::ReadPrevious(ICookAllocator& allocator, CookDiagnostics* diagnostics)
    {
        CookMemoryCharge fixed   = {};
        const auto       charged = allocator.TryCharge(sizeof(CookWorkspace) + sizeof(VFS::VFSRelativePath) + sizeof(fixed), CookMemoryCategory::StagingManifest, &fixed);
        if (charged.Failed())
        {
            return Fail(CookStage::Setup, Detail::ManifestResourceCode(charged.Error()), charged.Error(), "Principal previous-manifest read state exceeds the working budget.", diagnostics);
        }
        const auto path   = PublicationPath(CookManifestName);
        const auto opened = m_input.Root->Open(path, VFS::VFSOpenFlags::Read | VFS::VFSOpenFlags::AllowReplace);
        if (opened.Failed())
        {
            return opened.Error() == VFS::VFSError::NotFound ? VFS::VFSResult<void>::Ok() : Fail(CookStage::Setup, PublicationError(opened.Error(), CookDiagnosticCode::InvalidManifest), opened.Error(), "Previous manifest is unreadable; publication fails closed.", diagnostics, &path);
        }
        if (!opened.Value())
        {
            return Fail(CookStage::Setup, CookDiagnosticCode::IOFailure, VFS::VFSError::IOError, "Previous manifest open returned no file handle.", diagnostics, &path);
        }
        const auto read = [&]() -> VFS::VFSResult<void> {
            const auto stat = opened.Value()->Stat();
            if (stat.Failed())
            {
                return VFS::VFSResult<void>::Fail(stat.Error());
            }
            if (stat.Value().IsDirectory)
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            }
            const uint64_t size = stat.Value().SizeBytes;
            if (size > m_input.Limits->MaxManifestBytes)
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
            }
            CookWorkspace storage = {};
            uint8_t*      bytes   = m_stream.data();
            if (size)
            {
                const auto allocated = storage.Initialize(allocator, static_cast<size_t>(size), CookMemoryCategory::StagingManifest);
                if (allocated.Failed())
                {
                    return allocated;
                }
                bytes = static_cast<uint8_t*>(storage.Arena()->AllocateNoZero(static_cast<size_t>(size), 1));
                if (!bytes)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                }
            }
            const auto contents = opened.Value()->ReadAll({bytes, static_cast<size_t>(size)});
            if (contents.Failed() || contents.Value() != size)
            {
                return VFS::VFSResult<void>::Fail(contents.Failed() ? contents.Error() : VFS::VFSError::Corrupted);
            }
            const auto eof = opened.Value()->Read(m_stream.first(1), size);
            if (eof.Failed() || eof.Value())
            {
                return VFS::VFSResult<void>::Fail(eof.Failed() ? eof.Error() : VFS::VFSError::Corrupted);
            }
            const auto after = opened.Value()->Stat();
            if (after.Failed() || after.Value().IsDirectory || after.Value().SizeBytes != size)
            {
                return VFS::VFSResult<void>::Fail(after.Failed() ? after.Error() : VFS::VFSError::Corrupted);
            }
            const auto parsed = m_previous.Read({bytes, static_cast<size_t>(size)}, m_input.Recipe.Target, *m_input.Limits, allocator, diagnostics);
            return parsed.Failed() ? VFS::VFSResult<void>::Fail(parsed.Error()) : VFS::VFSResult<void>::Ok();
        }();
        const auto close = m_input.Root->CloseChecked(opened.Value());
        const auto error = read.Failed() ? read.Error() : close.Error();
        return error == VFS::VFSError::OK ? VFS::VFSResult<void>::Ok() : Fail(CookStage::Setup, PublicationError(error, CookDiagnosticCode::InvalidManifest), error, "Previous manifest read/parse/checked-close failed; its package is preserved.", diagnostics, &path);
    }

    VFS::VFSResult<CookReuseDecision> CookPublication::PrepareAsset(uint32_t index, CookDiagnostics* diagnostics)
    {
        if (!m_locked || m_finished || m_error != VFS::VFSError::OK)
        {
            return VFS::VFSResult<CookReuseDecision>::Fail(m_error == VFS::VFSError::OK ? VFS::VFSError::InvalidPath : m_error);
        }
        const auto prepared = m_reuse.PrepareAsset(index, diagnostics);
        if (prepared.Failed())
        {
            m_error = prepared.Error();
        }
        return prepared;
    }

    VFS::VFSResult<CookReuseDecision> CookPublication::PrepareScene(uint32_t index, const Digest256& digest, CookDiagnostics* diagnostics)
    {
        if (!m_locked || m_finished || m_error != VFS::VFSError::OK)
        {
            return VFS::VFSResult<CookReuseDecision>::Fail(m_error == VFS::VFSError::OK ? VFS::VFSError::InvalidPath : m_error);
        }
        const auto prepared = m_reuse.PrepareScene(index, digest, diagnostics);
        if (prepared.Failed())
        {
            m_error = prepared.Error();
        }
        return prepared;
    }

    VFS::VFSResult<void> CookPublication::EncodeAsset(uint32_t index, CookProduceBytesFn produce, void* context, CookDiagnostics* diagnostics)
    {
        const auto prepared = PrepareAsset(index, diagnostics);
        return prepared.Failed() ? VFS::VFSResult<void>::Fail(prepared.Error()) : prepared.Value() == CookReuseDecision::Ready ? VFS::VFSResult<void>::Ok() : Encode(index, false, produce, context, diagnostics);
    }

    VFS::VFSResult<void> CookPublication::EncodeScene(uint32_t index, const Digest256& digest, CookProduceBytesFn produce, void* context, CookDiagnostics* diagnostics)
    {
        const auto prepared = PrepareScene(index, digest, diagnostics);
        return prepared.Failed() ? VFS::VFSResult<void>::Fail(prepared.Error()) : prepared.Value() == CookReuseDecision::Ready ? VFS::VFSResult<void>::Ok() : Encode(index, true, produce, context, diagnostics);
    }

    VFS::VFSResult<void> CookPublication::Encode(uint32_t index, bool scene, CookProduceBytesFn produce, void* context, CookDiagnostics* diagnostics)
    {
        if (!produce)
        {
            return Fail(CookStage::Encode, CookDiagnosticCode::InvalidRequest, VFS::VFSError::InvalidPath, "Encoding requires a synchronous byte producer.", diagnostics);
        }
        CookMemoryCharge fixed   = {};
        const auto       charged = m_allocator->TryCharge(sizeof(VFS::VFSRootAtomicFileWriter) + sizeof(StagingSink) + Core::Hash::SHA256::FixedWorkingBytes() + sizeof(CookObjectDescriptor) + sizeof(Digest256) + 2 * sizeof(VFS::VFSRelativePath) + sizeof(fixed), CookMemoryCategory::StagingManifest, &fixed);
        if (charged.Failed())
        {
            return Fail(CookStage::Encode, Detail::ManifestResourceCode(charged.Error()), charged.Error(), "Principal object staging state exceeds the working budget.", diagnostics);
        }
        if (!m_objects)
        {
            const auto directory = PublicationPath("objects");
            const auto created   = m_input.Root->CreateDir(directory);
            if (created.Failed())
            {
                return Fail(CookStage::Encode, CookDiagnosticCode::IOFailure, created.Error(), "Cannot create the immutable object directory.", diagnostics, &directory);
            }
            m_objects = true;
        }
        const auto*                  node      = scene ? nullptr : m_input.Graph->Node(index);
        const auto*                  format    = node ? FindCookAssetFormat(node->Format) : nullptr;
        const cstring                extension = scene ? CookSceneObjectExtension : format->Extension;
        const uint64_t               limit     = scene ? m_input.Limits->SceneLimits.MaxFileBytes : node->Identity.Kind == CookAssetKind::Material ? std::min(m_input.Limits->MaxMaterialBytes, m_input.Limits->MaxEncodedAssetBytes) : m_input.Limits->MaxEncodedAssetBytes;
        VFS::VFSRootAtomicFileWriter writer    = {};
        const auto                   opened    = writer.Open(*m_input.Root, PublicationPath("objects/pending"));
        if (opened.Failed())
        {
            return Fail(CookStage::Encode, PublicationError(opened.Error()), opened.Error(), "Cannot exclusively create object staging.", diagnostics);
        }
        Core::Hash::SHA256 hash = {};
        StagingSink        sink = {};
        sink.Writer             = &writer;
        sink.Hash               = &hash;
        sink.Buffer             = m_stream;
        sink.Staged             = &m_staged_bytes;
        sink.TotalLimit         = m_input.Limits->MaxStagedBytes;
        sink.ObjectLimit        = limit;
        const auto produced     = produce(context, sink);
        auto       status       = sink.Error != VFS::VFSError::OK ? VFS::VFSResult<void>::Fail(sink.Error) : produced;
        if (status.Succeeded() && !sink.Reserved)
        {
            status = VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
        if (status.Succeeded())
        {
            status = sink.Flush();
        }
        if (status.Succeeded())
        {
            status = writer.Seal();
        }
        CookObjectDescriptor object = {};
        Digest256            digest = {};
        if (status.Succeeded())
        {
            status = hash.Finalize(&digest);
        }
        if (status.Succeeded())
        {
            status = MakeCookObjectDescriptor(digest, writer.BytesWritten(), extension, &object);
        }
        const bool verifying = status.Succeeded();
        if (status.Succeeded())
        {
            status = VerifyCookStagedObject(CookObjectReader(*m_input.Root), writer.TemporaryPath(), object, m_stream, *m_allocator);
        }
        if (status.Succeeded())
        {
            const auto installed = writer.Commit(object.Path, VFS::AtomicWriteMode::NoReplace);
            status               = installed.Failed() && installed.Error() != VFS::VFSError::AlreadyExists ? installed : scene ? m_reuse.AcceptInstalledScene(index, object, diagnostics) : m_reuse.AcceptInstalledAsset(index, object, diagnostics);
        }
        const auto source = object.Path.IsValid() ? object.Path : writer.TemporaryPath();
        CleanupStage(writer, sink.Reserved, diagnostics);
        return status.Succeeded() ? status : Fail(CookStage::Encode, verifying && status.Error() == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : PublicationError(status.Error(), scene ? CookDiagnosticCode::InvalidScene : CookDiagnosticCode::InvalidAsset), status.Error(), "Object encoding, staged verification or immutable installation failed; no existing object was replaced.", diagnostics, &source);
    }

    VFS::VFSResult<bool> CookPublication::CompareManifest(const CookManifestInput& input, Core::Containers::ArrayView<uint32_t> workspace, CookManifestOutput* output, CookDiagnostics* diagnostics)
    {
        if (!m_previous.IsValid())
        {
            return VFS::VFSResult<bool>::Ok(false);
        }
        CookMemoryCharge fixed   = {};
        const auto       charged = m_allocator->TryCharge(sizeof(ManifestComparison) + sizeof(VFS::VFSRelativePath) + sizeof(fixed), CookMemoryCategory::StagingManifest, &fixed);
        if (charged.Failed())
        {
            (void) Fail(CookStage::Publish, Detail::ManifestResourceCode(charged.Error()), charged.Error(), "Principal manifest-comparison state exceeds the working budget.", diagnostics);
            return VFS::VFSResult<bool>::Fail(charged.Error());
        }
        const auto path   = PublicationPath(CookManifestName);
        const auto opened = m_input.Root->Open(path, VFS::VFSOpenFlags::Read | VFS::VFSOpenFlags::AllowReplace);
        if (opened.Failed() || !opened.Value())
        {
            const auto error = opened.Failed() ? opened.Error() : VFS::VFSError::IOError;
            (void) Fail(CookStage::Publish, PublicationError(error, CookDiagnosticCode::InvalidManifest), error, "Cannot reopen the previous manifest for exact canonical comparison.", diagnostics, &path);
            return VFS::VFSResult<bool>::Fail(error);
        }
        ManifestComparison comparison   = {};
        comparison.File                 = opened.Value();
        comparison.Buffer               = m_stream;
        bool       emission_unsupported = false;
        const auto compared             = [&]() -> VFS::VFSResult<void> {
            const auto stat = opened.Value()->Stat();
            if (stat.Failed() || stat.Value().IsDirectory)
            {
                return VFS::VFSResult<void>::Fail(stat.Failed() ? stat.Error() : VFS::VFSError::Corrupted);
            }
            comparison.Size = stat.Value().SizeBytes;
            if (comparison.Size > m_input.Limits->MaxManifestBytes)
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
            }
            const auto emitted = WriteCookManifest(input, workspace, comparison, output, *m_allocator);
            if (emitted.Failed())
            {
                emission_unsupported = emitted.Error() == VFS::VFSError::Unsupported;
                return emitted;
            }
            comparison.Equal &= comparison.Position == comparison.Size;
            const auto eof    = opened.Value()->Read(m_stream.first(1), comparison.Size);
            if (eof.Failed() || eof.Value())
            {
                return VFS::VFSResult<void>::Fail(eof.Failed() ? eof.Error() : VFS::VFSError::Corrupted);
            }
            const auto after = opened.Value()->Stat();
            return after.Failed() || after.Value().IsDirectory || after.Value().SizeBytes != comparison.Size ? VFS::VFSResult<void>::Fail(after.Failed() ? after.Error() : VFS::VFSError::Corrupted) : VFS::VFSResult<void>::Ok();
        }();
        const auto closed = m_input.Root->CloseChecked(opened.Value());
        if (compared.Failed() || closed.Failed())
        {
            const auto error = compared.Failed() ? compared.Error() : closed.Error();
            (void) Fail(CookStage::Publish, emission_unsupported ? CookDiagnosticCode::UnsupportedBackend : PublicationError(error, CookDiagnosticCode::InvalidManifest), error, "Exact canonical-manifest comparison failed.", diagnostics, &path);
            return VFS::VFSResult<bool>::Fail(error);
        }
        return VFS::VFSResult<bool>::Ok(comparison.Equal);
    }

    VFS::VFSResult<void> CookPublication::PublishManifest(Core::Containers::ArrayView<uint32_t> workspace, CookPublicationResult* output, CookDiagnostics* diagnostics)
    {
        if (!m_locked || m_finished || m_error != VFS::VFSError::OK || !output || !m_reuse.AllReady())
        {
            return Fail(CookStage::Publish, CookDiagnosticCode::InvalidRequest, m_error == VFS::VFSError::OK ? VFS::VFSError::InvalidPath : m_error, "Publishing requires a healthy sealed run, complete verified results and fixed caller result storage.", diagnostics);
        }
        // Preflight caller storage before any fixed-budget query or file I/O.
        const uint32_t count = static_cast<uint32_t>(m_reuse.AssetResults().size() + m_reuse.SceneResults().size());
        if ((workspace.size() && !workspace.data()) || workspace.size() < count)
        {
            const auto error = workspace.size() && !workspace.data() ? VFS::VFSError::InvalidPath : VFS::VFSError::SizeLimitExceeded;
            return Fail(CookStage::Publish, PublicationError(error), error, "Manifest publication requires complete caller sorting workspace.", diagnostics);
        }
        CookMemoryCharge fixed   = {};
        const auto       charged = m_allocator->TryCharge(sizeof(CookManifestInput) + sizeof(CookPublicationResult) + sizeof(Detail::CookSortState<uint32_t>) + sizeof(fixed), CookMemoryCategory::StagingManifest, &fixed);
        if (charged.Failed())
        {
            return Fail(CookStage::Publish, Detail::ManifestResourceCode(charged.Error()), charged.Error(), "Principal manifest result state exceeds the working budget.", diagnostics);
        }
        const CookManifestInput         input     = {m_input.Graph, m_input.Recipe, m_reuse.AssetResults(), m_reuse.SceneResults(), m_input.Limits};
        CookPublicationResult           result    = {};
        const auto                      object_at = [&](uint32_t index) -> const CookObjectDescriptor& { return index < input.Assets.size() ? input.Assets[index].Object : input.Scenes[index - input.Assets.size()].Object; };
        Detail::CookSortState<uint32_t> sort      = {};
        const auto                      sorted    = Detail::ValidateManifestObjectSizes(count, workspace, sort, object_at);
        if (sorted.Failed())
        {
            return Fail(CookStage::Publish, PublicationError(sorted.Error()), sorted.Error(), "Cannot prepare unique referenced-object totals.", diagnostics);
        }
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto& object = object_at(workspace[i]);
            if (i && object.Path == object_at(workspace[i - 1]).Path)
            {
                continue;
            }
            ++result.ObjectCount;
            result.ReferencedObjectBytes += object.Size; // Fixed v1 caps bound this sum.
        }
        const auto same = CompareManifest(input, workspace, &result.Manifest, diagnostics);
        if (same.Failed())
        {
            return VFS::VFSResult<void>::Fail(m_error = same.Error());
        }
        if (same.Value())
        {
            m_finished = true;
            *output    = result;
            return VFS::VFSResult<void>::Ok();
        }
        // No-op comparison needs no staging owner/sink or unused hash state.
        // Keep this reservation until staging cleanup/destructors have drained.
        CookMemoryCharge staging_fixed   = {};
        const auto       staging_charged = m_allocator->TryCharge(sizeof(VFS::VFSRootAtomicFileWriter) + sizeof(StagingSink) + sizeof(CookObjectDescriptor) + sizeof(VFS::VFSRelativePath) + sizeof(staging_fixed), CookMemoryCategory::StagingManifest, &staging_fixed);
        if (staging_charged.Failed())
        {
            return Fail(CookStage::Publish, Detail::ManifestResourceCode(staging_charged.Error()), staging_charged.Error(), "Principal manifest staging state exceeds the working budget.", diagnostics);
        }
        const auto                   path   = PublicationPath(CookManifestName);
        VFS::VFSRootAtomicFileWriter writer = {};
        auto                         status = writer.Open(*m_input.Root, path);
        if (status.Failed())
        {
            return Fail(CookStage::Publish, PublicationError(status.Error()), status.Error(), "Cannot exclusively create manifest staging.", diagnostics, &path);
        }
        StagingSink sink                = {};
        sink.Writer                     = &writer;
        sink.Buffer                     = m_stream;
        sink.Staged                     = &m_staged_bytes;
        sink.TotalLimit                 = m_input.Limits->MaxStagedBytes;
        sink.ObjectLimit                = m_input.Limits->MaxManifestBytes;
        // The canonical writer hashes exact output once; this sink has no hash.
        status                          = WriteCookManifest(input, workspace, sink, &result.Manifest, *m_allocator);
        const bool emission_unsupported = status.Error() == VFS::VFSError::Unsupported;
        if (status.Succeeded())
        {
            status = sink.Flush();
        }
        if (status.Succeeded())
        {
            status = writer.Seal();
        }
        const CookObjectDescriptor manifest  = {.Digest = result.Manifest.Digest, .Size = result.Manifest.Bytes};
        const bool                 verifying = status.Succeeded();
        if (status.Succeeded())
        {
            status = VerifyCookStagedObject(CookObjectReader(*m_input.Root), writer.TemporaryPath(), manifest, m_stream, *m_allocator);
        }
        result.Published = true; // Every result field is prepared before commit.
        if (status.Succeeded())
        {
            status = writer.Commit(path, VFS::AtomicWriteMode::Replace);
        }
        if (status.Failed())
        {
            CleanupStage(writer, sink.Reserved, diagnostics);
            return Fail(CookStage::Publish, (verifying || emission_unsupported) && status.Error() == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : PublicationError(status.Error(), CookDiagnosticCode::InvalidManifest), status.Error(), "Manifest staging/verification/atomic commit failed; the old manifest remains authoritative.", diagnostics, &path);
        }
        // The replacing rename above is the ONLY commit. No validation,
        // allocation or producer callback occurs after this point.
        m_finished = true;
        *output    = result;
        CleanupStage(writer, sink.Reserved, diagnostics);
        return VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Cook
