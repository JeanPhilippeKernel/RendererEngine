#pragma once
#include <ZEngine/Rendering/Scenes/CookedRuntimeScene.h>

namespace ZEngine
{
    struct EngineContext;
}
namespace ZEngine::Applications
{
    struct GameApplication;
    struct RenderFrameState;

    /// Main-thread publication owner for renderer-only cooked scenes. The host
    /// must call ShutdownAfterReadersJoined before engine services are destroyed.
    class CookedRuntimeSceneHost final
    {
    public:
        CookedRuntimeSceneHost() = default;
        ~CookedRuntimeSceneHost();
        CookedRuntimeSceneHost(const CookedRuntimeSceneHost&)                                         = delete;
        CookedRuntimeSceneHost&                              operator=(const CookedRuntimeSceneHost&) = delete;

        /// All validation and frame reservation precedes the generation-guarded
        /// no-fail switch. Failure preserves the candidate and active bindings.
        [[nodiscard]] bool                                   Publish(EngineContext&, GameApplication&, std::unique_ptr<Rendering::Scenes::CookedRuntimeScene>&, ECS::SceneRequestId, uint64_t expected_generation, const uuids::uuid& camera, RenderFrameState, ECS::SceneDiagnostics*);
        /// Call after render/pool workers have joined and before RRM shutdown.
        [[nodiscard]] bool                                   ShutdownAfterReadersJoined(EngineContext&, GameApplication&) noexcept;
        [[nodiscard]] Rendering::Scenes::CookedRuntimeScene* Active() const noexcept
        {
            return m_active.get();
        }

    private:
        std::unique_ptr<Rendering::Scenes::CookedRuntimeScene> m_active            = {};
        ECS::Scene*                                            m_original_world    = nullptr;
        ECS::ActorManager*                                     m_original_actors   = nullptr;
        ECS::WorldCommands*                                    m_original_commands = nullptr;
        ECS::WorldTick*                                        m_original_tick     = nullptr;
        Rendering::Scenes::RenderScene*                        m_original_render   = nullptr;
    };
} // namespace ZEngine::Applications
