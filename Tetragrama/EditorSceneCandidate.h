#pragma once
#include <Tetragrama/EditorScene.h>
#include <ZEngine/ECS/SceneInstance.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Scenes/SceneMeshBindings.h>
#include <ZEngine/Rendering/Scenes/SceneResourcePreparation.h>
#include <memory>

namespace ZEngine::Managers
{
    class CookedCpuAssetSet;
}
namespace ZEngine::ECS
{
    struct CookedSceneReconstruction;
}

namespace Tetragrama
{
    using EditorSceneAssetResolver   = ZEngine::Rendering::Scenes::SceneMeshResolver;
    using EditorSceneCandidateLimits = ZEngine::Rendering::Scenes::SceneMeshBindingLimits;

    enum class CookedCandidateState : uint8_t
    {
        Pending,
        Ready,
        Failed
    };

    /// Owns one unpublished or active loaded document in a reclaimable load slot.
    /// Keep the original owner alive until this complete candidate is ready. Loaded
    /// entities stay pure ECS: no fabricated Actor subclasses or OnCreate replay.
    /// The bound manager supports future actor creation in this same world.
    class EditorSceneCandidate final
    {
    public:
        ~EditorSceneCandidate();
        EditorSceneCandidate(const EditorSceneCandidate&)                                = delete;
        EditorSceneCandidate&                     operator=(const EditorSceneCandidate&) = delete;

        /// Explicit load-time work, never per-frame. Success consumes document and
        /// replaces out; failure preserves both. No active engine pointer is changed.
        [[nodiscard]] static bool                 Prepare(ZEngine::EngineContext& engine, ZEngine::ECS::SceneDocument&& document, const ZEngine::ECS::SceneComponentSchemaRegistry& schemas, const ZEngine::Core::VFS::VFSPath& source, const EditorSceneAssetResolver& assets, ZEngine::Helpers::Scope<EditorSceneCandidate>* out, ZEngine::ECS::SceneDiagnostics* diagnostics, const EditorSceneCandidateLimits& limits = {});
        /// Consumes an already admitted slot. No second reservation or fallback arena.
        [[nodiscard]] static bool                 Prepare(ZEngine::ECS::SceneStorageLease&& storage, ZEngine::ECS::SceneDocument&& document, const ZEngine::ECS::SceneComponentSchemaRegistry& schemas, const ZEngine::Core::VFS::VFSPath& source, const EditorSceneAssetResolver& assets, ZEngine::Helpers::Scope<EditorSceneCandidate>* out, ZEngine::ECS::SceneDiagnostics* diagnostics, const EditorSceneCandidateLimits& limits = {});
        /// Candidate-local editor policy on the exclusive reconstruction owner.
        /// Asset resolvers used on workers must be thread-safe. Consumes/discards the owner on binding failure;
        /// never repeats population, creates actors, or changes active bindings.
        [[nodiscard]] static bool                 PreparePopulated(std::unique_ptr<ZEngine::ECS::SceneInstance>&& instance, const ZEngine::Core::VFS::VFSPath& source, const EditorSceneAssetResolver& assets, ZEngine::Helpers::Scope<EditorSceneCandidate>* out, ZEngine::ECS::SceneDiagnostics* diagnostics, const EditorSceneCandidateLimits& limits = {});
        /// Resolve mesh draw counts from a verified cooked closure, without an
        /// authoring registry. The closure must remain alive for this call; the
        /// adapter retains only entity and asset identities, not CPU pointers.
        [[nodiscard]] static bool                 PreparePopulatedCooked(std::unique_ptr<ZEngine::ECS::SceneInstance>&& instance, const ZEngine::Core::VFS::VFSPath& source, const ZEngine::Managers::CookedCpuAssetSet& assets, ZEngine::Helpers::Scope<EditorSceneCandidate>* out, ZEngine::ECS::SceneDiagnostics* diagnostics, const EditorSceneCandidateLimits& limits = {});
        /// Worker-only continuation of the cooked reconstruction transaction.
        /// Pending and cache failure leave the instance and lease in the input;
        /// Ready transfers the instance into the candidate and leaves the lease
        /// with the caller for upload staging. Binding failure discards the
        /// instance, as PreparePopulated does; output is unchanged on failure.
        [[nodiscard]] static CookedCandidateState PrepareReconstructedCooked(ZEngine::ECS::CookedSceneReconstruction& reconstruction, const ZEngine::Core::VFS::VFSPath& source, ZEngine::Helpers::Scope<EditorSceneCandidate>* out, ZEngine::ECS::SceneDiagnostics* diagnostics, ZEngine::Core::VFS::VFSError* asset_error = nullptr, const EditorSceneCandidateLimits& limits = {});

        EditorScene&                              Render()
        {
            return m_render;
        }
        ZEngine::ECS::SceneInstance& Instance()
        {
            return *m_instance;
        }
        ZEngine::Rendering::Scenes::SceneResourcePreparation& ResourcePreparation()
        {
            return m_scene_resources;
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
        /// A false result is retryable unless terminal_failure is set.
        bool                                                                  PrepareRenderResources(ZEngine::Rendering::RenderResourceManager& resources, ZEngine::ECS::SceneRequestId request, uint64_t generation, const ZEngine::Rendering::Scenes::SceneInitialView& view = {}, const ZEngine::Managers::CookedCpuAssetSet* cooked = nullptr, bool* terminal_failure = nullptr);
        /// Cooked candidates retain exact resource versions for their lifetime;
        /// authoring candidates release their initial bundle after first record.
        ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationId InitialRenderResources();
        bool                                                                  PollRenderResources(ZEngine::Rendering::RenderResourceManager::SceneResourcePreparationResult* out) const;
        size_t                                                                RequiredMeshCount() const
        {
            return m_required_meshes.size();
        }
        std::span<const uuids::uuid> RequiredMeshes() const
        {
            return {m_required_meshes.data(), m_required_meshes.size()};
        }
        bool ResourceInstancesCollected() const noexcept
        {
            return m_scene_resources.InstancesCollected(m_render.Instances.size());
        }
        bool HasRenderResourcePreparation() const noexcept
        {
            return m_scene_resources.HasPreparation();
        }
        bool MatchesCookedClosure(const ZEngine::Core::Hash::Digest256& manifest, const uuids::uuid& scene) const noexcept
        {
            return m_instance && m_instance->MatchesCookedClosure(manifest, scene);
        }

    private:
        EditorSceneCandidate() = default;
        [[nodiscard]] bool                                   PrepareRenderAdapter(const ZEngine::Core::VFS::VFSPath& source, const EditorSceneAssetResolver& assets, ZEngine::ECS::SceneDiagnostics* diagnostics, const EditorSceneCandidateLimits& limits, bool cooked = false);

        // Destroy the borrowing editor adapter before the engine owner releases
        // actors/ECS/document and, last, its bounded scene-storage lease.
        std::unique_ptr<ZEngine::ECS::SceneInstance>         m_instance        = {};
        EditorScene                                          m_render          = {};
        uint32_t                                             m_draw_count      = 0;
        ZEngine::Core::Containers::Array<uuids::uuid>        m_required_meshes = {};
        ZEngine::Rendering::Scenes::SceneResourcePreparation m_scene_resources = {};
    };
} // namespace Tetragrama
