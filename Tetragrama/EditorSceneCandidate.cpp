#include <Tetragrama/EditorSceneCandidate.h>
#include <Tetragrama/EditorSceneIO.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Systems/HierarchySystem.h>
#include <ZEngine/ECS/Systems/LightSyncSystem.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Profiling/Profiling.h>
#include <algorithm>
#include <new>
#include <vector>

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
        struct MeshRequirement
        {
            uuids::uuid UUID = {};
            uint32_t    Uses = 1;
        };

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

    EditorSceneCandidate::~EditorSceneCandidate()
    {
        if (m_resources && m_resource_preparation)
        {
            m_resources->ReleaseSceneResourcePreparation(m_resource_preparation);
        }
    }

    bool EditorSceneCandidate::PrepareRenderResources(ZEngine::Rendering::RenderResourceManager& resources, SceneRequestId request, uint64_t generation, const ZEngine::Rendering::Scenes::SceneInitialView& view)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Main.ResourceAdmission");
        if (m_resource_preparation)
        {
            if (m_resources != &resources)
            {
                return false;
            }
            if (m_resource_instances_copied < m_render.Instances.size())
            {
                const auto count = std::min(size_t(64), m_render.Instances.size() - m_resource_instances_copied);
                if (resources.AppendSceneResourceInstances(m_resource_preparation, {m_render.Instances.data() + m_resource_instances_copied, count}))
                {
                    m_resource_instances_copied += static_cast<uint32_t>(count);
                }
                // A refused batch is a terminal resource result, not backpressure.
                // The caller polls it and preserves the outgoing generation.
            }
            return true;
        }
        // Resolve the candidate's sun, never the outgoing world's light state.
        ZEngine::ECS::Systems::SyncHierarchy(World());
        // The unpublished adapter already has exact entity membership. Copy
        // directly rather than performing a linear instance-ID lookup per entity.
        for (auto& instance : m_render.Instances)
        {
            const auto* transform = World().GetComponent<TransformComponent>(instance.Entity);
            if (!transform)
            {
                return false;
            }
            instance.Transform = transform->WorldTransform;
        }
        m_render.MarkInstancesDirty();
        ZEngine::ECS::Systems::SyncECSToLights(World(), m_render);
        const ZEngine::Rendering::RenderResourceManager::SceneEnvironmentInput  environment   = {m_render.Sky, m_render.CelestialLight, m_render.SkyRevision.value.load(std::memory_order_acquire)};
        const ZEngine::Rendering::RenderResourceManager::SceneInitialFrameInput initial_frame = {
            {m_render.Instances.data(), m_render.Instances.size()},
            m_render.PendingLights, m_render.Grid, view
        };
        if (!resources.PrepareSceneResources(request, generation, {m_required_meshes.data(), m_required_meshes.size()}, &m_resource_preparation, ZEngine::Rendering::RenderResourceManager::ScenePreparationContent::MeshMaterials, &environment, &initial_frame, true))
        {
            return false;
        }
        m_resource_instances_copied = 0;
        m_resources                 = &resources;
        return true;
    }

    bool EditorSceneCandidate::PollRenderResources(ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationResult* out) const
    {
        return m_resources && m_resources->PollSceneResourcePreparation(m_resource_preparation, out);
    }

    ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationId EditorSceneCandidate::InitialRenderResources()
    {
        ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationResult result = {};
        if (PollRenderResources(&result) && result.FrameRecorded)
        {
            m_resources->ReleaseSceneResourcePreparation(m_resource_preparation);
            m_resource_preparation = {};
            m_resources            = nullptr;
        }
        return m_resource_preparation;
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

    bool EditorSceneCandidate::PrepareRenderAdapter(const ZEngine::Core::VFS::VFSPath& source, const EditorSceneAssetResolver& assets, SceneDiagnostics* diagnostics, const EditorSceneCandidateLimits& limits)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Worker.RenderAdapter");
        constexpr auto gpu_budget = ZEngine::Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS;
        const auto*    settings   = Document().Settings();
        if (!settings || !IsSceneSourcePath(source) || limits.MaxInstances > gpu_budget || limits.MaxDrawCommands > gpu_budget)
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
        std::vector<EntityID> retired_icons = {};
        world.ForEach<MeshComponent, LightComponent>([&](EntityID entity, const MeshComponent& mesh, const LightComponent& light) {
            if (mesh.MeshUUID == *legacy_icon && light.LightType == LightComponent::Type::Directional)
            {
                retired_icons.push_back(entity);
            }
        });
        for (const EntityID entity : retired_icons)
        {
            world.RemoveComponent<MeshComponent>(entity);
        }

        // O(E + M log M), with one asset lookup per distinct mesh, not per placed
        // instance. Temporary vectors exist only during an explicit scene load.
        std::vector<MeshRequirement> requirements = {};
        bool                         valid        = true;
        world.ForEach<MeshComponent>([&](EntityID entity, const MeshComponent& mesh) {
            if (!world.HasComponent<TransformComponent>(entity) || mesh.MeshUUID.is_nil() || requirements.size() >= limits.MaxInstances)
            {
                valid = false;
                return;
            }
            requirements.push_back({mesh.MeshUUID});
        });
        if (!valid)
        {
            return Reject(diagnostics, "A mesh has no transform/asset identity, or the complete scene exceeds the instance budget.");
        }
        const auto instance_count = static_cast<uint32_t>(requirements.size());
        if (instance_count && !assets.ResolveMesh)
        {
            return Reject(diagnostics, "Scene meshes need an explicit asset resolver; no render bindings were published.");
        }
        std::sort(requirements.begin(), requirements.end(), [](const auto& a, const auto& b) { return a.UUID < b.UUID; });
        size_t unique_count = 0;
        for (size_t i = 0; i < requirements.size(); ++i)
        {
            if (unique_count && requirements[unique_count - 1].UUID == requirements[i].UUID)
            {
                ++requirements[unique_count - 1].Uses;
            }
            else
            {
                requirements[unique_count++] = requirements[i];
            }
        }
        requirements.resize(unique_count);
        m_required_meshes.m_allocator = m_instance->Arena();
        if (!m_required_meshes.try_reserve(unique_count))
        {
            return Reject(diagnostics, "Cannot retain the complete scene resource identity set.");
        }
        for (const auto& requirement : requirements)
        {
            m_required_meshes.push(requirement.UUID);
            uint32_t draws = 0;
            if (!assets.ResolveMesh(assets.Context, requirement.UUID, &draws, diagnostics) || draws == 0)
            {
                return Reject(diagnostics, "A referenced mesh is missing or has no complete drawable binding; the active scene was preserved.");
            }
            const uint64_t additional = uint64_t(draws) * requirement.Uses;
            if (additional > limits.MaxDrawCommands - m_draw_count)
            {
                return Reject(diagnostics, "The complete scene exceeds the GPU submesh draw budget; no instances were truncated.");
            }
            m_draw_count += static_cast<uint32_t>(additional);
        }
        if (!render.Instances.try_reserve(instance_count))
        {
            return Reject(diagnostics, "Cannot retain the complete scene instance list in its bounded arena.");
        }
        world.ForEach<MeshComponent>([&](EntityID entity, MeshComponent& mesh) {
            ZEngine::Rendering::Scenes::MeshInstance instance = {};
            instance.Id                                       = render.NextInstanceId++;
            instance.MeshUUID                                 = mesh.MeshUUID;
            instance.Entity                                   = entity;
            instance.Transform                                = world.GetComponent<TransformComponent>(entity)->WorldTransform;
            if (const auto* name = world.GetComponent<NameComponent>(entity))
            {
                ZEngine::Helpers::secure_strncpy(instance.Name, sizeof(instance.Name), name->Value, sizeof(instance.Name) - 1);
            }
            mesh.RenderInstanceId = instance.Id;
            render.Instances.push(instance);
        });

        render.Name         = settings->Name.data(); // NUL-terminated text owned by the retained document.
        render.UUID         = settings->UUID;
        render.AuthoredGrid = settings->Grid;
        render.SyncGridFromAuthored();
        render.Sky            = settings->Sky;
        render.SourcePath     = source;
        render.SourceReadOnly = Document().ReadOnly();
        render.MarkDirty(false);
        render.MarkInstancesDirty();
        render.MarkSkyDirty();
        m_instance->RecordMemoryUsage();
        // Settings/name and unknown component data remain owned for the entire
        // loaded-world lifetime, independent of parser scratch or source files.
        return true;
    }
} // namespace Tetragrama
