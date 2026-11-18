#include <Tetragrama/EditorPlayCoordinator.h>
#include <limits>

namespace Tetragrama
{
    using namespace ZEngine::ECS;

    bool EditorPlayCoordinator::OnOwnerThread() const noexcept
    {
        return m_owner == std::this_thread::get_id();
    }

    bool EditorPlayCoordinator::BeginTransition(Mode next, SceneRequestId* output) noexcept
    {
        if (!OnOwnerThread() || !output || m_active || !m_requests.Drained() || m_generation == std::numeric_limits<uint64_t>::max())
            return false;
        SceneRequestId request = {};
        if (!m_requests.Admit(m_generation, &request))
            return false;
        // Drained admission has no outstanding asynchronous owner, so the
        // freshly admitted request can always enter preparation on this thread.
        if (!m_requests.TryStart(request))
        {
            (void) m_requests.Fail(request, SceneLoadStage::Admission, SceneLoadError::Unavailable);
            (void) m_requests.Acknowledge(request);
            return false;
        }
        m_active = request;
        m_mode   = next;
        *output  = request;
        return true;
    }

    bool EditorPlayCoordinator::BeginEnter(SceneRequestId* output) noexcept
    {
        if (!OnOwnerThread() || m_mode != Mode::Edit || !BeginTransition(Mode::Starting, output))
            return false;
        m_last_enter = *output;
        return true;
    }

    SceneCancelResult EditorPlayCoordinator::CancelEnter(SceneRequestId request) noexcept
    {
        if (!OnOwnerThread() || !request || request != m_last_enter)
            return SceneCancelResult::NotFound;
        const auto result = m_requests.Cancel(request);
        if (result == SceneCancelResult::Cancelled && m_mode == Mode::Starting && request == m_active)
        {
            m_active = {};
            m_mode   = Mode::Edit;
        }
        return result;
    }

    bool EditorPlayCoordinator::FailTransition(Mode expected, Mode next, SceneRequestId request, SceneLoadStage stage, SceneLoadError error) noexcept
    {
        if (!OnOwnerThread() || m_mode != expected || request != m_active || !m_requests.Fail(request, stage, error))
            return false;
        m_active = {};
        m_mode   = next;
        return true;
    }

    bool EditorPlayCoordinator::FailEnter(SceneRequestId request, SceneLoadStage stage, SceneLoadError error) noexcept
    {
        return FailTransition(Mode::Starting, Mode::Edit, request, stage, error);
    }

    bool EditorPlayCoordinator::BeginCommit(Mode expected, SceneRequestId request, uint64_t generation) noexcept
    {
        return OnOwnerThread() && m_mode == expected && request == m_active && generation == m_generation && m_requests.BeginCommit(request, generation);
    }

    bool EditorPlayCoordinator::FinishCommit(Mode expected, Mode next, SceneRequestId request) noexcept
    {
        if (!OnOwnerThread() || m_mode != expected || request != m_active || m_generation == std::numeric_limits<uint64_t>::max() || !m_requests.Commit(request, m_generation + 1))
            return false;
        ++m_generation;
        m_active = {};
        m_mode   = next;
        return true;
    }

    bool EditorPlayCoordinator::BeginEnterCommit(SceneRequestId request, uint64_t generation) noexcept
    {
        return BeginCommit(Mode::Starting, request, generation);
    }

    bool EditorPlayCoordinator::FinishEnterCommit(SceneRequestId request) noexcept
    {
        return FinishCommit(Mode::Starting, Mode::Play, request);
    }

    bool EditorPlayCoordinator::Pause() noexcept
    {
        if (!OnOwnerThread() || m_mode != Mode::Play)
            return false;
        m_mode = Mode::Paused;
        return true;
    }

    bool EditorPlayCoordinator::Resume() noexcept
    {
        if (!OnOwnerThread() || m_mode != Mode::Paused)
            return false;
        m_mode = Mode::Play;
        return true;
    }

    bool EditorPlayCoordinator::BeginStep() noexcept
    {
        if (!OnOwnerThread() || m_mode != Mode::Paused)
            return false;
        m_mode = Mode::Stepping;
        return true;
    }

    bool EditorPlayCoordinator::FinishStep() noexcept
    {
        if (!OnOwnerThread() || m_mode != Mode::Stepping)
            return false;
        m_mode = Mode::Paused;
        return true;
    }

    bool EditorPlayCoordinator::BeginStop(SceneRequestId* output) noexcept
    {
        return OnOwnerThread() && (m_mode == Mode::Play || m_mode == Mode::Paused || m_mode == Mode::ReturnError) && BeginTransition(Mode::Stopping, output);
    }

    bool EditorPlayCoordinator::FailStop(SceneRequestId request, SceneLoadStage stage, SceneLoadError error) noexcept
    {
        return FailTransition(Mode::Stopping, Mode::ReturnError, request, stage, error);
    }

    bool EditorPlayCoordinator::BeginStopCommit(SceneRequestId request, uint64_t generation) noexcept
    {
        return BeginCommit(Mode::Stopping, request, generation);
    }

    bool EditorPlayCoordinator::FinishStopCommit(SceneRequestId request) noexcept
    {
        return FinishCommit(Mode::Stopping, Mode::Edit, request);
    }

    bool EditorPlayCoordinator::Poll(SceneRequestId request, SceneLoadResult* output) const noexcept
    {
        return m_requests.Poll(request, output);
    }

    bool EditorPlayCoordinator::Acknowledge(SceneRequestId request) noexcept
    {
        return m_requests.Acknowledge(request);
    }

    bool EditorPlayCoordinator::FinishCleanup(SceneRequestId request) noexcept
    {
        return m_requests.FinishCleanup(request);
    }

    EditorPlayCoordinator::Mode EditorPlayCoordinator::CurrentMode() const noexcept
    {
        return m_mode;
    }

    uint64_t EditorPlayCoordinator::Generation() const noexcept
    {
        return m_generation;
    }
} // namespace Tetragrama
