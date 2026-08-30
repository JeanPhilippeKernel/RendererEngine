#include <Tetragrama/Editor.h>
#include <Tetragrama/MessageToken.h>
#include <Tetragrama/Messengers/Messenger.h>
#include <Tetragrama/Panels/PanelHelpers.h>
#include <Tetragrama/Panels/ViewportPanel.h>
#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>
#include <cstring>

using namespace ZEngine::UI;
using namespace ZEngine::Helpers;

namespace Tetragrama::Panels
{
    static constexpr int kGizmoNone      = -1;
    static constexpr int kGizmoTranslate = 0;
    static constexpr int kGizmoRotate    = 1;
    static constexpr int kGizmoScale     = 2;

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

        m_scene_texture = app->RenderPipeline->SceneRenderer->GetFrameOutput();

        float sw = rect[2] - rect[0];
        float sh = rect[3] - rect[1];

        // Outer fill column — scene image fills the whole docked area, no chrome
        ZUIBox* bg   = ZUIBeginColumn(ctx, "##vp_bg", ZFill(), ZFill());
        bg->Flags    = bg->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(bg, kDarkBg);
        bg->EdgeSoftness = 0.f;

        // Scene image — fills the column, drag-drop target
        ZUIBox* img       = ZUIPushBox(ctx, "##vp_img", 8, ZUI_DrawBackground | ZUI_Clickable);
        img->Size[0]      = ZFill();
        img->Size[1]      = ZFill();
        img->TextureIndex = m_scene_texture.Valid() ? (uint32_t) m_scene_texture.Index : 0xFFFFFFFFu;
        ZUIBoxSetColor(img, 1.f, 1.f, 1.f, m_scene_texture.Valid() ? 1.f : 0.f);

        ZUISignal img_sig = ZUISignalFromBox(ctx, img);
        ZUIPopBox(ctx);

        // Gate camera controller via viewport-hover
        ctx->ViewportHovered = (img_sig.Flags & ZUI_SignalHovered) != 0;

        // Drag-drop: accept scene files and raw mesh formats
        char drop_buf[512] = {};
        if (ZUIAcceptDrop(ctx, img, drop_buf, (uint32_t) sizeof(drop_buf)) && secure_strlen(drop_buf) > 0)
        {
            const char* dot = strrchr(drop_buf, '.');
            if (dot && strcmp(dot, ".zescene") == 0)
            {
                Messengers::IMessenger::SendAsync<ZEngine::Applications::Layer, Messengers::GenericMessage<std::string>>(
                    Tetragrama::EDITOR_COMPONENT_DOCKSPACE_REQUEST_OPENSCENE,
                    Messengers::GenericMessage<std::string>(drop_buf));
            }
            else if (dot && strcmp(dot, ".zemesh") == 0)
            {
                Messengers::IMessenger::SendAsync<ZEngine::Applications::Layer, Messengers::GenericMessage<std::string>>(
                    Tetragrama::EDITOR_COMPONENT_DOCKSPACE_REQUEST_OPENMESH,
                    Messengers::GenericMessage<std::string>(drop_buf));
            }
            else if (app->Configuration && dot &&
                     (strcmp(dot, ".glb") == 0 || strcmp(dot, ".gltf") == 0 ||
                      strcmp(dot, ".fbx") == 0 || strcmp(dot, ".obj") == 0))
            {
                secure_strncpy(app->Configuration->PendingImportPath,
                               sizeof(app->Configuration->PendingImportPath),
                               drop_buf, sizeof(app->Configuration->PendingImportPath) - 1);
                const char* fname = strrchr(drop_buf, '/');
                fname             = fname ? fname + 1 : drop_buf;
                secure_strncpy(app->Configuration->PendingImportName,
                               sizeof(app->Configuration->PendingImportName),
                               fname, sizeof(app->Configuration->PendingImportName) - 1);
                app->Configuration->ShowImporter  = true;
                app->Configuration->FocusImporter = true;
            }
        }

        // Resize: emit a resize request whenever the docked area changes
        if ((uint32_t) sw != m_last_w || (uint32_t) sh != m_last_h)
        {
            m_last_w = (uint32_t) sw;
            m_last_h = (uint32_t) sh;
            if (app->State)
            {
                app->State->RenderTargetResizeRequests.Emplace({.Width = m_last_w, .Height = m_last_h});
            }
        }

        // --- Overlay toolbar: vertical, floated top-left at (8, 8) ---
        {
            static constexpr float kBtnSz  = 28.f;  // button height (also min draw size)
            static constexpr float kTbW    = 36.f;  // toolbar width — 4px padding each side
            static constexpr float kSepH   = 1.f;
            static constexpr float kPad    = 5.f;   // top / bottom inner padding
            static constexpr float kGap    = 4.f;   // gap between buttons
            static constexpr float kSepGap = 4.f;   // gap on each side of separator
            float tb_h = kPad + kBtnSz + kSepGap + kSepH + kSepGap
                       + kBtnSz + kGap + kBtnSz + kGap + kBtnSz + kPad;

            ZUIBox* tb   = ZUIBeginColumn(ctx, "##vp_tb", ZPx(kTbW), ZPx(tb_h));
            tb->Flags    = tb->Flags | ZUI_FloatX | ZUI_FloatY;
            tb->FloatPos[0] = 8.f;
            tb->FloatPos[1] = 8.f;

            ZUISpacer(ctx, kPad);

            // Grid toggle button — cyan theme
            {
                static const float kCol[4] = {0.30f, 0.80f, 0.90f, 1.f};
                bool  act = m_grid_enabled;
                ZUIBox* b = ZUIPushBox(ctx, "##vp_bg0", 8, ZUI_DrawBackground | ZUI_Clickable | ZUI_DrawActorIcon);
                b->Size[0] = ZFill();   // fills toolbar width — icon renderer uses sz=min(w,h)=kBtnSz
                b->Size[1] = ZPx(kBtnSz);
                bool hov   = (ctx->HotKey == b->Key);
                if (act)
                    ZUIBoxSetColor(b, kCol[0] * 0.25f, kCol[1] * 0.25f, kCol[2] * 0.25f, 0.92f);
                else if (hov)
                    ZUIBoxSetColor(b, 0.30f, 0.30f, 0.30f, 0.90f);
                else
                    ZUIBoxSetColor(b, 0.12f, 0.12f, 0.12f, 0.70f);
                float dim        = (act || hov) ? 1.f : 0.55f;
                b->TextColor[0]  = kCol[0] * dim;
                b->TextColor[1]  = kCol[1] * dim;
                b->TextColor[2]  = kCol[2] * dim;
                b->TextColor[3]  = 1.f;
                ZUIBoxSetCornerRadius(b, 3.f);
                auto* ps = ZUIStateGetOrInsert(&ctx->StateStore, b->Key);
                if (ps) ps->UserData = ZUI_ICON_GRID;
                ZUISignal sig = ZUISignalFromBox(ctx, b);
                ZUIPopBox(ctx);
                if (sig.Flags & ZUI_SignalClicked)
                    m_grid_enabled = !m_grid_enabled;
            }

            // Separator with breathing room
            ZUISpacer(ctx, kSepGap);
            {
                ZUIBox* sep  = ZUIPushBox(ctx, "##vp_sep", 8, ZUI_DrawBackground);
                sep->Size[0] = ZFill();
                sep->Size[1] = ZPx(kSepH);
                ZUIBoxSetColor(sep, 0.30f, 0.30f, 0.32f, 0.50f);
                ZUIPopBox(ctx);
            }
            ZUISpacer(ctx, kSepGap);

            // Translate / Rotate / Scale buttons
            struct BtnDef
            {
                const char* key;
                int         op;
                float       col[4];
                float       icon;
            };
            static const BtnDef kBtns[3] = {
                {"##vp_bt", kGizmoTranslate, {0.33f, 0.60f, 1.00f, 1.f}, ZUI_ICON_TRANSLATE},
                {"##vp_br", kGizmoRotate,    {1.00f, 0.60f, 0.20f, 1.f}, ZUI_ICON_ROTATE},
                {"##vp_bs", kGizmoScale,     {0.30f, 0.85f, 0.40f, 1.f}, ZUI_ICON_SCALE},
            };

            for (int i = 0; i < 3; ++i)
            {
                if (i > 0) ZUISpacer(ctx, kGap);
                const BtnDef& d   = kBtns[i];
                bool          act = (m_gizmo_op == d.op);
                ZUIBox*       b   = ZUIPushBox(ctx, d.key, (uint32_t) strlen(d.key),
                                             ZUI_DrawBackground | ZUI_Clickable | ZUI_DrawActorIcon);
                b->Size[0]        = ZFill();   // fills toolbar width
                b->Size[1]        = ZPx(kBtnSz);
                bool hov          = (ctx->HotKey == b->Key);
                if (act)
                    ZUIBoxSetColor(b, d.col[0] * 0.25f, d.col[1] * 0.25f, d.col[2] * 0.25f, 0.92f);
                else if (hov)
                    ZUIBoxSetColor(b, 0.30f, 0.30f, 0.30f, 0.90f);
                else
                    ZUIBoxSetColor(b, 0.12f, 0.12f, 0.12f, 0.70f);
                float dim        = (act || hov) ? 1.f : 0.55f;
                b->TextColor[0]  = d.col[0] * dim;
                b->TextColor[1]  = d.col[1] * dim;
                b->TextColor[2]  = d.col[2] * dim;
                b->TextColor[3]  = 1.f;
                ZUIBoxSetCornerRadius(b, 3.f);
                auto* ps = ZUIStateGetOrInsert(&ctx->StateStore, b->Key);
                if (ps) ps->UserData = d.icon;
                ZUISignal sig = ZUISignalFromBox(ctx, b);
                ZUIPopBox(ctx);
                if (sig.Flags & ZUI_SignalClicked)
                    m_gizmo_op = act ? kGizmoNone : d.op;
            }

            ZUISpacer(ctx, kPad);
            ZUIEndColumn(ctx);
        }

        // --- FPS overlay: floated top-right ---
        {
            if (ctx->DeltaTime > 0.f)
                m_fps_ema = m_fps_ema * 0.95f + (1.f / ctx->DeltaTime) * 0.05f;
            char fps_buf[32];
            snprintf(fps_buf, sizeof(fps_buf), "%.0f fps", (double) m_fps_ema);

            static constexpr float kFpsW = 64.f;
            ZUIBox* fps_row      = ZUIBeginRow(ctx, "##vp_fps", ZPx(kFpsW), ZPx(22.f));
            fps_row->Flags       = fps_row->Flags | ZUI_FloatX | ZUI_FloatY;
            fps_row->FloatPos[0] = sw - kFpsW - 8.f;
            fps_row->FloatPos[1] = 8.f;
            ZUIBoxSetColor(fps_row, 0.f, 0.f, 0.f, 0.f);
            ZUILabel(ctx, fps_buf, ctx->Theme.TextDim);
            ZUIEndRow(ctx);
        }

        ZUIEndColumn(ctx);
    }

} // namespace Tetragrama::Panels
