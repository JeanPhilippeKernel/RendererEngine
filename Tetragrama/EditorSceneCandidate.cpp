#include <Tetragrama/EditorSceneCandidate.h>
#include <Tetragrama/EditorSceneIO.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/CookedSceneReconstruction.h>
#include <ZEngine/ECS/Systems/HierarchySystem.h>
#include <ZEngine/ECS/Systems/LightSyncSystem.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Managers/CookedCpuAssetSet.h>
#include <ZEngine/Profiling/Profiling.h>
#include <algorithm>
#include <new>

using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;

namespace Tetragrama
{
    namespace
    {
        bool Reject(SceneDiagnostics* diagnostics, cstring message)
        {
            SceneDiagnosticError(diagnostics, message);
            return false;
        }
        /// Retain the adapter's existing failure contract after the engine has
        /// reconstructed the ECS: later editor binding failure returns the input
        /// document before the unpublished engine owner releases its slot.
        struct DocumentRollback
        {
            SceneDocument& Document;
            SceneInstance& Instance;
            bool           Committed = false;

            ~DocumentRollback()
            {
                if (!Committed)
                {
                    [[maybe_unused]] const bool released = Instance.TryReleaseDocumentForDiscard(&Document);
                    ZENGINE_VALIDATE_ASSERT(released, "Synchronous candidate rollback cannot return an instance-owned routed document.")
                }
            }
        };
    } // namespace

    EditorSceneCandidate::~EditorSceneCandidate() = default;

    bool EditorSceneCandidate::PrepareRenderResources(ZEngine::Rendering::RenderResourceManager& resources, SceneRequestId request, uint64_t generation, const ZEngine::Rendering::Scenes::SceneInitialView& view, const ZEngine::Managers::CookedCpuAssetSet* cooked, bool* terminal_failure)
    {
        return m_scene_resources.Prepare(*m_instance, m_render, {m_required_meshes.data(), m_required_meshes.size()}, resources, request, generation, view, cooked, terminal_failure);
    }

    bool EditorSceneCandidate::PollRenderResources(ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationResult* out) const
    {
        return m_scene_resources.Poll(out);
    }

    ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationId EditorSceneCandidate::InitialRenderResources()
    {
        return m_scene_resources.InitialResources();
    }

    bool EditorSceneCandidate::Prepare(ZEngine::EngineContext& engine, SceneDocument&& document, const SceneComponentSchemaRegistry& schemas, const ZEngine::Core::VFS::VFSPath& source, const EditorSceneAssetResolver& assets, ZEngine::Helpers::Scope<EditorSceneCandidate>* out, SceneDiagnostics* diagnostics, const EditorSceneCandidateLimits& limits)
    {
        SceneStorageLease storage = {};
        if (!engine.TryAcquireSceneStorage(&storage))
        {
            return Reject(diagnostics, "No bounded scene-load slot is available; the active scene was preserved.");
        }
        return Prepare(std::move(storage), std::move(document), schemas, source, assets, out, diagnostics, limits);
    }

    bool EditorSceneCandidate::Prepare(SceneStorageLease&& storage, SceneDocument&& document, const SceneComponentSchemaRegistry& schemas, const ZEngine::Core::VFS::VFSPath& source, const EditorSceneAssetResolver& assets, ZEngine::Helpers::Scope<EditorSceneCandidate>* out, SceneDiagnostics* diagnostics, const EditorSceneCandidateLimits& limits)
    {
        constexpr auto gpu_budget = ZEngine::Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS;
        const auto*    settings   = document.Settings();
        if (!out || !settings || !schemas.IsInitialized() || !IsSceneSourcePath(source) || limits.MaxInstances > gpu_budget || limits.MaxDrawCommands > gpu_budget)
        {
            return Reject(diagnostics, "Invalid scene candidate destination, source, schemas or render limits.");
        }

        auto candidate = ZEngine::Helpers::Scope<EditorSceneCandidate>(new (std::nothrow) EditorSceneCandidate{});
        if (!candidate)
        {
            return Reject(diagnostics, "Cannot allocate editor scene ownership.");
        }
        if (!SceneInstance::Create(std::move(storage), &candidate->m_instance, diagnostics))
        {
            return Reject(diagnostics, "No bounded scene-load slot is available; the active scene was preserved.");
        }
        if (!candidate->m_instance->Populate(std::move(document), schemas, diagnostics))
        {
            return false;
        }
        DocumentRollback rollback = {document, *candidate->m_instance};
        if (!candidate->PrepareRenderAdapter(source, assets, diagnostics, limits))
        {
            return false;
        }
        rollback.Committed = true;
        *out               = std::move(candidate);
        return true;
    }

    bool EditorSceneCandidate::PreparePopulated(std::unique_ptr<SceneInstance>&& instance, const ZEngine::Core::VFS::VFSPath& source, const EditorSceneAssetResolver& assets, ZEngine::Helpers::Scope<EditorSceneCandidate>* out, SceneDiagnostics* diagnostics, const EditorSceneCandidateLimits& limits)
    {
        if (!out || !instance || !instance->Document().Settings() || instance->Actors().Count() || !instance->Commands().IsEmpty() || instance->Tick().SystemCount())
        {
            return Reject(diagnostics, "Editor binding requires a complete unpublished engine owner with no actor lifecycle work.");
        }
        auto candidate = ZEngine::Helpers::Scope<EditorSceneCandidate>(new (std::nothrow) EditorSceneCandidate{});
        if (!candidate)
        {
            return Reject(diagnostics, "Cannot allocate editor scene ownership.");
        }
        candidate->m_instance = std::move(instance);
        if (!candidate->PrepareRenderAdapter(source, assets, diagnostics, limits))
        {
            return false;
        }
        *out = std::move(candidate);
        return true;
    }

    bool EditorSceneCandidate::PreparePopulatedCooked(std::unique_ptr<SceneInstance>&& instance, const ZEngine::Core::VFS::VFSPath& source, const ZEngine::Managers::CookedCpuAssetSet& assets, ZEngine::Helpers::Scope<EditorSceneCandidate>* out, SceneDiagnostics* diagnostics, const EditorSceneCandidateLimits& limits)
    {
        if (!instance || !assets.IsValid() || !instance->MatchesCookedClosure(assets.ManifestDigest(), assets.SceneUUID()))
        {
            return Reject(diagnostics, "Scene binding requires the verified closure for this cooked scene and manifest.");
        }
        const auto*                    closure  = &assets;
        const EditorSceneAssetResolver resolver = {&closure, [](void* context, const uuids::uuid& uuid, uint32_t* draws, SceneDiagnostics*) {
                                                       const auto*                            set      = *static_cast<const ZEngine::Managers::CookedCpuAssetSet**>(context);
                                                       const ZEngine::Cook::CookAssetIdentity identity = {ZEngine::Cook::CookAssetKind::Mesh, uuid};
                                                       const auto*                            mesh     = set->FindMesh(identity);
                                                       if (!draws || !mesh || mesh->SubMeshes.empty() || mesh->SubMeshes.size() > UINT32_MAX)
                                                       {
                                                           return false;
                                                       }
                                                       *draws = static_cast<uint32_t>(mesh->SubMeshes.size());
                                                       return true;
                                                   }};
        if (!out || *out || !instance->Document().Settings() || instance->Actors().Count() || !instance->Commands().IsEmpty() || instance->Tick().SystemCount())
        {
            return Reject(diagnostics, "Cooked editor binding requires an unpublished engine owner without actor lifecycle work.");
        }
        auto candidate = ZEngine::Helpers::Scope<EditorSceneCandidate>(new (std::nothrow) EditorSceneCandidate{});
        if (!candidate)
            return Reject(diagnostics, "Cannot allocate editor scene ownership.");
        candidate->m_instance = std::move(instance);
        if (!candidate->PrepareRenderAdapter(source, resolver, diagnostics, limits, true))
            return false;
        *out = std::move(candidate);
        return true;
    }

    CookedCandidateState EditorSceneCandidate::PrepareReconstructedCooked(CookedSceneReconstruction& reconstruction, const ZEngine::Core::VFS::VFSPath& source, ZEngine::Helpers::Scope<EditorSceneCandidate>* out, SceneDiagnostics* diagnostics, ZEngine::Core::VFS::VFSError* asset_error, const EditorSceneCandidateLimits& limits)
    {
        if (!out || *out || !reconstruction.Instance || !reconstruction.Assets)
        {
            Reject(diagnostics, "Cooked scene binding requires an unpublished reconstruction and an empty destination.");
            return CookedCandidateState::Failed;
        }
        ZEngine::Managers::CookedCpuClosureCache::Result result = {};
        if (!reconstruction.Assets.Poll(&result))
        {
            Reject(diagnostics, "The cooked asset lease is no longer valid.");
            return CookedCandidateState::Failed;
        }
        if (result.StateValue == ZEngine::Managers::CookedCpuClosureCache::State::Pending)
        {
            return CookedCandidateState::Pending;
        }
        if (result.StateValue != ZEngine::Managers::CookedCpuClosureCache::State::Ready || !result.Assets)
        {
            if (asset_error)
            {
                *asset_error = result.Error;
            }
            Reject(diagnostics, "The cooked asset closure could not be prepared.");
            return CookedCandidateState::Failed;
        }
        return PreparePopulatedCooked(std::move(reconstruction.Instance), source, *result.Assets, out, diagnostics, limits) ? CookedCandidateState::Ready : CookedCandidateState::Failed;
    }

    bool EditorSceneCandidate::PrepareRenderAdapter(const ZEngine::Core::VFS::VFSPath& source, const EditorSceneAssetResolver& assets, SceneDiagnostics* diagnostics, const EditorSceneCandidateLimits& limits, bool cooked)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Worker.RenderAdapter");
        constexpr auto gpu_budget = ZEngine::Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS;
        const auto*    settings   = Document().Settings();
        if (!settings || (cooked ? (!source.IsValid() || source.IsRoot()) : !IsSceneSourcePath(source)) || limits.MaxInstances > gpu_budget || limits.MaxDrawCommands > gpu_budget)
        {
            return Reject(diagnostics, "Invalid scene source or editor render limits.");
        }
        auto& render = m_render;
        if (!render.InitializeDeserialized(*m_instance->Arena()))
        {
            return Reject(diagnostics, "Scene storage cannot retain the complete editor render adapter.");
        }
        auto&      world       = World();

        // Directional lights used to own a renderable editor-icon mesh. Drop
        // that retired component while loading old scene files so its removed
        // asset cannot make an otherwise valid scene fail to load.
        const auto legacy_icon = uuids::uuid::from_string("ff000000-0000-0000-0000-000000000001");
        if (!legacy_icon)
        {
            return Reject(diagnostics, "The legacy directional-light icon identity is invalid.");
        }
        {
            ZEngine::Core::Memory::ArenaCheckpoint     scratch(m_instance->Arena());
            ZEngine::Core::Containers::Array<EntityID> retired_icons = {};
            retired_icons.m_allocator                                = m_instance->Arena();
            bool scratch_ok                                          = true;
            world.ForEach<MeshComponent, LightComponent>([&](EntityID entity, const MeshComponent& mesh, const LightComponent& light) {
                if (mesh.MeshUUID == *legacy_icon && light.LightType == LightComponent::Type::Directional && !retired_icons.try_push(entity))
                    scratch_ok = false;
            });
            if (!scratch_ok)
                return Reject(diagnostics, "Scene storage cannot retain legacy icon cleanup scratch.");
            for (const EntityID entity : retired_icons)
                world.RemoveComponent<MeshComponent>(entity);
            m_instance->RecordMemoryUsage();
        }

        if (!ZEngine::Rendering::Scenes::BuildSceneMeshBindings(*m_instance, render, assets, limits, m_required_meshes, m_draw_count, diagnostics))
            return false;

        render.Name         = settings->Name.data(); // NUL-terminated text owned by the retained document.
        render.UUID         = settings->UUID;
        render.AuthoredGrid = settings->Grid;
        render.SyncGridFromAuthored();
        render.Sky            = settings->Sky;
        render.SourcePath     = source;
        render.SourceReadOnly = cooked || Document().ReadOnly();
        render.MarkDirty(false);
        render.MarkInstancesDirty();
        render.MarkSkyDirty();
        m_instance->RecordMemoryUsage();
        // Settings/name and unknown component data remain owned for the entire
        // loaded-world lifetime, independent of parser scratch or source files.
        return true;
    }
} // namespace Tetragrama
