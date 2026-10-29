#include <Tetragrama/Editor.h>
#include <Tetragrama/Panels/PanelHelpers.h>
#include <Tetragrama/Panels/ViewportPanel.h>
#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/UI/ZUIFont.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>
#include <cstring>

using namespace ZEngine::UI;
using namespace ZEngine::Helpers;
using namespace ZEngine::Core::VFS;

namespace Tetragrama::Panels
{
    void ViewportPanel::Initialize(Tetragrama::Layers::ZUILayer* layer)
    {
        m_layer = layer;
    }

    namespace
    {
        void QueueGizmoChange(EditorSession& session, const Gizmo::Change& change)
        {
            if (!session.RequestGizmoChange(change))
                ZENGINE_CORE_WARN("[Gizmo] Toolbar request rejected; preferences were preserved")
        }

        void SnapControl(ZUIContext* ctx, EditorSession& session, Gizmo::SnapTarget target, const char* key, const char* popup_key, ZUIIcon icon, const char* toggle_label)
        {
            const auto* setting = Gizmo::SnapFor(session.GizmoSettings(), target);
            if (!setting)
            {
                return;
            }
            const auto  presets = Gizmo::SnapPresets(target);
            const char* label   = "";
            for (const auto& preset : presets)
            {
                if (preset.Increment == setting->Increment)
                {
                    label = preset.Label;
                }
            }
            const auto signal = ZUIEditorControl(ctx, key, icon, label, true, setting->Enabled);
            ZUISetTooltip(ctx, signal, toggle_label);
            if (signal.Flags & ZUI_SignalClicked)
            {
                const auto* bounds = ZUIStateGetOrInsert(&ctx->StateStore, signal.Key);
                ZUIOpenPopup(ctx, popup_key, bounds ? bounds->ScreenMinX : ctx->MousePos[0], bounds ? bounds->ScreenMaxY : ctx->MousePos[1]);
            }

            const float previous_width = ctx->PopupDesiredW;
            ctx->PopupDesiredW         = Gizmo::Constants::SnapMenuWidthPx - ctx->Style.PopupInnerPaddingX * 2.f;
            if (ZUIBeginPopup(ctx, popup_key))
            {
                if (ZUIMenuItemEx(ctx, toggle_label, nullptr, setting->Enabled))
                {
                    QueueGizmoChange(session, {.Kind = Gizmo::ChangeKind::SnapEnabled, .Target = target, .Enabled = !setting->Enabled});
                }
                ZUISeparator(ctx);
                for (const auto& preset : presets)
                {
                    if (ZUIMenuItemEx(ctx, preset.Label, nullptr, preset.Increment == setting->Increment))
                    {
                        QueueGizmoChange(session, {.Kind = Gizmo::ChangeKind::SnapIncrement, .Target = target, .Increment = preset.Increment});
                    }
                }
                if (target == Gizmo::SnapTarget::Translation)
                {
                    ZUISeparator(ctx);
                    const bool shown = session.GizmoSettings().ShowGrid;
                    if (ZUIMenuItemEx(ctx, "Show grid", nullptr, shown))
                    {
                        QueueGizmoChange(session, {.Kind = Gizmo::ChangeKind::GridVisibility, .Enabled = !shown});
                    }
                }
                ZUIEndPopup(ctx);
            }
            ctx->PopupDesiredW = previous_width;
        }
    } // namespace

    void ViewportPanel::BuildProjectionControl(ZUIContext* ctx, const EditorSession& session, ZEngine::Controllers::ICameraController* controller)
    {
        using ZEngine::Rendering::Cameras::CameraType;
        const auto* camera = controller ? controller->GetCamera() : nullptr;
        const auto  type   = camera ? camera->Type : CameraType::UNDEFINED;
        const bool  usable = type == CameraType::PERSPECTIVE || type == CameraType::ORTHOGRAPHIC;
        const bool  locked = !usable || session.Active();
        if (locked)
        {
            ZUIBeginDisabled(ctx);
        }
        const auto signal = ZUIEditorControl(ctx, "##vp_persp", ZUIIcon::Box, type == CameraType::ORTHOGRAPHIC ? "Orthographic" : (usable ? "Perspective" : "Projection"), true);
        if (signal.Flags & ZUI_SignalClicked)
        {
            const auto* bounds = ZUIStateGetOrInsert(&ctx->StateStore, signal.Key);
            ZUIOpenPopup(ctx, "##vp_projection_menu", bounds ? bounds->ScreenMinX : ctx->MousePos[0], bounds ? bounds->ScreenMaxY : ctx->MousePos[1]);
        }
        if (ZUIBeginPopup(ctx, "##vp_projection_menu"))
        {
            // This is editor-camera navigation, not an authored edit. Explicit
            // choices cannot double-toggle or alter a captured gizmo projection.
            if (ZUIMenuItemEx(ctx, "Perspective", nullptr, type == CameraType::PERSPECTIVE) && !controller->SetProjectionType(CameraType::PERSPECTIVE))
                ZENGINE_CORE_WARN("[Viewport] Controller rejected perspective projection")
            if (ZUIMenuItemEx(ctx, "Orthographic", nullptr, type == CameraType::ORTHOGRAPHIC) && !controller->SetProjectionType(CameraType::ORTHOGRAPHIC))
                ZENGINE_CORE_WARN("[Viewport] Controller rejected orthographic projection")
            ZUIEndPopup(ctx);
        }
        if (locked)
        {
            ZUIEndDisabled(ctx);
        }
    }

    void ViewportPanel::BuildTransformToolbar(ZUIContext* ctx, EditorSession& session)
    {
        const auto& state = session.GizmoSettings();
        const bool  grid  = session.Selection().ActiveSceneTarget() == SceneTarget::Grid;
        ZUIBox*     right = ZUIBeginRow(ctx, "##vp_transform", ZFit(), ZPx(ZEngine::UI::Metrics::HeightControl));
        right->Gap        = ZEngine::UI::Metrics::Space4;
        if (!session.Ready())
        {
            ZUIBeginDisabled(ctx);
        }
        const auto mode_control = [&](const char* key, ZUIIcon icon, Gizmo::Operation mode, const char* tooltip) {
            const auto signal = ZUIEditorControl(ctx, key, icon, nullptr, false, state.Mode == mode);
            ZUISetTooltip(ctx, signal, tooltip);
            if (signal.Flags & ZUI_SignalClicked)
            {
                QueueGizmoChange(session, {.Kind = Gizmo::ChangeKind::Operation, .Mode = mode});
            }
        };
        mode_control("##vp_sel", ZUIIcon::MousePointer2, Gizmo::Operation::Select, "Select (Q)");
        mode_control("##vp_move", ZUIIcon::Move, Gizmo::Operation::Translate, "Move (W)");
        mode_control("##vp_rot", ZUIIcon::Rotate3D, Gizmo::Operation::Rotate, "Rotate (E)");
        if (grid)
        {
            ZUIBeginDisabled(ctx);
        }
        mode_control("##vp_scale", ZUIIcon::Scaling, Gizmo::Operation::Scale, "Scale (R)");
        if (grid)
        {
            ZUIEndDisabled(ctx);
        }
        const bool local = grid || state.Space == TransformSpacePolicy::Local;
        if (grid)
        {
            ZUIBeginDisabled(ctx);
        }
        const auto space = ZUIEditorControl(ctx, "##vp_space", local ? ZUIIcon::Box : ZUIIcon::Globe, nullptr, false, local);
        ZUISetTooltip(ctx, space, local ? "Space: Local" : "Space: World");
        if (space.Flags & ZUI_SignalClicked)
        {
            QueueGizmoChange(session, {.Kind = Gizmo::ChangeKind::Space, .Space = local ? TransformSpacePolicy::World : TransformSpacePolicy::Local});
        }
        if (grid)
        {
            ZUIEndDisabled(ctx);
        }
        SnapControl(ctx, session, Gizmo::SnapTarget::Translation, "##vp_grid", "##vp_grid_menu", ZUIIcon::Grid3x3, "Snap to grid");
        SnapControl(ctx, session, Gizmo::SnapTarget::Rotation, "##vp_snaprot", "##vp_angle_menu", ZUIIcon::RotateCw, "Snap to angle");
        if (grid)
        {
            ZUIBeginDisabled(ctx);
        }
        SnapControl(ctx, session, Gizmo::SnapTarget::Scale, "##vp_snapscale", "##vp_scale_menu", ZUIIcon::Maximize, "Snap scale");
        if (grid)
        {
            ZUIEndDisabled(ctx);
        }
        if (!session.Ready())
        {
            ZUIEndDisabled(ctx);
        }
        ZUIEditorControl(ctx, "##vp_cam", ZUIIcon::Camera, "4", true);
        ZUIEndRow(ctx);
    }

    // HOT PATH — runs every frame, no heap allocation allowed.
    void ViewportPanel::BuildContent(ZUIContext* ctx, float rect[4])
    {
        static const float kDarkBg[4] = {0.09f, 0.09f, 0.095f, 1.f};

        if (!m_layer || !m_layer->CurrentApp)
        {
            EmptyPanelBg(ctx, "##vp_bg", kDarkBg, nullptr);
            return;
        }

        auto* app = reinterpret_cast<Tetragrama::EditorPtr>(m_layer->CurrentApp);
        if (!app->RenderPipeline || !app->RenderPipeline->SceneRenderer)
        {
            EmptyPanelBg(ctx, "##vp_bg", kDarkBg, nullptr);
            return;
        }

        m_scene_texture                 = app->RenderPipeline->SceneRenderer->GetFrameOutput();

        float sw                        = rect[2] - rect[0];
        float sh                        = rect[3] - rect[1];
        app->GizmoLogicalViewportHeight = sh;

        // Outer fill column — scene image fills the whole docked area, no chrome
        ZUIBox* bg                      = ZUIBeginColumn(ctx, "##vp_bg", ZFill(), ZFill());
        bg->Flags                       = bg->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(bg, kDarkBg);
        bg->EdgeSoftness  = 0.f;

        // Scene image — fills the column, drag-drop target
        ZUIBox* img       = ZUIPushBox(ctx, "##vp_img", 8, ZUI_DrawBackground | ZUI_Clickable | ZUI_NoKeyboardFocus);
        img->Size[0]      = ZFill();
        img->Size[1]      = ZFill();
        img->TextureIndex = m_scene_texture.Valid() ? (uint32_t) m_scene_texture.Index : 0xFFFFFFFFu;
        ZUIBoxSetColor(img, 1.f, 1.f, 1.f, m_scene_texture.Valid() ? 1.f : 0.f);
        ctx->ViewportInputKey   = img->Key;
        // Read-only scenes remain selectable. Authoring tools add their own
        // Editable gate when they begin a transaction.
        ctx->ViewportSelectable = app->Session.Ready();
        ctx->ViewportEditable   = app->Session.Editable();

        ZUISignal img_sig       = ZUISignalFromBox(ctx, img);
        ZUIPopBox(ctx);

        // The camera consumes raw GLFW cursor positions, not UI-space positions.
        // Convert both the hover bounds and the extent used by camera rays/panning.
        if (app->CameraController)
        {
            app->CameraController->SetViewportFromUI(rect, ctx->InputScale);
        }

        // One transparent full-viewport surface. The custom filter accepts only
        // actual handles, so empty viewport space remains available to navigation.
        // Toolbar siblings are built later and retain paint-order priority.
        auto* gizmo              = ZUIPushBox(ctx, "##vp_gizmo", 10, ZUI_Clickable | ZUI_NoKeyboardFocus | ZUI_FloatX | ZUI_FloatY);
        gizmo->Size[0]           = ZFill();
        gizmo->Size[1]           = ZFill();
        gizmo->CustomHitTest     = Gizmo::Viewport::Filter;
        gizmo->CustomHitTestData = &app->GizmoViewport;
        app->GizmoViewport.Bind(ctx, rect, img->Key, gizmo->Key);
        ctx->ViewportOverlayKey = gizmo->Key;
        app->RefreshGizmoHitFrame(ctx);
        // A captured gizmo remains precise even when its drag leaves the image.
        // The semantic request is resolved once by the UI platform layer.
        if (app->Session.Active() && !ctx->TextInputActive && !ctx->ActiveModalKey && !ctx->ModalBox && !ctx->PopupStackSize && !ctx->PendingPopupKey)
        {
            ZUIRequestCursor(ctx, ZUICursor::Crosshair, ZUI_CursorPriorityCapture);
        }
        ZUIPopBox(ctx);

        // Keep ViewportHovered for ZUI-level concerns (drag-drop, scroll routing).
        ctx->ViewportHovered             = (img_sig.Flags & ZUI_SignalHovered) != 0;

        // Drag-drop: accept scene files and raw mesh formats
        char               drop_buf[512] = {};
        ZUIDragPayloadKind drop_kind     = ZUIDragPayloadKind::Opaque;
        if (ZUIAcceptDrop(ctx, img, drop_buf, (uint32_t) sizeof(drop_buf), &drop_kind) && drop_kind != ZUIDragPayloadKind::Opaque && secure_strlen(drop_buf) > 0)
        {
            const auto parsed        = ZEngine::Core::VFS::VFSPath::Parse(drop_buf);
            const auto ext           = parsed.Succeeded() ? parsed.Value().Extension() : ZEngine::Core::VFS::VFSPathComponent{};
            char       extension[12] = {};
            if (ext.Length < sizeof(extension))
            {
                for (size_t i = 0; i < ext.Length; ++i)
                {
                    extension[i] = ext.Data[i] >= 'A' && ext.Data[i] <= 'Z' ? ext.Data[i] + ('a' - 'A') : ext.Data[i];
                }
            }
            // Cooked assets are loaded through the VFS; native files are only accepted by importers.
            if (drop_kind == ZUIDragPayloadKind::VFSPath && ZEngine::Helpers::secure_strcmp(extension, ".zescene") == 0)
            {
                (void) app->RequestSceneOpen(parsed.Value(), app->Session.SceneEpoch());
            }
            else if (drop_kind == ZUIDragPayloadKind::VFSPath && ZEngine::Helpers::secure_strcmp(extension, ".zemesh") == 0)
            {
                (void) app->RequestMeshDrop(parsed.Value(), app->Session.SceneEpoch());
            }
            else if (app->Configuration && (ZEngine::Helpers::secure_strcmp(extension, ".glb") == 0 || ZEngine::Helpers::secure_strcmp(extension, ".gltf") == 0 || ZEngine::Helpers::secure_strcmp(extension, ".fbx") == 0 || ZEngine::Helpers::secure_strcmp(extension, ".obj") == 0))
            {
                auto* eng = ZEngine::Engine::GetContext();
                if (ResolveDroppedFile(drop_buf, drop_kind, app->Configuration->WorkingSpacePath.c_str(), eng ? eng->EngineAssetsNativeRoot : nullptr, app->Configuration->PendingImportPath, sizeof(app->Configuration->PendingImportPath)))
                {
                    const auto name = parsed.Value().Filename();
                    secure_strncpy(app->Configuration->PendingImportName, sizeof(app->Configuration->PendingImportName), name.Data, name.Length < sizeof(app->Configuration->PendingImportName) ? name.Length : sizeof(app->Configuration->PendingImportName) - 1);
                    app->Configuration->ShowImporter       = true;
                    app->Configuration->PendingImportEpoch = app->Session.SceneEpoch();
                    app->Configuration->FocusImporter      = true;
                }
            }
        }

        // Resize: emit a resize request whenever the docked area changes
        const uint32_t requested_width  = static_cast<uint32_t>(sw);
        const uint32_t requested_height = static_cast<uint32_t>(sh);
        if (requested_width > 0 && requested_height > 0 && (requested_width != m_last_w || requested_height != m_last_h) && app->State)
        {
            if (app->State->RenderTargetResizeRequests.push({.Width = requested_width, .Height = requested_height}))
            {
                m_last_w = requested_width;
                m_last_h = requested_height;
            }
        }

        // Figma viewport overlays: display controls top-left, transform/snapping controls top-right,
        // scene info bottom-left, performance readout bottom-right.
        {
            ZUIBox* top      = ZUIBeginRow(ctx, "##vp_top", ZPx(sw - 16.f), ZPx(26.f));
            top->Flags       = top->Flags | ZUI_FloatX | ZUI_FloatY;
            top->FloatPos[0] = 8.f;
            top->FloatPos[1] = 8.f;
            top->MainAlign   = ZUIAlign::SpaceBetween;

            ZUIBox* left     = ZUIBeginRow(ctx, "##vp_display", ZFit(), ZPx(26.f));
            left->Gap        = 5.f;
            ZUIEditorControl(ctx, "##vp_menu", ZUIIcon::Menu, nullptr, false);
            BuildProjectionControl(ctx, app->Session, app->CameraController);
            ZUIEditorControl(ctx, "##vp_lit", ZUIIcon::Sun, "Lit", true);
            ZUIEditorControl(ctx, "##vp_show", ZUIIcon::Count, "Show", true);
            ZUIEndRow(ctx);

            BuildTransformToolbar(ctx, app->Session);

            ZUIEndRow(ctx);
        }

        {
            // Scene information, bottom-left.
            const char* scene_name = (app->Configuration && !app->Configuration->ActiveSceneName.empty()) ? app->Configuration->ActiveSceneName.c_str() : "Untitled";
            char        info[200]  = {};
            snprintf(info, sizeof(info), "%s  •  Persistent Scene", scene_name);
            ZUIBox* row      = ZUIBeginRow(ctx, "##vp_scene", ZFit(), ZPx(13.f));
            row->Flags       = row->Flags | ZUI_FloatX | ZUI_FloatY;
            row->FloatPos[0] = 12.f;
            row->FloatPos[1] = sh - 12.f - 13.f;
            row->Gap         = 8.f;
            row->CrossAlign  = ZUIAlign::Center;
            ZUIIconBox(ctx, ZUIIcon::Map, 12.f);
            ZUILabelTight(ctx, info, ctx->Theme.TextDefault, ZUIFontSize::Body);
            ZUIEndRow(ctx);
        }

        {
            // Performance readout, bottom-right: render-thread SmoothedDeltaTime (includes vsync wait).
            const float smooth_dt   = ZEngine::Engine::GetContext()->SmoothedDeltaTime;
            const float fps         = smooth_dt > 0.f ? 1.f / smooth_dt : 0.f;
            char        fps_buf[24] = {};
            char        ms_buf[24]  = {};
            snprintf(fps_buf, sizeof(fps_buf), "%.1f FPS", (double) fps);
            snprintf(ms_buf, sizeof(ms_buf), "%.2f ms", (double) (smooth_dt * 1000.f));

            ZUIBox* row      = ZUIBeginRow(ctx, "##vp_perf", ZFit(), ZPx(12.f));
            row->Flags       = row->Flags | ZUI_FloatX | ZUI_FloatY;
            row->FloatPos[0] = sw - 14.f - 99.f;
            row->FloatPos[1] = sh - 12.f - 12.f;
            row->Gap         = 8.f;
            row->CrossAlign  = ZUIAlign::Center;
            ZUILabelTight(ctx, fps_buf, ctx->Theme.TextDefault, ZUIFontSize::Body);
            ZUIBox* sep  = ZUIPushBox(ctx, "##vp_perf_sep", 13, ZUI_DrawBackground);
            sep->Size[0] = ZPx(1.f);
            sep->Size[1] = ZPx(12.f);
            ZUIBoxSetColorArr(sep, ctx->Theme.TextDim);
            sep->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);
            ZUILabelTight(ctx, ms_buf, ctx->Theme.TextDefault, ZUIFontSize::Body);
            ZUIEndRow(ctx);
        }

        ZUIEndColumn(ctx);
        app->GizmoViewport.BuildPill(app->Session, ctx);
    }

} // namespace Tetragrama::Panels
