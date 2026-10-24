#include <Tetragrama/EditorSession.h>
#include <type_traits>

using namespace ZEngine::ECS;

namespace Tetragrama
{
    bool EditorSession::SetSceneSettingsHooks(const EditorSceneSettingsHooks& hooks)
    {
        if (!Allowed() || !m_scene || Active() || !hooks.ReadGrid || !hooks.ApplyGrid)
        {
            return false;
        }
        // Never replay old history through a new owner in the same scene epoch.
        if (m_scene_settings.ReadGrid)
        {
            return m_scene_settings.Context == hooks.Context && m_scene_settings.ReadGrid == hooks.ReadGrid && m_scene_settings.ApplyGrid == hooks.ApplyGrid;
        }
        m_scene_settings = hooks;
        return true;
    }

    bool EditorSession::ReadGridSettings(EditorGridView& output) const
    {
        if (!OnOwnerThread() || !m_scene || !m_scene_settings.ReadGrid)
        {
            return false;
        }
        EditorGridView view = {};
        if (!m_scene_settings.ReadGrid(m_scene_settings.Context, *m_scene, view) || view.SceneUUID.is_nil() || !ValidSceneGridSettings(view.Value))
        {
            return false;
        }
        view.Editable = view.Editable && m_editable;
        output        = view;
        return true;
    }

    bool EditorSession::BeginGridSettings()
    {
        static_assert(std::is_trivially_copyable_v<GridChange>);
        if (!Allowed() || Active())
        {
            return false;
        }
        EditorGridView view   = {};
        GridChange     change = {};
        if (!ReadGridSettings(view) || !view.Editable || !m_scene_settings.ApplyGrid || !EncodeSceneGridBinary(view.Value, change.Before) || !m_history.CanStore(sizeof(GridChange)))
        {
            return Reject("Grid settings cannot begin in the current scene or history budget.");
        }
        change.Epoch     = m_epoch;
        change.SceneUUID = view.SceneUUID;
        change.After     = change.Before;
        m_grid           = change;
        StartInteraction(Interaction::Grid);
        return true;
    }

    bool EditorSession::ValidateGrid() const
    {
        EditorGridView  view  = {};
        SceneGridBinary bytes = {};
        const bool      gizmo = m_translation.Key || m_rotation.Key;
        return m_active == Interaction::Grid && m_grid.Epoch == m_epoch && (!gizmo || (m_selection.ActiveSceneTarget() == SceneTarget::Grid && m_selection.Revision() == m_start_selection_revision)) && ReadGridSettings(view) && view.Editable && view.SceneUUID == m_grid.SceneUUID && EncodeSceneGridBinary(view.Value, bytes) && bytes == m_grid.After;
    }

    bool EditorSession::PreviewGridTranslation(const ZEngine::Core::Maths::Vec3f& origin)
    {
        SceneGridSettings candidate = {};
        if (!DecodeSceneGridBinary(m_grid.Before, candidate))
        {
            return false;
        }
        candidate.Origin = origin;
        return PreviewGridSettings(candidate);
    }

    bool EditorSession::PreviewGridRotation(const ZEngine::Core::Maths::Vec3f& axis, double angle)
    {
        SceneGridSettings candidate = {};
        if (!DecodeSceneGridBinary(m_grid.Before, candidate) || !ZEngine::Core::Maths::Geometry::RotateOrientation(candidate.Orientation, axis, angle, &candidate.Orientation))
        {
            return false;
        }
        return PreviewGridSettings(candidate);
    }

    bool EditorSession::PreviewGridSettings(const SceneGridSettings& value)
    {
        if (!Allowed() || m_active != Interaction::Grid)
        {
            return false;
        }
        if (!ValidateGrid())
        {
            CancelGrid();
            return Reject("The Grid interaction was cancelled because its scene settings changed.");
        }
        SceneGridBinary   bytes     = {};
        SceneGridSettings canonical = {};
        if (!EncodeSceneGridBinary(value, bytes) || !DecodeSceneGridBinary(bytes, canonical))
        {
            return Reject("Invalid Grid settings were rejected without changing the last preview.");
        }
        if (!(bytes == m_grid.After))
        {
            AccountGrid(m_grid.SceneUUID, m_grid.After, bytes);
            m_scene_settings.ApplyGrid(m_scene_settings.Context, *m_scene, canonical);
            m_grid.After = bytes;
            ++m_document_revision;
        }
        m_preview_changed = !(m_grid.Before == m_grid.After);
        return true;
    }

    bool EditorSession::PrepareGridSettings(const SceneGridSettings& expected, const SceneGridSettings& value, SceneGridBinary& before, SceneGridBinary& after) const
    {
        if (!Allowed())
        {
            return false;
        }
        EditorGridView  view    = {};
        SceneGridBinary current = {};
        // Entire copied intent and budget validate before retiring any unrelated preview.
        return ReadGridSettings(view) && view.Editable && EncodeSceneGridBinary(view.Value, current) && EncodeSceneGridBinary(expected, before) && before == current && EncodeSceneGridBinary(value, after) && (before == after || m_history.CanStore(sizeof(GridChange)));
    }

    bool EditorSession::CanSetGridSettings(const SceneGridSettings& expected, const SceneGridSettings& value) const
    {
        SceneGridBinary before = {}, after = {};
        return PrepareGridSettings(expected, value, before, after);
    }

    bool EditorSession::SetGridSettings(const SceneGridSettings& expected, const SceneGridSettings& value)
    {
        if (!Allowed())
        {
            return false;
        }
        SceneGridBinary before = {}, after = {};
        if (!PrepareGridSettings(expected, value, before, after))
        {
            return Reject("Queued Grid settings no longer match the editable scene or history budget.");
        }
        if (before == after)
        {
            return true;
        }
        Cancel();
        if (!BeginGridSettings())
        {
            return false;
        }
        if (!PreviewGridSettings(value) || !Commit())
        {
            Cancel();
            return false;
        }
        return true;
    }

    void EditorSession::CancelGrid()
    {
        EditorGridView    view    = {};
        SceneGridBinary   current = {};
        SceneGridSettings before  = {};
        const bool        owned   = m_grid.Epoch == m_epoch && ReadGridSettings(view) && view.SceneUUID == m_grid.SceneUUID && EncodeSceneGridBinary(view.Value, current) && current == m_grid.After && DecodeSceneGridBinary(m_grid.Before, before);
        if (owned)
        {
            if (!(m_grid.Before == m_grid.After))
            {
                AccountGrid(m_grid.SceneUUID, m_grid.After, m_grid.Before);
                m_scene_settings.ApplyGrid(m_scene_settings.Context, *m_scene, before);
                ++m_document_revision;
            }
        }
        else
        {
            // The scene owner/foreign value is not ours to overwrite on cancel.
            m_history.Invalidate();
            m_saved_content_known = false;
        }
        m_active          = Interaction::None;
        m_preview_changed = false;
        m_grid            = {};
        m_translation     = {};
        m_rotation        = {};
    }

    EditorOperation EditorSession::GridOperation()
    {
        return {
            .Validate = ValidateGridOperation,
            .Apply    = [](void* context, std::span<const std::byte> payload) { ApplyGridOperation(context, payload, false); },
            .Revert   = [](void* context, std::span<const std::byte> payload) { ApplyGridOperation(context, payload, true); },
            .Destroy  = [](std::span<std::byte>) {},
            .Name     = [](std::span<const std::byte>) -> cstring { return "Grid settings"; },
        };
    }

    bool EditorSession::ValidateGridOperation(void* context, std::span<const std::byte> payload, bool undo)
    {
        auto& session = *static_cast<EditorSession*>(context);
        if (!session.Allowed() || payload.size() != sizeof(GridChange))
        {
            return false;
        }
        const auto&       change  = *reinterpret_cast<const GridChange*>(payload.data());
        EditorGridView    view    = {};
        SceneGridBinary   current = {};
        SceneGridSettings before = {}, after = {};
        if (change.Epoch != session.m_epoch || !DecodeSceneGridBinary(change.Before, before) || !DecodeSceneGridBinary(change.After, after) || !session.ReadGridSettings(view) || !view.Editable || view.SceneUUID != change.SceneUUID || !EncodeSceneGridBinary(view.Value, current) || !(current == (undo ? change.After : change.Before)))
        {
            return session.Reject("Grid history no longer matches the scene; no setting was changed.");
        }
        return true;
    }

    void EditorSession::ApplyGridOperation(void* context, std::span<const std::byte> payload, bool undo)
    {
        auto&             session = *static_cast<EditorSession*>(context);
        const auto&       change  = *reinterpret_cast<const GridChange*>(payload.data());
        SceneGridSettings value   = {};
        const bool        decoded = DecodeSceneGridBinary(undo ? change.Before : change.After, value);
        ZENGINE_VALIDATE_ASSERT(decoded, "Validated Grid history must decode")
        if (!decoded)
        {
            return;
        }
        session.AccountGrid(change.SceneUUID, undo ? change.After : change.Before, undo ? change.Before : change.After);
        session.m_scene_settings.ApplyGrid(session.m_scene_settings.Context, *session.m_scene, value);
        ++session.m_document_revision;
    }
} // namespace Tetragrama
