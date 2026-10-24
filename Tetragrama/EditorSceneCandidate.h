#pragma once
#include <Tetragrama/EditorScene.h>
#include <ZEngine/ECS/SceneDocument.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
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

        EditorScene&              Render()
        {
            return m_render;
        }
        ZEngine::ECS::Scene& World()
        {
            return m_world;
        }
        ZEngine::ECS::ActorManager& Actors()
        {
            return m_actors;
        }
        const ZEngine::ECS::SceneDocument& Document() const
        {
            return m_document;
        }
        uint32_t DrawCount() const
        {
            return m_draw_count;
        }

    private:
        EditorSceneCandidate()                    = default;

        // Render must release its arena last, after every ECS/actor/document owner.
        EditorScene                 m_render      = {};
        ZEngine::ECS::Scene         m_world       = {};
        ZEngine::ECS::ActorManager  m_actors      = {};
        ZEngine::ECS::SceneDocument m_document    = {};
        uint32_t                    m_draw_count  = 0;
        bool                        m_world_live  = false;
        bool                        m_actors_live = false;
    };
} // namespace Tetragrama
