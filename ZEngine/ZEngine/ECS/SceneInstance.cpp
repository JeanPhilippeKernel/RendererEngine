#include <ZEngine/ECS/BinarySceneCodec.h>
#include <ZEngine/ECS/SceneInstance.h>
#include <ZEngine/Managers/CookedPackageSnapshot.h>
#include <ZEngine/Profiling/Profiling.h>
#include <new>
#include <utility>

namespace ZEngine::ECS
{
    SceneInstance::~SceneInstance()
    {
        RecordMemoryUsage();
        if (m_initialized)
        {
            m_commands.Clear();
            m_actors.Shutdown();
            m_commands.Clear();
            m_world.Shutdown();
        }
    }

    void SceneInstance::RecordMemoryUsage() const noexcept
    {
        const auto* arena = Arena();
        if (!arena)
        {
            return;
        }
        const auto document = DocumentMemory();
        ZENGINE_PROFILE_VALUE("SceneLoad.Memory.SceneArenaCapacityBytes", arena->m_total_size);
        ZENGINE_PROFILE_VALUE("SceneLoad.Memory.SceneArenaReservedBytes", arena->m_current_offset);
        ZENGINE_PROFILE_VALUE("SceneLoad.Memory.DocumentCapacityBytes", document.Capacity);
        ZENGINE_PROFILE_VALUE("SceneLoad.Memory.DocumentAllocatedChargeBytes", document.Allocated);
        ZENGINE_PROFILE_VALUE("SceneLoad.Memory.DocumentWorkingChargeBytes", document.Working);
        ZENGINE_PROFILE_VALUE("SceneLoad.Memory.DocumentPeakChargeBytes", document.Peak);
    }

    bool SceneInstance::Create(SceneStorageLease&& storage, std::unique_ptr<SceneInstance>* out, SceneDiagnostics* diagnostics)
    {
        const auto* arena = storage.Arena();
        if (!out || !arena || !arena->m_memory || arena->m_current_offset > arena->m_total_size || arena->m_total_size - arena->m_current_offset < SceneStorageLease::MinimumBytes)
        {
            SceneDiagnosticError(diagnostics, "Scene instance requires a destination and sufficient leased world storage.", SceneDiagnosticCode::InvalidArgument);
            return false;
        }
        auto instance = std::unique_ptr<SceneInstance>(new (std::nothrow) SceneInstance{});
        if (!instance)
        {
            SceneDiagnosticError(diagnostics, "Cannot allocate scene instance ownership.", SceneDiagnosticCode::AllocationFailed);
            return false;
        }
        instance->m_storage = std::move(storage);
        instance->m_world.Initialize(instance->Arena());
        instance->m_actors.Initialize(instance->Arena(), instance->m_world);
        instance->m_commands.Initialize(instance->Arena());
        instance->m_tick.Initialize(instance->Arena());
        instance->m_initialized = true;
        instance->RecordMemoryUsage();
        *out = std::move(instance);
        return true;
    }

    bool SceneInstance::Populate(SceneDocument&& document, const SceneComponentSchemaRegistry& schemas, SceneDiagnostics* diagnostics)
    {
        if (m_owned_document_kind != OwnedDocumentKind::None)
        {
            SceneDiagnosticError(diagnostics, "An instance-owned source document cannot be replaced by an external snapshot.", SceneDiagnosticCode::InvalidArgument);
            return false;
        }
        if (!PopulateDocument(document, schemas, diagnostics))
        {
            return false;
        }
        m_document = std::move(document);
        return true;
    }

    bool SceneInstance::PopulateDocument(const SceneDocument& document, const SceneComponentSchemaRegistry& schemas, SceneDiagnostics* diagnostics)
    {
        if (!m_initialized || m_population_attempted || !document.Settings() || m_world.AliveCount() || m_actors.Count())
        {
            SceneDiagnosticError(diagnostics, "Scene reconstruction requires a fresh unpublished instance and a valid document.", SceneDiagnosticCode::InvalidArgument);
            return false;
        }
        m_population_attempted = true;
        const bool populated   = document.PopulateCandidate(m_world, schemas, diagnostics);
        RecordMemoryUsage();
        if (!populated)
        {
            return false;
        }
        // Pure, candidate-local derived state belongs to the reconstruction
        // owner, not the later main-thread publication. No actor callbacks or
        // renderer/cache access is involved; hierarchy storage is scene-bounded.
        m_world.SynchronizeHierarchy();
        return true;
    }

    bool SceneInstance::ReadSource(Core::VFS::IVFSContext& vfs, const Core::VFS::VFSPath& path, const SceneComponentSchemaRegistry& schemas, size_t working_bytes, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits, const SceneMigrationRegistry* migrations)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Worker.SourceRead");
        if (!m_initialized || m_owned_document_kind != OwnedDocumentKind::None || m_population_attempted || m_document.Settings() || m_world.AliveCount() || !working_bytes)
        {
            SceneDiagnosticError(diagnostics, "Source reconstruction requires a fresh owner and a finite document workspace.", SceneDiagnosticCode::InvalidArgument);
            return false;
        }
        m_owned_document_kind = OwnedDocumentKind::Source;
        if (!m_document_route.Initialize(Arena(), working_bytes))
        {
            SceneDiagnosticError(diagnostics, "Cannot reserve the document workspace within admitted scene storage.", SceneDiagnosticCode::AllocationFailed);
            return false;
        }
        const bool read = SceneDocument::Read(vfs, path, schemas, &m_document, diagnostics, limits, migrations, {.AllocationRoute = &m_document_route});
        RecordMemoryUsage();
        return read;
    }

    bool SceneInstance::PopulateSource(const SceneComponentSchemaRegistry& schemas, SceneDiagnostics* diagnostics)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Worker.Populate");
        if (m_owned_document_kind != OwnedDocumentKind::Source)
        {
            SceneDiagnosticError(diagnostics, "Source population requires an instance-owned source read.", SceneDiagnosticCode::InvalidArgument);
            return false;
        }
        return PopulateDocument(m_document, schemas, diagnostics);
    }

    bool SceneInstance::ReadCooked(const Managers::CookedObjectSnapshot& object, const SceneComponentSchemaRegistry& schemas, size_t working_bytes, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Worker.CookedDecode");
        if (!m_initialized || m_owned_document_kind != OwnedDocumentKind::None || m_population_attempted || m_document.Settings() || m_world.AliveCount() || !working_bytes || object.Bytes().empty())
        {
            SceneDiagnosticError(diagnostics, "Cooked reconstruction requires a fresh owner, verified bytes and a finite document workspace.", SceneDiagnosticCode::InvalidArgument);
            return false;
        }
        m_owned_document_kind = OwnedDocumentKind::Cooked;
        if (!m_document_route.Initialize(Arena(), working_bytes))
        {
            SceneDiagnosticError(diagnostics, "Cannot reserve the document workspace within admitted scene storage.", SceneDiagnosticCode::AllocationFailed);
            return false;
        }
        const bool read = BinarySceneCodec::Decode(object.Bytes(), schemas, &m_document, diagnostics, limits, &m_document_route);
        RecordMemoryUsage();
        return read;
    }

    bool SceneInstance::ReadVerifiedCooked(const Managers::CookedPackageSnapshot& package, const Managers::CookedObjectSnapshot& object, const SceneComponentSchemaRegistry& schemas, size_t working_bytes, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits, const std::atomic<bool>* cancelled)
    {
        const auto* scene = package.Scene();
        if (!scene || object.Descriptor() != scene->Result.Object)
        {
            SceneDiagnosticError(diagnostics, "Cooked scene bytes do not match the selected package object.", SceneDiagnosticCode::InvalidData);
            return false;
        }
        if (cancelled && cancelled->load(std::memory_order_acquire))
        {
            SceneDiagnosticError(diagnostics, "Cooked scene reconstruction was cancelled.", SceneDiagnosticCode::InvalidData);
            return false;
        }
        if (!ReadCooked(object, schemas, working_bytes, diagnostics, limits))
        {
            return false;
        }
        if (package.ValidateScene(m_document, schemas, diagnostics, cancelled).Failed())
        {
            SceneDiagnosticError(diagnostics, "Cooked scene references do not match the selected package.", SceneDiagnosticCode::InvalidData);
            return false;
        }
        m_cooked_validated = true;
        m_cooked_manifest  = package.ManifestDigest();
        return true;
    }

    bool SceneInstance::MatchesCookedClosure(const Core::Hash::Digest256& manifest, const uuids::uuid& scene) const noexcept
    {
        const auto* settings = m_document.Settings();
        return m_cooked_validated && settings && settings->UUID == scene && m_cooked_manifest == manifest;
    }

    bool SceneInstance::PopulateCooked(const SceneComponentSchemaRegistry& schemas, SceneDiagnostics* diagnostics)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Worker.Populate");
        if (m_owned_document_kind != OwnedDocumentKind::Cooked || !m_cooked_validated)
        {
            SceneDiagnosticError(diagnostics, "Cooked population requires a package-validated instance-owned decode.", SceneDiagnosticCode::InvalidArgument);
            return false;
        }
        return PopulateDocument(m_document, schemas, diagnostics);
    }

    bool SceneInstance::TryReleaseDocumentForDiscard(SceneDocument* out) noexcept
    {
        if (!out || m_owned_document_kind != OwnedDocumentKind::None || !m_document.Settings())
        {
            return false;
        }
        *out = std::move(m_document);
        return true;
    }
} // namespace ZEngine::ECS
