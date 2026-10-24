#include <Tetragrama/Editor.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/Engine.h>

namespace Tetragrama
{
    namespace
    {
        bool ValidPrimary(EditorSession& session, const ZEngine::Rendering::Scenes::SkyConfig& before, const ZEngine::Rendering::Scenes::SkyConfig& after, const EntityTarget& target)
        {
            if (after.PrimaryCelestialLight.is_nil() || after.PrimaryCelestialLight == before.PrimaryCelestialLight)
            {
                return true; // preserve diagnostics for an existing missing source
            }
            const auto entity = session.Resolve(target);
            if (!entity.IsValid() || entity != target.CachedEntity || target.UUID != after.PrimaryCelestialLight)
            {
                return false;
            }
            const auto* light = session.Scene()->GetComponent<ZEngine::ECS::Components::LightComponent>(entity);
            return light && light->LightType == ZEngine::ECS::Components::LightComponent::Type::Directional && session.Scene()->HasComponent<ZEngine::ECS::Components::TransformComponent>(entity);
        }
    } // namespace

    bool Editor::SceneSettingsMatch() const
    {
        if (!Session.OnOwnerThread())
        {
            return false;
        }
        const auto* owner = static_cast<const EditorScene*>(CurrentScene);
        return owner && owner->Session == &Session && m_scene_settings.Epoch == Session.SceneEpoch() && m_scene_settings.Scene == owner->UUID;
    }

    void Editor::DiscardSceneSettings()
    {
        if (Session.Ready() && !Session.OnOwnerThread())
        {
            return;
        }
        m_scene_settings.Sky = m_scene_settings.Grid = false;
    }

    void Editor::PrepareSceneSettingsRequest()
    {
        if (!SceneSettingsMatch())
        {
            DiscardSceneSettings();
            m_scene_settings.Epoch = Session.SceneEpoch();
            m_scene_settings.Scene = static_cast<const EditorScene*>(CurrentScene)->UUID;
        }
    }

    ZEngine::Rendering::Scenes::SkyConfig Editor::SceneSkyForUI() const
    {
        if (!Session.OnOwnerThread() || !CurrentScene)
        {
            return {};
        }
        const auto& owner = *static_cast<const EditorScene*>(CurrentScene);
        return SceneSettingsMatch() && m_scene_settings.Sky && owner.Sky == m_scene_settings.SkyBefore ? m_scene_settings.SkyAfter : owner.Sky;
    }

    ZEngine::ECS::SceneGridSettings Editor::SceneGridForUI() const
    {
        if (!Session.OnOwnerThread() || !CurrentScene)
        {
            return {};
        }
        const auto& owner = *static_cast<const EditorScene*>(CurrentScene);
        return SceneSettingsMatch() && m_scene_settings.Grid && ZEngine::ECS::EqualSceneGridSettings(owner.AuthoredGrid, m_scene_settings.GridBefore) ? m_scene_settings.GridAfter : owner.AuthoredGrid;
    }

    bool Editor::RequestSceneGridSelection()
    {
        if (!Session.OnOwnerThread())
        {
            return false;
        }
        const auto* owner = static_cast<const EditorScene*>(CurrentScene);
        if (m_closing || m_editor_close_pending || m_new_scene_pending || SceneOpenPending() || SceneChangePromptState().Visible() || !owner || owner->Session != &Session)
        {
            return false;
        }
        EditorGridView view = {};
        // Use the bound scene service, not merely a UI's retained owner pointer.
        // A read-only Grid is still a valid editor-local selection target.
        return Session.ReadGridSettings(view) && view.SceneUUID == owner->UUID && Session.RequestSceneSelection(SceneTarget::Grid);
    }

    bool Editor::RequestSceneSkySettings(const ZEngine::Rendering::Scenes::SkyConfig& value)
    {
        if (!Session.OnOwnerThread())
        {
            return false;
        }
        const auto* owner = static_cast<const EditorScene*>(CurrentScene);
        if (m_closing || m_editor_close_pending || m_new_scene_pending || SceneOpenPending() || SceneChangePromptState().Visible() || !owner || owner->Session != &Session || owner->SourceReadOnly || !Session.Editable() || !value.IsValid())
        {
            return false;
        }
        EntityTarget target = {};
        if (!value.PrimaryCelestialLight.is_nil() && value.PrimaryCelestialLight != owner->Sky.PrimaryCelestialLight)
        {
            // Retain the originally selected generation while coalescing another
            // field. Recreating an entity with the same UUID cannot retarget intent.
            target = SceneSettingsMatch() && m_scene_settings.Sky && m_scene_settings.SkyAfter.PrimaryCelestialLight == value.PrimaryCelestialLight ? m_scene_settings.PrimaryTarget : Session.TargetFor(Session.Resolve(value.PrimaryCelestialLight));
        }
        if (!ValidPrimary(Session, owner->Sky, value, target))
        {
            return false;
        }
        PrepareSceneSettingsRequest();
        if (!m_scene_settings.Sky || owner->Sky != m_scene_settings.SkyBefore)
        {
            m_scene_settings.SkyBefore = owner->Sky;
        }
        m_scene_settings.SkyAfter      = value;
        m_scene_settings.PrimaryTarget = target;
        m_scene_settings.Sky           = value != m_scene_settings.SkyBefore;
        return true;
    }

    bool Editor::RequestSceneGridSettings(const ZEngine::ECS::SceneGridSettings& value)
    {
        if (!Session.OnOwnerThread())
        {
            return false;
        }
        const auto* owner = static_cast<const EditorScene*>(CurrentScene);
        if (m_closing || m_editor_close_pending || m_new_scene_pending || SceneOpenPending() || SceneChangePromptState().Visible() || !owner || owner->Session != &Session || owner->SourceReadOnly || !Session.Editable())
        {
            return false;
        }
        auto canonical = value;
        if (!ZEngine::ECS::CanonicalizeSceneGridSettings(canonical))
        {
            return false;
        }
        PrepareSceneSettingsRequest();
        if (!m_scene_settings.Grid || !ZEngine::ECS::EqualSceneGridSettings(owner->AuthoredGrid, m_scene_settings.GridBefore))
        {
            m_scene_settings.GridBefore = owner->AuthoredGrid;
        }
        m_scene_settings.GridAfter = canonical;
        m_scene_settings.Grid      = !ZEngine::ECS::EqualSceneGridSettings(canonical, m_scene_settings.GridBefore);
        return true;
    }

    bool Editor::ProcessSceneSettings(ZEngine::EngineContext& engine, ZEngine::ECS::SceneDiagnostics* diagnostics)
    {
        if (!Session.OnOwnerThread())
        {
            return false;
        }
        if (!m_scene_settings.Sky && !m_scene_settings.Grid)
        {
            return true;
        }
        if (!Session.InMutationPhase() || Session.PendingRequestCount())
        {
            return false;
        }
        const bool sky = m_scene_settings.Sky, grid = m_scene_settings.Grid;
        DiscardSceneSettings();
        auto* owner = static_cast<EditorScene*>(CurrentScene);
        if (m_closing || !SceneSettingsMatch() || !Session.Editable() || owner->SourceReadOnly || engine.Scene != Session.Scene() || engine.CloseRequested.value.load(std::memory_order_acquire) || (sky && (owner->Sky != m_scene_settings.SkyBefore || !m_scene_settings.SkyAfter.IsValid() || !ValidPrimary(Session, owner->Sky, m_scene_settings.SkyAfter, m_scene_settings.PrimaryTarget))) || (grid && (!ZEngine::ECS::EqualSceneGridSettings(owner->AuthoredGrid, m_scene_settings.GridBefore) || !ZEngine::ECS::ValidSceneGridSettings(m_scene_settings.GridAfter))))
        {
            ZEngine::ECS::SceneDiagnosticError(diagnostics, "Scene settings changed owner, revision or editability; the queued edit was rejected.");
            return false;
        }
        if (grid && !Session.CanSetGridSettings(m_scene_settings.GridBefore, m_scene_settings.GridAfter))
        {
            ZEngine::ECS::SceneDiagnosticError(diagnostics, "The complete Grid setting no longer fits the editable scene or shared history budget.");
            return false;
        }
        // Sky retains its existing untracked operation. Preflight the complete
        // compound intent above before retiring previews or invalidating history.
        if (sky)
        {
            Session.InvalidateForExternalMutation(false);
        }
        // A cancelled Grid preview may have restored its before-value. The
        // original copied intent was validated above; apply its absolute value
        // against that now-committed source, never create a second history stack.
        if (grid && !Session.SetGridSettings(owner->AuthoredGrid, m_scene_settings.GridAfter))
        {
            ZEngine::ECS::SceneDiagnosticError(diagnostics, Session.LastError());
            return false;
        }
        if (sky)
        {
            owner->Sky = m_scene_settings.SkyAfter;
            owner->MarkSkyDirty();
            owner->MarkDirty(true);
        }
        return true;
    }
} // namespace Tetragrama
