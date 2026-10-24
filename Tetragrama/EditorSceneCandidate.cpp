#include <Tetragrama/EditorSceneCandidate.h>
#include <Tetragrama/EditorSceneIO.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <algorithm>
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
    } // namespace

    EditorSceneCandidate::~EditorSceneCandidate()
    {
        // Never release global mesh geometry here: the outgoing world, a retained
        // render snapshot or another candidate can still reference the same asset.
        if (m_actors_live)
        {
            m_actors.Shutdown();
        }
        if (m_world_live)
        {
            m_world.Shutdown();
        }
    }

    bool EditorSceneCandidate::Prepare(ZEngine::EngineContext& engine, SceneDocument&& document, const SceneComponentSchemaRegistry& schemas, const ZEngine::Core::VFS::VFSPath& source, const EditorSceneAssetResolver& assets, ZEngine::Helpers::Scope<EditorSceneCandidate>* out, SceneDiagnostics* diagnostics, const EditorSceneCandidateLimits& limits)
    {
        constexpr auto gpu_budget = ZEngine::Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS;
        const auto*    settings   = document.Settings();
        if (!out || !settings || !schemas.IsInitialized() || !IsSceneSourcePath(source) || limits.MaxInstances > gpu_budget || limits.MaxDrawCommands > gpu_budget)
        {
            return Reject(diagnostics, "Invalid scene candidate destination, source, schemas or render limits.");
        }

        auto  candidate = ZEngine::Helpers::Scope<EditorSceneCandidate>(new EditorSceneCandidate{});
        auto& render    = candidate->m_render;
        if (!render.InitializeDeserialized(engine))
        {
            return Reject(diagnostics, "No bounded scene-load slot is available; the active scene was preserved.");
        }
        candidate->m_world.Initialize(render.LocalArena);
        candidate->m_world_live = true;
        candidate->m_actors.Initialize(render.LocalArena, candidate->m_world);
        candidate->m_actors_live = true;
        if (!document.PopulateCandidate(candidate->m_world, schemas, diagnostics))
        {
            return false;
        }

        // O(E + M log M), with one asset lookup per distinct mesh, not per placed
        // instance. Temporary vectors exist only during an explicit scene load.
        std::vector<MeshRequirement> requirements = {};
        bool                         valid        = true;
        candidate->m_world.ForEach<MeshComponent>([&](EntityID entity, const MeshComponent& mesh) {
            if (!candidate->m_world.HasComponent<TransformComponent>(entity) || mesh.MeshUUID.is_nil() || requirements.size() >= limits.MaxInstances)
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
        for (const auto& requirement : requirements)
        {
            uint32_t draws = 0;
            if (!assets.ResolveMesh(assets.Context, requirement.UUID, &draws, diagnostics) || draws == 0)
            {
                return Reject(diagnostics, "A referenced mesh is missing or has no complete drawable binding; the active scene was preserved.");
            }
            const uint64_t additional = uint64_t(draws) * requirement.Uses;
            if (additional > limits.MaxDrawCommands - candidate->m_draw_count)
            {
                return Reject(diagnostics, "The complete scene exceeds the GPU submesh draw budget; no instances were truncated.");
            }
            candidate->m_draw_count += static_cast<uint32_t>(additional);
        }
        if (!render.Instances.try_reserve(instance_count))
        {
            return Reject(diagnostics, "Cannot retain the complete scene instance list in its bounded arena.");
        }
        candidate->m_world.ForEach<MeshComponent>([&](EntityID entity, MeshComponent& mesh) {
            ZEngine::Rendering::Scenes::MeshInstance instance = {};
            instance.Id                                       = render.NextInstanceId++;
            instance.MeshUUID                                 = mesh.MeshUUID;
            instance.Transform                                = candidate->m_world.GetComponent<TransformComponent>(entity)->WorldTransform;
            if (const auto* name = candidate->m_world.GetComponent<NameComponent>(entity))
            {
                ZEngine::Helpers::secure_strncpy(instance.Name, sizeof(instance.Name), name->Value, sizeof(instance.Name) - 1);
            }
            mesh.RenderInstanceId = instance.Id;
            render.Instances.push(instance);
        });

        render.Name         = settings->Name.c_str();
        render.UUID         = settings->UUID;
        render.AuthoredGrid = settings->Grid;
        render.SyncGridFromAuthored();
        render.Sky            = settings->Sky;
        render.SourcePath     = source;
        render.SourceReadOnly = document.ReadOnly();
        render.MarkDirty(false);
        render.MarkInstancesDirty();
        render.MarkSkyDirty();
        // Settings/name and unknown component data remain owned for the entire
        // loaded-world lifetime, independent of parser scratch or source files.
        candidate->m_document = std::move(document);
        *out                  = std::move(candidate);
        return true;
    }
} // namespace Tetragrama
