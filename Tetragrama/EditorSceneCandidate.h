#pragma once
#include <Tetragrama/EditorScene.h>
#include <ZEngine/ECS/SceneInstance.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <memory>

namespace Tetragrama
{
    /// Resolves a complete CPU mesh binding, not GPU residency. Implementations may
    /// populate the shared asset cache, but must not mutate the active scene or reset
    /// global geometry. Return the complete number of submesh draws per instance.
    struct EditorSceneAssetResolver
    {
        void* Context                                                                                                                 = nullptr;
        bool (*ResolveMesh)(void* context, const uuids::uuid& uuid, uint32_t* out_draws, ZEngine::ECS::SceneDiagnostics* diagnostics) = nullptr;
    };

    struct EditorSceneCandidateLimits
    {
        uint32_t MaxInstances    = ZEngine::Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS;
        uint32_t MaxDrawCommands = ZEngine::Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS;
    };

    /// Owns one unpublished or active loaded document in a reclaimable load slot.
    /// Keep the original owner alive until this complete candidate is ready. Loaded
    /// entities stay pure ECS: no fabricated Actor subclasses or OnCreate replay.
    /// The bound manager supports future actor creation in this same world.
    class EditorSceneCandidate final
    {
    public:
        ~EditorSceneCandidate();
        EditorSceneCandidate(const EditorSceneCandidate&)                = delete;
        EditorSceneCandidate&     operator=(const EditorSceneCandidate&) = delete;

        /// Explicit load-time work, never per-frame. Success consumes document and
        /// replaces out; failure preserves both. No active engine pointer is changed.
        [[nodiscard]] static bool Prepare(ZEngine::EngineContext& engine, ZEngine::ECS::SceneDocument&& document, const ZEngine::ECS::SceneComponentSchemaRegistry& schemas, const ZEngine::Core::VFS::VFSPath& source, const EditorSceneAssetResolver& assets, ZEngine::Helpers::Scope<EditorSceneCandidate>* out, ZEngine::ECS::SceneDiagnostics* diagnostics, const EditorSceneCandidateLimits& limits = {});
        /// Consumes an already admitted slot. No second reservation or fallback arena.
        [[nodiscard]] static bool Prepare(ZEngine::ECS::SceneStorageLease&& storage, ZEngine::ECS::SceneDocument&& document, const ZEngine::ECS::SceneComponentSchemaRegistry& schemas, const ZEngine::Core::VFS::VFSPath& source, const EditorSceneAssetResolver& assets, ZEngine::Helpers::Scope<EditorSceneCandidate>* out, ZEngine::ECS::SceneDiagnostics* diagnostics, const EditorSceneCandidateLimits& limits = {});
        /// Candidate-local editor policy on the exclusive reconstruction owner.
        /// Asset resolvers used on workers must be thread-safe. Consumes/discards the owner on binding failure;
        /// never repeats population, creates actors, or changes active bindings.
        [[nodiscard]] static bool PreparePopulated(std::unique_ptr<ZEngine::ECS::SceneInstance>&& instance, const ZEngine::Core::VFS::VFSPath& source, const EditorSceneAssetResolver& assets, ZEngine::Helpers::Scope<EditorSceneCandidate>* out, ZEngine::ECS::SceneDiagnostics* diagnostics, const EditorSceneCandidateLimits& limits = {});

        EditorScene&              Render()
        {
            return m_render;
        }
        ZEngine::ECS::Scene& World()
        {
            return m_instance->World();
        }
        ZEngine::ECS::ActorManager& Actors()
        {
            return m_instance->Actors();
        }
        const ZEngine::ECS::SceneDocument& Document() const
        {
            return m_instance->Document();
        }
        ZEngine::ECS::WorldCommands& Commands()
        {
            return m_instance->Commands();
        }
        ZEngine::ECS::WorldTick& Tick()
        {
            return m_instance->Tick();
        }
        uint32_t DrawCount() const
        {
            return m_draw_count;
        }
        bool                                                                  PrepareRenderResources(ZEngine::Rendering::RenderResourceManager& resources, ZEngine::ECS::SceneRequestId request, uint64_t generation, const ZEngine::Rendering::Scenes::SceneInitialView& view = {});
        ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationId InitialRenderResources();
        bool                                                                  PollRenderResources(ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationResult* out) const;
        size_t                                                                RequiredMeshCount() const
        {
            return m_required_meshes.size();
        }

    private:
        EditorSceneCandidate() = default;
        [[nodiscard]] bool                                                    PrepareRenderAdapter(const ZEngine::Core::VFS::VFSPath& source, const EditorSceneAssetResolver& assets, ZEngine::ECS::SceneDiagnostics* diagnostics, const EditorSceneCandidateLimits& limits);

        // Destroy the borrowing editor adapter before the engine owner releases
        // actors/ECS/document and, last, its bounded scene-storage lease.
        std::unique_ptr<ZEngine::ECS::SceneInstance>                          m_instance                  = {};
        EditorScene                                                           m_render                    = {};
        uint32_t                                                              m_draw_count                = 0;
        uint32_t                                                              m_resource_instances_copied = 0;
        ZEngine::Core::Containers::Array<uuids::uuid>                         m_required_meshes           = {};
        ZEngine::Rendering::RenderResourceManager*                            m_resources                 = nullptr;
        ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationId m_resource_preparation      = {};
    };
} // namespace Tetragrama
