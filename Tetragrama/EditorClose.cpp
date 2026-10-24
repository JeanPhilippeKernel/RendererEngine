#include <Tetragrama/Editor.h>
#include <ZEngine/Engine.h>

namespace Tetragrama
{
    bool Editor::OnCloseRequested()
    {
        if (Session.Ready() && !Session.OnOwnerThread())
        {
            return false;
        }
        if (m_closing || !CurrentScene)
        {
            return true;
        }
        // One destructive user intent at a time. Native repeated close requests
        // must not replace an existing decision or steal another lifecycle prompt.
        if (m_editor_close_pending || m_new_scene_pending || SceneOpenPending() || SceneChangePromptState().Visible())
        {
            return false;
        }
        auto* owner = static_cast<EditorScene*>(CurrentScene);
        if (!Session.Ready() || owner->Session != &Session)
        {
            return !owner->IsDirty();
        }
        if (!m_editor_close_owner.Capture(*this))
        {
            return false;
        }
        m_editor_close_pending = true;
        MeshDrop.Cancel();
        return false;
    }

    bool Editor::ProcessCloseRequest(ZEngine::EngineContext& engine)
    {
        if (!m_editor_close_pending)
        {
            return true;
        }
        if (!Session.InMutationPhase())
        {
            return false;
        }
        const auto finish = [&] {
            m_editor_close_pending = false;
            m_scene_change.Finish(SceneChangeKind::CloseEditor, m_editor_close_owner.Epoch);
            m_editor_close_owner.Reset();
        };
        if (m_closing || !m_editor_close_owner.Matches(*this) || !engine.Scene || engine.Scene != Session.Scene())
        {
            finish();
            return false;
        }
        if (Session.PendingRequestCount() || !ProcessSceneSettings(engine))
        {
            return false;
        }
        // Restore transient previews and drain earlier edit intent before consent;
        // teardown itself still runs once, through Engine::Run -> OnClosing.
        Session.Cancel();
        const auto gate = GateSceneChange(SceneChangeKind::CloseEditor);
        if (gate == SceneChangeGate::Waiting)
        {
            return true;
        }
        finish();
        if (gate == SceneChangeGate::Ready)
        {
            engine.CloseRequested.value.store(true, std::memory_order_release);
        }
        return true;
    }
} // namespace Tetragrama
