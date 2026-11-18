#pragma once
#include <ZEngine/ECS/SceneLoadCoordinator.h>
#include <cstdint>
#include <thread>

namespace Tetragrama
{
    /// @brief Main-thread transition ledger for retained-authoring Play.
    /// @details This ledger owns no worlds and performs no simulation or binding
    /// switches. The host must prepare and guard those operations before commit,
    /// and must drain worker/GPU owners before reporting cleanup complete.
    class EditorPlayCoordinator final
    {
    public:
        enum class Mode : uint8_t
        {
            Edit,
            Starting,
            Play,
            Paused,
            Stepping,
            Stopping,
            ReturnError
        };

        EditorPlayCoordinator()                                                               = default;
        EditorPlayCoordinator(const EditorPlayCoordinator&)                                   = delete;
        EditorPlayCoordinator&                        operator=(const EditorPlayCoordinator&) = delete;

        /// @brief Admit one Enter request; outstanding cleanup blocks admission.
        [[nodiscard]] bool                            BeginEnter(ZEngine::ECS::SceneRequestId* output) noexcept;
        /// @brief Cancel Enter before commit; cleanup remains independently owned.
        [[nodiscard]] ZEngine::ECS::SceneCancelResult CancelEnter(ZEngine::ECS::SceneRequestId request) noexcept;
        /// @brief Fail Enter before commit, retaining the terminal result.
        [[nodiscard]] bool                            FailEnter(ZEngine::ECS::SceneRequestId request, ZEngine::ECS::SceneLoadStage stage, ZEngine::ECS::SceneLoadError error) noexcept;
        /// @brief Seal a prepared Enter against cancellation and reentrant transitions.
        [[nodiscard]] bool                            BeginEnterCommit(ZEngine::ECS::SceneRequestId request, uint64_t expected_generation) noexcept;
        /// @brief Record a successfully published Enter after the host switches bindings.
        [[nodiscard]] bool                            FinishEnterCommit(ZEngine::ECS::SceneRequestId request) noexcept;
        /// @brief Pause simulation without restoring authoring.
        [[nodiscard]] bool                            Pause() noexcept;
        /// @brief Resume continuous simulation from Paused.
        [[nodiscard]] bool                            Resume() noexcept;
        /// @brief Reserve a single paused step; the host executes it separately.
        [[nodiscard]] bool                            BeginStep() noexcept;
        /// @brief Finish a reserved step and return to Paused.
        [[nodiscard]] bool                            FinishStep() noexcept;
        /// @brief Admit Stop from Play, Paused, or a failed-return recovery state.
        [[nodiscard]] bool                            BeginStop(ZEngine::ECS::SceneRequestId* output) noexcept;
        /// @brief Record a failed return; retry remains possible after cleanup.
        [[nodiscard]] bool                            FailStop(ZEngine::ECS::SceneRequestId request, ZEngine::ECS::SceneLoadStage stage, ZEngine::ECS::SceneLoadError error) noexcept;
        /// @brief Seal a prepared return against reentrant transitions.
        [[nodiscard]] bool                            BeginStopCommit(ZEngine::ECS::SceneRequestId request, uint64_t expected_generation) noexcept;
        /// @brief Record the successful switch back to the retained authoring world.
        [[nodiscard]] bool                            FinishStopCommit(ZEngine::ECS::SceneRequestId request) noexcept;
        /// @brief Read a retained transition outcome on the owner thread.
        [[nodiscard]] bool                            Poll(ZEngine::ECS::SceneRequestId request, ZEngine::ECS::SceneLoadResult* output) const noexcept;
        /// @brief Release a terminal outcome; this does not imply cleanup is done.
        [[nodiscard]] bool                            Acknowledge(ZEngine::ECS::SceneRequestId request) noexcept;
        /// @brief Release request cleanup ownership after transfer or retirement.
        /// @details A successful Enter may release its preparation record after
        /// ownership transfers to the active host. Cancelled work and retired
        /// worlds retain it until their external CPU/GPU users have drained.
        [[nodiscard]] bool                            FinishCleanup(ZEngine::ECS::SceneRequestId request) noexcept;
        /// @brief Return the owner-thread mode without enabling simulation.
        [[nodiscard]] Mode                            CurrentMode() const noexcept;
        /// @brief Return the owner-thread activation generation.
        [[nodiscard]] uint64_t                        Generation() const noexcept;

    private:
        [[nodiscard]] bool                 OnOwnerThread() const noexcept;
        [[nodiscard]] bool                 BeginTransition(Mode next, ZEngine::ECS::SceneRequestId* output) noexcept;
        [[nodiscard]] bool                 BeginCommit(Mode expected, ZEngine::ECS::SceneRequestId request, uint64_t generation) noexcept;
        [[nodiscard]] bool                 FinishCommit(Mode expected, Mode next, ZEngine::ECS::SceneRequestId request) noexcept;
        [[nodiscard]] bool                 FailTransition(Mode expected, Mode next, ZEngine::ECS::SceneRequestId request, ZEngine::ECS::SceneLoadStage stage, ZEngine::ECS::SceneLoadError error) noexcept;
        const std::thread::id              m_owner      = std::this_thread::get_id();
        ZEngine::ECS::SceneLoadCoordinator m_requests   = {};
        ZEngine::ECS::SceneRequestId       m_active     = {};
        ZEngine::ECS::SceneRequestId       m_last_enter = {};
        Mode                               m_mode       = Mode::Edit;
        uint64_t                           m_generation = 1;
    };
} // namespace Tetragrama
