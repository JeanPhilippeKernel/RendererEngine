#include <ZEngine/Cook/CookAdapterSupport.h>
#include <ZEngine/Cook/CookSceneCodec.h>
#include <ZEngine/ECS/BinarySceneCodec.h>
#include <ZEngine/ECS/SceneCompatibility.h>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    CookSceneCodec::CookSceneCodec(ICookAllocator& allocator, const ECS::SceneComponentSchemaRegistry& schemas, const ECS::SceneMigrationRegistry* migrations) noexcept : m_allocator(&allocator), m_schemas(&schemas), m_migrations(migrations), m_route(allocator) {}

    VFS::VFSResult<void> CookSceneCodec::Fail(const CookSceneInput& input, CookStage stage, CookDiagnostics* diagnostics, CookDiagnosticCode code, VFS::VFSError error, cstring message, const uuids::uuid& uuid)
    {
        if (auto* diagnostic = diagnostics ? diagnostics->Emplace(stage, code, error) : nullptr)
        {
            diagnostic->Source    = input.Source;
            diagnostic->SceneUUID = uuid;
            (void) diagnostic->SetMessage(message);
        }
        return VFS::VFSResult<void>::Fail(error);
    }

    VFS::VFSResult<void> CookSceneCodec::SceneFailure(const CookSceneInput& input, CookStage stage, CookDiagnostics* diagnostics, cstring fallback, const uuids::uuid& uuid)
    {
        auto code  = CookDiagnosticCode::InvalidScene;
        auto error = VFS::VFSError::Corrupted;
        if (m_route.Error() != VFS::VFSError::OK)
        {
            error = m_route.Error();
            code  = error == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : Detail::AdapterErrorCode(error, CookDiagnosticCode::InvalidScene);
        }
        else if (m_diagnostics.HasError(ECS::SceneDiagnosticCode::LimitExceeded))
        {
            error = VFS::VFSError::SizeLimitExceeded;
            code  = CookDiagnosticCode::BudgetExceeded;
        }
        else if (m_diagnostics.HasError(ECS::SceneDiagnosticCode::AllocationFailed))
        {
            error = VFS::VFSError::OutOfMemory;
            code  = CookDiagnosticCode::AllocationFailed;
        }
        else if (m_diagnostics.HasError(ECS::SceneDiagnosticCode::UnsupportedVersion))
        {
            error = VFS::VFSError::Unsupported;
            code  = CookDiagnosticCode::UnsupportedVersion;
        }
        else if (m_diagnostics.HasError(ECS::SceneDiagnosticCode::InvalidArgument))
        {
            error = VFS::VFSError::InvalidPath;
            code  = CookDiagnosticCode::InvalidRequest;
        }
        cstring message = fallback;
        for (uint32_t i = 0; i < m_diagnostics.Count; ++i)
        {
            const auto& entry = m_diagnostics.Entries[i];
            if (entry.Severity == ECS::SceneDiagnosticSeverity::Error)
            {
                message = entry.Message;
                break;
            }
        }
        return Fail(input, stage, diagnostics, code, error, message, uuid);
    }

    VFS::VFSResult<void> CookSceneCodec::Read(const CookSceneInput& input, ECS::SceneDocument* output, CookDiagnostics* diagnostics, CookStage stage)
    {
        m_diagnostics.Clear();
        if (!output)
        {
            return Fail(input, stage, diagnostics, CookDiagnosticCode::InvalidRequest, VFS::VFSError::InvalidPath, "Scene reading requires result storage.");
        }
        uint64_t   size      = 0;
        const auto validated = ValidateInput(input, stage, diagnostics, size);
        return validated.Failed() ? validated : ReadValidated(input, size, output, diagnostics, stage);
    }

    VFS::VFSResult<void> CookSceneCodec::ValidateInput(const CookSceneInput& input, CookStage stage, CookDiagnostics* diagnostics, uint64_t& source_bytes)
    {
        if (!input.Capture || !input.Limits || !IsCookLogicalPath(input.Source) || !m_schemas->IsInitialized() || (m_migrations && !m_migrations->IsInitialized()) || (stage != CookStage::Discover && stage != CookStage::Encode))
        {
            return Fail(input, stage, diagnostics, CookDiagnosticCode::InvalidRequest, VFS::VFSError::InvalidPath, "Scene reading requires captured input, logical source, schemas, limits and result storage.");
        }
        const auto valid_limits = ValidateCookLimits(*input.Limits);
        if (valid_limits.Failed())
        {
            return Fail(input, stage, diagnostics, CookDiagnosticCode::InvalidRequest, valid_limits.Error(), "Scene cook limits are invalid.");
        }
        if (m_route.Error() != VFS::VFSError::OK)
        {
            return SceneFailure(input, stage, diagnostics, "Scene allocator already refused this cook run.");
        }
        const auto size = input.Capture->Size();
        if (size > input.Limits->SceneLimits.MaxFileBytes)
        {
            return Fail(input, stage, diagnostics, CookDiagnosticCode::BudgetExceeded, VFS::VFSError::SizeLimitExceeded, "Captured scene exceeds its source byte limit.");
        }
        if (!size)
        {
            return Fail(input, stage, diagnostics, CookDiagnosticCode::InvalidScene, VFS::VFSError::Corrupted, "Captured scene source is empty.");
        }
        source_bytes = size;
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookSceneCodec::ReadValidated(const CookSceneInput& input, uint64_t size, ECS::SceneDocument* output, CookDiagnostics* diagnostics, CookStage stage)
    {
        Core::Memory::WorkingMemoryCharge fixed = {};
        if (!fixed.TryAcquire(m_route.WorkingMemoryBudget(), sizeof(CookWorkspace) + Core::Hash::SHA256::FixedWorkingBytes() + sizeof(Digest256) + sizeof(ECS::SceneDocument) + sizeof(fixed)))
        {
            return SceneFailure(input, stage, diagnostics, "Principal scene read state exceeds the working budget.");
        }
        CookWorkspace source    = {};
        const auto    allocated = source.Initialize(*m_allocator, static_cast<size_t>(size), CookMemoryCategory::Codecs);
        if (allocated.Failed())
        {
            return Fail(input, stage, diagnostics, Detail::AdapterErrorCode(allocated.Error(), CookDiagnosticCode::InvalidScene), allocated.Error(), "Scene source allocation failed.");
        }
        auto* bytes = static_cast<uint8_t*>(source.Arena()->AllocateNoZero(static_cast<size_t>(size), 1));
        if (!bytes)
        {
            return Fail(input, stage, diagnostics, CookDiagnosticCode::BudgetExceeded, VFS::VFSError::SizeLimitExceeded, "Scene source workspace capacity was exhausted.");
        }
        const auto read = ReadCookCapturedRange(*input.Capture, 0, {bytes, static_cast<size_t>(size)});
        if (read.Failed())
        {
            return Fail(input, stage, diagnostics, Detail::AdapterErrorCode(read.Error(), CookDiagnosticCode::InputChanged), read.Error(), "Captured scene could not be read completely.");
        }
        Core::Hash::SHA256 hash   = {};
        Digest256          digest = {};
        if (hash.Update({bytes, static_cast<size_t>(size)}).Failed() || hash.Finalize(&digest).Failed() || digest != input.Capture->Digest())
        {
            return Fail(input, stage, diagnostics, CookDiagnosticCode::InputChanged, VFS::VFSError::Corrupted, "Captured scene does not match its captured source digest.");
        }
        ECS::SceneDocument candidate = {};
        if (!ECS::SceneDocument::Parse(reinterpret_cast<cstring>(bytes), static_cast<size_t>(size), *m_schemas, &candidate, &m_diagnostics, input.Limits->SceneLimits, m_migrations, &m_route) || !ECS::SceneCompatibilityService::PrepareForSave(candidate, ECS::SceneCompatibilityUse::CookedRuntime, &m_diagnostics) || m_route.Error() != VFS::VFSError::OK)
        {
            return SceneFailure(input, stage, diagnostics, "Captured scene parsing failed.");
        }
        *output = std::move(candidate);
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookSceneCodec::Encode(const CookSceneInput& input, const uuids::uuid& expected_uuid, CookTarget target, ICookByteSink& sink, CookSceneOutput* output, CookDiagnostics* diagnostics)
    {
        if (!output || expected_uuid.is_nil())
        {
            return Fail(input, CookStage::Encode, diagnostics, CookDiagnosticCode::InvalidRequest, VFS::VFSError::InvalidPath, "Scene encoding requires its discovered UUID and result storage.");
        }
        if (target != CookTarget::DesktopVulkanLE64)
        {
            return Fail(input, CookStage::Encode, diagnostics, CookDiagnosticCode::UnsupportedTarget, VFS::VFSError::Unsupported, "Scene target is unsupported.", expected_uuid);
        }
        m_diagnostics.Clear();
        uint64_t   source_bytes = 0;
        const auto validated    = ValidateInput(input, CookStage::Encode, diagnostics, source_bytes);
        if (validated.Failed())
        {
            return validated;
        }
        Core::Memory::WorkingMemoryCharge fixed = {};
        if (!fixed.TryAcquire(m_route.WorkingMemoryBudget(), sizeof(ECS::SceneDocument) + sizeof(CookWorkspace) + sizeof(Core::Containers::Array<uint8_t>) + Core::Hash::SHA256::FixedWorkingBytes() + sizeof(CookSceneOutput) + sizeof(size_t) + sizeof(fixed)))
        {
            return SceneFailure(input, CookStage::Encode, diagnostics, "Principal scene encode state exceeds the working budget.", expected_uuid);
        }
        ECS::SceneDocument document = {};
        const auto         parsed   = ReadValidated(input, source_bytes, &document, diagnostics, CookStage::Encode);
        if (parsed.Failed())
        {
            return parsed;
        }
        if (document.Settings()->UUID != expected_uuid)
        {
            return Fail(input, CookStage::Encode, diagnostics, CookDiagnosticCode::IdentityConflict, VFS::VFSError::Corrupted, "Captured scene UUID disagrees with discovery.", expected_uuid);
        }
        size_t measured = 0;
        if (!ECS::BinarySceneCodec::Measure(document, *m_schemas, &measured, &m_diagnostics, input.Limits->SceneLimits))
        {
            return SceneFailure(input, CookStage::Encode, diagnostics, "Binary scene measurement failed.", expected_uuid);
        }
        CookWorkspace workspace = {};
        const auto    allocated = workspace.Initialize(*m_allocator, measured, CookMemoryCategory::Codecs);
        if (allocated.Failed())
        {
            return Fail(input, CookStage::Encode, diagnostics, Detail::AdapterErrorCode(allocated.Error(), CookDiagnosticCode::InvalidScene), allocated.Error(), "Binary scene output allocation failed.", expected_uuid);
        }
        Core::Containers::Array<uint8_t> bytes = {};
        if (!ECS::BinarySceneCodec::Encode(document, *m_schemas, workspace.Arena(), &bytes, &m_diagnostics, input.Limits->SceneLimits))
        {
            return SceneFailure(input, CookStage::Encode, diagnostics, "Binary scene encoding failed.", expected_uuid);
        }
        if (bytes.size() != measured)
        {
            return Fail(input, CookStage::Encode, diagnostics, CookDiagnosticCode::InvalidScene, VFS::VFSError::Corrupted, "Scene binary codec changed its measured size.", expected_uuid);
        }
        Core::Hash::SHA256 hash      = {};
        CookSceneOutput    candidate = {.UUID = expected_uuid, .Bytes = bytes.size()};
        if (hash.Update({bytes.data(), bytes.size()}).Failed() || hash.Finalize(&candidate.Digest).Failed())
        {
            return Fail(input, CookStage::Encode, diagnostics, CookDiagnosticCode::InvalidScene, VFS::VFSError::Corrupted, "Scene binary digest failed.", expected_uuid);
        }
        const auto written = sink.Write({bytes.data(), bytes.size()});
        if (written.Failed())
        {
            return Fail(input, CookStage::Encode, diagnostics, Detail::AdapterErrorCode(written.Error(), CookDiagnosticCode::CorruptObject), written.Error(), "Scene binary staging write failed.", expected_uuid);
        }
        *output = candidate;
        return VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Cook
