#include <Tetragrama/EditorSceneChange.h>

namespace Tetragrama
{
    void EditorSceneChange::Reset()
    {
        m_prompt = {};
        m_choice = SceneChangeChoice::None;
        m_phase  = Phase::Idle;
        // Do not reuse a serial when a delayed UI response outlives this request.
    }

    bool EditorSceneChange::Matches(SceneChangeKind kind, uint64_t epoch) const
    {
        return m_phase != Phase::Idle && m_prompt.Kind == kind && m_prompt.Epoch == epoch;
    }

    void EditorSceneChange::Refresh(uint64_t revision, uint64_t external_revision, bool failed)
    {
        m_prompt.Revision         = revision;
        m_prompt.ExternalRevision = external_revision;
        m_prompt.Serial           = m_next_serial++;
        if (!m_next_serial)
        {
            ++m_next_serial;
        }
        m_prompt.Saving     = false;
        m_prompt.SaveFailed = failed;
        m_phase             = Phase::Confirm;
        m_choice            = SceneChangeChoice::None;
    }

    SceneChangeGate EditorSceneChange::Evaluate(SceneChangeKind kind, uint64_t epoch, uint64_t revision, bool dirty, uint64_t external_revision)
    {
        if ((kind != SceneChangeKind::NewScene && kind != SceneChangeKind::OpenScene && kind != SceneChangeKind::CloseEditor) || !epoch)
        {
            Reset();
            return SceneChangeGate::Cancelled;
        }
        if (!Matches(kind, epoch))
        {
            Reset();
            if (!dirty)
            {
                return SceneChangeGate::Ready;
            }
            m_prompt.Kind  = kind;
            m_prompt.Epoch = epoch;
            Refresh(revision, external_revision);
            return SceneChangeGate::Waiting;
        }
        if (m_phase == Phase::Cancelled || m_choice == SceneChangeChoice::Cancel)
        {
            Reset();
            return SceneChangeGate::Cancelled;
        }
        if (m_phase == Phase::Saving)
        {
            return SceneChangeGate::Waiting;
        }
        if (!dirty)
        {
            Reset();
            return SceneChangeGate::Ready;
        }
        if (m_prompt.Revision != revision || m_prompt.ExternalRevision != external_revision)
        {
            Refresh(revision, external_revision); // approval must never follow a newer authored edit
        }
        if (m_phase == Phase::Discarded)
        {
            return SceneChangeGate::Ready;
        }
        const auto choice = m_choice;
        m_choice          = SceneChangeChoice::None;
        if (choice == SceneChangeChoice::Discard)
        {
            m_phase = Phase::Discarded;
            return SceneChangeGate::Ready;
        }
        if (choice == SceneChangeChoice::Save)
        {
            m_phase         = Phase::Saving;
            m_prompt.Saving = true;
            return SceneChangeGate::Save;
        }
        return SceneChangeGate::Waiting;
    }

    bool EditorSceneChange::Choose(SceneChangeChoice choice, uint64_t prompt_serial)
    {
        if (!prompt_serial || prompt_serial != m_prompt.Serial || m_choice != SceneChangeChoice::None || (m_phase != Phase::Confirm && m_phase != Phase::Saving) || (choice != SceneChangeChoice::Save && choice != SceneChangeChoice::Discard && choice != SceneChangeChoice::Cancel) || (m_phase == Phase::Saving && choice != SceneChangeChoice::Cancel))
        {
            return false;
        }
        m_choice = choice;
        return true;
    }

    SceneChangePrompt EditorSceneChange::Prompt() const
    {
        return m_phase == Phase::Confirm || m_phase == Phase::Saving ? m_prompt : SceneChangePrompt{};
    }

    bool EditorSceneChange::MayDiscard(SceneChangeKind kind, uint64_t epoch, uint64_t revision, uint64_t external_revision) const
    {
        return m_phase == Phase::Discarded && Matches(kind, epoch) && revision == m_prompt.Revision && external_revision == m_prompt.ExternalRevision;
    }

    void EditorSceneChange::SaveFinished(uint64_t epoch, bool succeeded)
    {
        if (m_phase != Phase::Saving || m_prompt.Epoch != epoch || m_choice == SceneChangeChoice::Cancel)
        {
            return;
        }
        if (succeeded)
        {
            Reset(); // the caller must still recheck the actual dirty state
        }
        else
        {
            Refresh(m_prompt.Revision, m_prompt.ExternalRevision, true);
        }
    }

    void EditorSceneChange::SaveCancelled(uint64_t epoch)
    {
        if (m_phase == Phase::Saving && m_prompt.Epoch == epoch)
        {
            m_phase = Phase::Cancelled;
        }
    }

    void EditorSceneChange::Finish(SceneChangeKind kind, uint64_t epoch)
    {
        if (Matches(kind, epoch))
        {
            Reset();
        }
    }
} // namespace Tetragrama
