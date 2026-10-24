#include <Tetragrama/Editor.h>

namespace Tetragrama
{
    void Editor::RefreshGizmoHitFrame(const ZEngine::UI::ZUIContext* ctx)
    {
        if (EnableGizmoDebug || !CameraController || !CameraController->GetCamera())
        {
            GizmoViewport.Invalidate();
            return;
        }
        GizmoViewport.BuildHitFrame(Session, CameraController->GetCamera()->CaptureFrameData(), ctx, m_gizmo_focused);
    }

    void Editor::UpdateGizmoInput(ZEngine::UI::ZUIContext* ctx, bool focused)
    {
        m_gizmo_focused = focused;
        if (ctx)
        {
            ctx->InteractionCapture = {Session.Active(), Session.Active()};
        }
        SyncSceneChangeCapture(ctx);
        if (EnableGizmoDebug)
        {
            GizmoViewport.Unbind();
        }
        // Runtime waves and scheduled completions have joined before this point.
        // Legacy raw TRS writers require this synchronization before input; only
        // resolve a visible transform target, not every toolbar or idle update.
        if (focused && GizmoViewport.Bound(ctx) && Session.Editable() && !Session.Selection().Entities().empty())
        {
            Session.Scene()->SynchronizeHierarchy();
        }
        const auto camera = CameraController && CameraController->GetCamera() ? CameraController->GetCamera()->CaptureFrameData() : ZEngine::Rendering::Cameras::CameraFrameData{};
        GizmoViewport.UpdateHover(Session, camera, ctx, focused);
    }
} // namespace Tetragrama
