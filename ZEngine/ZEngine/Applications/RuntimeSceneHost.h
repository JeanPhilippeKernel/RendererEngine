#pragma once
#include <ZEngine/Rendering/Scenes/RuntimeScene.h>

namespace ZEngine
{
    struct EngineContext;
}
namespace ZEngine::Applications
{
    struct GameApplication;
    struct RenderFrameState;

    /// @brief Owns publication of renderer-only runtime scenes on the main thread.
    /// @details The caller must invoke ShutdownAfterReadersJoined before engine
    /// services are destroyed. The currently available scene preparation path
    /// consumes cooked assets, but publication does not inspect their format.
    class RuntimeSceneHost final
    {
    public:
        RuntimeSceneHost() = default;
        ~RuntimeSceneHost();
        RuntimeSceneHost(const RuntimeSceneHost&)                                         = delete;
        RuntimeSceneHost&                              operator=(const RuntimeSceneHost&) = delete;

        /// @brief Publish a prepared scene for the expected generation.
        /// @details Validation and frame reservation precede the guarded no-fail
        /// switch. Failure preserves the candidate and active bindings.
        [[nodiscard]] bool                             Publish(EngineContext&, GameApplication&, std::unique_ptr<Rendering::Scenes::RuntimeScene>&, ECS::SceneRequestId, uint64_t expected_generation, const uuids::uuid& camera, RenderFrameState, ECS::SceneDiagnostics*);
        /// @brief Restore the previous bindings and retire the active scene.
        /// @details Call after render and pool workers join, before RRM shutdown.
        [[nodiscard]] bool                             ShutdownAfterReadersJoined(EngineContext&, GameApplication&) noexcept;
        /// @brief Return the active scene, or nullptr before publication.
        [[nodiscard]] Rendering::Scenes::RuntimeScene* Active() const noexcept;

    private:
        std::unique_ptr<Rendering::Scenes::RuntimeScene> m_active            = {};
        ECS::Scene*                                      m_original_world    = nullptr;
        ECS::ActorManager*                               m_original_actors   = nullptr;
        ECS::WorldCommands*                              m_original_commands = nullptr;
        ECS::WorldTick*                                  m_original_tick     = nullptr;
        Rendering::Scenes::RenderScene*                  m_original_render   = nullptr;
    };
} // namespace ZEngine::Applications
