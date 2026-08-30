#include <GLFW/glfw3.h>
#include <Tetragrama/Components/ZUI/ZUIDockspaceComponent.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorScene.h>
#include <Tetragrama/Serializers/EditorSceneSerializer.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/UI/ZUIDockspace.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>

using namespace ZEngine::UI;

namespace Tetragrama::Components
{
    static constexpr float k_dim[4] = {0.55f, 0.55f, 0.60f, 1.f};

    void ZUIDockspaceComponent::Initialize(Tetragrama::Layers::ZUILayer* parent, cstring name, bool visibility)
    {
        ParentLayer = parent;
        Name        = name;
        Visible     = visibility;
    }

    void ZUIDockspaceComponent::BuildUI(ZUIContext* ctx)
    {
        if (!Visible)
        {
            return;
        }

        float sw = (float) ctx->ScreenW;
        float sh = (float) ctx->ScreenH;

        // Full-screen overlay — alpha=0 background so ZFill() children resolve
        // correctly, but visually transparent. Renders on top of PanelManagerComponent.
        ZUIBox* bg      = ZUIBeginColumn(ctx, "##dockspace_bg", ZPx(sw), ZPx(sh));
        bg->Flags       = bg->Flags | ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY;
        bg->FloatPos[0] = 0.f;
        bg->FloatPos[1] = 0.f;
        ZUIBoxSetColor(bg, 0.f, 0.f, 0.f, 0.f);
        bg->EdgeSoftness = 0.f;

        // Platform-aware shortcut display strings
#if defined(__APPLE__)
        static constexpr const char* kMod          = "Cmd+";
        static constexpr const char* kModShift     = "Cmd+Shift+";
        static constexpr const char* kQuitShortcut = "Cmd+Q";
#else
        static constexpr const char* kMod          = "Ctrl+";
        static constexpr const char* kModShift     = "Ctrl+Shift+";
        static constexpr const char* kQuitShortcut = "Alt+F4";
#endif
        char sc_new[24], sc_open[24], sc_save[24], sc_save_as[24], sc_undo[24], sc_redo[24], sc_all[24];
        snprintf(sc_new, sizeof(sc_new), "%sN", kMod);
        snprintf(sc_open, sizeof(sc_open), "%sO", kMod);
        snprintf(sc_save, sizeof(sc_save), "%sS", kMod);
        snprintf(sc_save_as, sizeof(sc_save_as), "%sS", kModShift);
        snprintf(sc_undo, sizeof(sc_undo), "%sZ", kMod);
        snprintf(sc_redo, sizeof(sc_redo), "%sY", kMod);
        snprintf(sc_all, sizeof(sc_all), "%sA", kMod);

        // --- Menu bar ---
        if (ZUIBeginMenuBar(ctx))
        {
            ZUISpacer(ctx, 6.f);

            // "File" menu
            if (ZUIBeginMenu(ctx, "File"))
            {
                auto* file_app = (ParentLayer && ParentLayer->CurrentApp) ? reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp) : nullptr;

                if (ZUIMenuItemEx(ctx, "New Scene", sc_new))
                {
                    if (file_app && file_app->CurrentScene)
                    {
                        auto* scene                = reinterpret_cast<EditorScenePtr>(file_app->CurrentScene);
                        scene->SelectedActorHandle = {};
                    }
                }
                if (ZUIMenuItemEx(ctx, "Open Scene...", sc_open))
                {
                    ZENGINE_CORE_INFO("[Editor] Use the Project panel to locate and drop a .zescene into the viewport")
                }
                ZUISeparator(ctx);
                if (ZUIMenuItemEx(ctx, "Save Scene", sc_save))
                {
                    if (file_app && file_app->CurrentScene && file_app->Configuration)
                    {
                        auto*                              scene = reinterpret_cast<EditorScenePtr>(file_app->CurrentScene);
                        Serializers::EditorSceneSerializer serializer;
                        serializer.Serialize(scene);
                        ZENGINE_CORE_INFO("[Editor] Scene saved")
                    }
                }
                if (ZUIMenuItemEx(ctx, "Save Scene As...", sc_save_as))
                {
                    if (file_app && file_app->CurrentScene)
                    {
                        auto*                              scene = reinterpret_cast<EditorScenePtr>(file_app->CurrentScene);
                        Serializers::EditorSceneSerializer serializer;
                        serializer.Serialize(scene);
                    }
                }
                ZUISeparator(ctx);
                if (ZUIMenuItemEx(ctx, "Quit", kQuitShortcut))
                {
                    if (file_app && file_app->CurrentWindow)
                    {
                        auto* glfw_win = static_cast<GLFWwindow*>(file_app->CurrentWindow->GetNativeWindow());
                        if (glfw_win)
                            glfwSetWindowShouldClose(glfw_win, GLFW_TRUE);
                    }
                }
                ZUIEndMenu(ctx);
            }
            ZUISpacer(ctx, 4.f);

            // "Edit" menu
            if (ZUIBeginMenu(ctx, "Edit"))
            {
                if (ZUIMenuItemEx(ctx, "Undo", sc_undo, false, false))
                {
                }
                if (ZUIMenuItemEx(ctx, "Redo", sc_redo, false, false))
                {
                }
                ZUISeparator(ctx);
                if (ZUIMenuItemEx(ctx, "Select All", sc_all))
                {
                    if (ParentLayer && ParentLayer->CurrentApp)
                    {
                        auto* edit_app   = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
                        auto* edit_scene = reinterpret_cast<EditorScenePtr>(edit_app->CurrentScene);
                        auto* eng        = ZEngine::Engine::GetContext();
                        if (edit_scene && eng && eng->ActorManager && eng->ActorManager->Count() > 0)
                        {
                            bool found = false;
                            eng->ActorManager->ForEach([&](ZEngine::ECS::ActorHandle h, ZEngine::ECS::Actor*) {
                                if (!found)
                                {
                                    edit_scene->SelectedActorHandle = h;
                                    found                           = true;
                                }
                            });
                        }
                    }
                }
                ZUIEndMenu(ctx);
            }
            ZUISpacer(ctx, 4.f);

            // "Settings" menu
            if (ZUIBeginMenu(ctx, "Settings"))
            {
                if (ZUIMenuItemEx(ctx, "Engine", nullptr, m_settings_open))
                    m_settings_open = !m_settings_open;
                ZUIEndMenu(ctx);
            }
            ZUISpacer(ctx, 4.f);

            // "Performances" menu
            if (ZUIBeginMenu(ctx, "Performances"))
            {
                if (ZUIMenuItemEx(ctx, "Memory Profiler", nullptr, m_perf_open))
                    m_perf_open = !m_perf_open;
                ZUIEndMenu(ctx);
            }

            ZUILabel(ctx, " | ", k_dim);

            // Scene name
            if (ParentLayer && ParentLayer->CurrentApp)
            {
                auto* app = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
                if (app->Configuration)
                {
                    const char* sname = app->Configuration->ActiveSceneName.empty() ? "-" : app->Configuration->ActiveSceneName.c_str();
                    char        scene_buf[128];
                    snprintf(scene_buf, sizeof(scene_buf), "Scene: %s", sname);
                    ZUILabel(ctx, scene_buf, k_dim);
                }
            }

            ZUIEndMenuBar(ctx);
        }

        // --- Engine Settings window ---
        if (m_settings_open)
        {
            static constexpr float kW     = 600.f;
            static constexpr float kH     = 420.f;
            static constexpr float kSideW = 140.f;
            float fh = ZUIGetFrameHeight(ctx);

            auto* stg_app   = (ParentLayer && ParentLayer->CurrentApp) ? reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp) : nullptr;
            auto* stg_scene = stg_app ? reinterpret_cast<EditorScenePtr>(stg_app->CurrentScene) : nullptr;

            auto mark_grid_dirty = [stg_scene]() {
                if (!stg_scene) return;
                stg_scene->GridDirty[0].value.store(true, std::memory_order_release);
                stg_scene->GridDirty[1].value.store(true, std::memory_order_release);
                stg_scene->GridDirty[2].value.store(true, std::memory_order_release);
            };

            // Dim backdrop — visual only; NOT Clickable (a clickable full-screen sibling
            // visited after the window in LIFO traversal would overwrite HotKey for all
            // window children). Click-outside is detected via bounds check below instead.
            {
                ZUIBox* dim      = ZUIPushBox(ctx, "##stg_dim", 9, ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
                dim->Size[0]     = ZPx(sw);
                dim->Size[1]     = ZPx(sh);
                dim->FloatPos[0] = 0.f;
                dim->FloatPos[1] = 0.f;
                ZUIBoxSetColor(dim, 0.f, 0.f, 0.f, 0.55f);
                ZUIPopBox(ctx);
            }

            // Click-outside detection using known window bounds (layout-independent)
            {
                float wx0 = (sw - kW) * 0.5f, wy0 = (sh - kH) * 0.5f;
                bool outside = ctx->MousePos[0] < wx0 || ctx->MousePos[0] > wx0 + kW
                            || ctx->MousePos[1] < wy0 || ctx->MousePos[1] > wy0 + kH;
                if (ctx->MouseReleased[0] && outside)
                    m_settings_open = false;
            }

            // Modal window — centered
            ZUIBox* win      = ZUIBeginColumn(ctx, "##stg_win", ZPx(kW), ZPx(kH));
            win->Flags       = win->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DropShadow | ZUI_FloatX | ZUI_FloatY | ZUI_ClipChildren;
            win->FloatPos[0] = (sw - kW) * 0.5f;
            win->FloatPos[1] = (sh - kH) * 0.5f;
            ZUIBoxSetColorArr(win, ctx->Theme.WindowBg);
            ZUIBoxSetCornerRadius(win, 5.f);
            win->BorderThickness = 1.f;
            win->BorderColor[0]  = ctx->Theme.PanelBorder[0];
            win->BorderColor[1]  = ctx->Theme.PanelBorder[1];
            win->BorderColor[2]  = ctx->Theme.PanelBorder[2];
            win->BorderColor[3]  = 1.f;
            win->EdgeSoftness    = 0.f;

            // Title bar
            {
                ZUIBox* tbar  = ZUIBeginRow(ctx, "##stg_tbar", ZFill(), ZPx(fh + 4.f));
                tbar->Flags   = tbar->Flags | ZUI_DrawBackground;
                ZUIBoxSetColorArr(tbar, ctx->Theme.TitleBarBg);
                ZUIBoxSetTopRadius(tbar, 5.f);
                tbar->EdgeSoftness = 0.f;

                ZUISpacer(ctx, 10.f);
                ZUILabel(ctx, "Engine Settings", ctx->Theme.TextDefault);

                ZUIBox* fill  = ZUIPushBox(ctx, "##stg_tf", 7, ZUI_None);
                fill->Size[0] = ZFill(); fill->Size[1] = ZPx(1.f);
                ZUIPopBox(ctx);

                // Close button
                ZUIBox* xb    = ZUIPushBox(ctx, "##stg_close", 11, ZUI_DrawBackground | ZUI_Clickable | ZUI_DrawText);
                xb->Size[0]   = ZPx(fh + 4.f); xb->Size[1] = ZPx(fh + 4.f);
                xb->Label     = ZUIPushStr(&ctx->FrameArena, "x", 1);
                xb->TextAlign = ZUITextAlign::Center;
                bool xhov     = (ctx->HotKey == xb->Key);
                ZUIBoxSetColor(xb, 1.f, 1.f, 1.f, xhov ? 0.12f : 0.f);
                xb->TextColor[0] = ctx->Theme.TextDim[0]; xb->TextColor[1] = ctx->Theme.TextDim[1];
                xb->TextColor[2] = ctx->Theme.TextDim[2]; xb->TextColor[3] = 1.f;
                ZUISignal xsig = ZUISignalFromBox(ctx, xb);
                ZUIPopBox(ctx);
                if (xsig.Flags & ZUI_SignalClicked) m_settings_open = false;
                ZUIEndRow(ctx);
            }

            // Separator under title
            {
                ZUIBox* sep  = ZUIPushBox(ctx, "##stg_hsep", 10, ZUI_DrawBackground);
                sep->Size[0] = ZFill(); sep->Size[1] = ZPx(1.f);
                ZUIBoxSetColor(sep, ctx->Theme.PanelBorder[0], ctx->Theme.PanelBorder[1], ctx->Theme.PanelBorder[2], 1.f);
                sep->EdgeSoftness = 0.f;
                ZUIPopBox(ctx);
            }

            // Body row: sidebar | separator | content
            ZUIBeginRow(ctx, "##stg_body", ZFill(), ZFill());

            // Sidebar
            ZUIBox* side   = ZUIBeginColumn(ctx, "##stg_side", ZPx(kSideW), ZFill());
            side->Flags    = side->Flags | ZUI_DrawBackground;
            ZUIBoxSetColor(side, 0.f, 0.f, 0.f, 0.15f);
            side->EdgeSoftness = 0.f;
            ZUISpacer(ctx, 6.f);
            {
                static const char* kPages[4]    = {"Grid", "Renderer", "Theme", "Layout"};
                static const char* kNavKeys[4]  = {"##nav_0", "##nav_1", "##nav_2", "##nav_3"};
                static const uint32_t kNKLen[4] = {7, 7, 7, 7};
                for (int pi = 0; pi < 4; ++pi)
                {
                    bool    act  = (m_settings_page == pi);
                    uint32_t pln = (uint32_t)strlen(kPages[pi]);
                    ZUIBox* nb   = ZUIPushBox(ctx, kNavKeys[pi], kNKLen[pi], ZUI_DrawBackground | ZUI_Clickable | ZUI_DrawText);
                    nb->Size[0]  = ZFill();
                    nb->Size[1]  = ZPx(fh + 8.f);
                    nb->Label    = ZUIPushStr(&ctx->FrameArena, kPages[pi], pln);
                    nb->TextAlign  = ZUITextAlign::Left;
                    nb->Padding[0] = 14.f;
                    if (act)
                    {
                        ZUIBoxSetColorArr(nb, ctx->Theme.TabActiveBg);
                        nb->TextColor[0] = nb->TextColor[1] = nb->TextColor[2] = nb->TextColor[3] = 1.f;
                    }
                    else
                    {
                        bool hov = (ctx->HotKey == nb->Key);
                        ZUIBoxSetColor(nb, 1.f, 1.f, 1.f, hov ? 0.06f : 0.f);
                        nb->TextColor[0] = ctx->Theme.TextDim[0]; nb->TextColor[1] = ctx->Theme.TextDim[1];
                        nb->TextColor[2] = ctx->Theme.TextDim[2]; nb->TextColor[3] = 1.f;
                    }
                    ZUIBoxSetCornerRadius(nb, 3.f);
                    ZUISignal sig = ZUISignalFromBox(ctx, nb);
                    ZUIPopBox(ctx);
                    if (sig.Flags & ZUI_SignalClicked) m_settings_page = pi;
                }
            }
            ZUIEndColumn(ctx); // sidebar

            // Vertical separator
            {
                ZUIBox* vs  = ZUIPushBox(ctx, "##stg_vsep", 9, ZUI_DrawBackground);
                vs->Size[0] = ZPx(1.f); vs->Size[1] = ZFill();
                ZUIBoxSetColor(vs, ctx->Theme.PanelBorder[0], ctx->Theme.PanelBorder[1], ctx->Theme.PanelBorder[2], 1.f);
                vs->EdgeSoftness = 0.f;
                ZUIPopBox(ctx);
            }

            // Content column
            ZUIBox* cnt = ZUIBeginColumn(ctx, "##stg_cnt", ZFill(), ZFill());
            cnt->Flags  = cnt->Flags | ZUI_ClipChildren;
            cnt->EdgeSoftness = 0.f;
            ZUISpacer(ctx, 8.f);

            if (m_settings_page == 0 && stg_scene) // Grid
            {
                auto& cfg = stg_scene->Grid;

                // Helpers: each row is [10px gap][140px label col][fill control]
                auto lbl_ctrl_row = [&](const char* row_k, const char* lbl_k, const char* label) -> ZUIBox* {
                    ZUIBeginRow(ctx, row_k, ZFill(), ZPx(fh + 4.f));
                    ZUISpacer(ctx, 10.f);
                    ZUIBox* lc = ZUIBeginColumn(ctx, lbl_k, ZPx(140.f), ZFill());
                    ZUILabel(ctx, label, ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    return lc; // caller calls ZUIEndRow after adding the control
                };
                (void) lbl_ctrl_row; // used via explicit calls below

                // Show Grid
                {
                    ZUIBeginRow(ctx, "##sg_en_r", ZFill(), ZPx(fh + 4.f));
                    ZUISpacer(ctx, 10.f);
                    ZUIBox* lc = ZUIBeginColumn(ctx, "##sg_en_l", ZPx(140.f), ZFill());
                    ZUILabel(ctx, "Show Grid", ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    bool prev = cfg.Enabled;
                    ZUICheckbox(ctx, "##sg_en_cb", &cfg.Enabled);
                    ZUIEndRow(ctx); ZUISpacer(ctx, 2.f);
                    if (cfg.Enabled != prev) mark_grid_dirty();
                }
                // Cell Size
                { float prev=cfg.CellSize;     ZUIBeginRow(ctx,"##sg_cs_r",ZFill(),ZPx(fh+4.f)); ZUISpacer(ctx,10.f); ZUIBox* lc=ZUIBeginColumn(ctx,"##sg_cs_l",ZPx(140.f),ZFill()); ZUILabel(ctx,"Cell Size",ctx->Theme.TextDefault); ZUIEndColumn(ctx); ZUISliderFloat(ctx,"##sg_cs_s",&cfg.CellSize,0.001f,1.f); ZUIEndRow(ctx); ZUISpacer(ctx,2.f); if(cfg.CellSize!=prev) mark_grid_dirty(); }
                // Fade Radius
                { float prev=cfg.FadeRadius;   ZUIBeginRow(ctx,"##sg_fr_r",ZFill(),ZPx(fh+4.f)); ZUISpacer(ctx,10.f); ZUIBox* lc=ZUIBeginColumn(ctx,"##sg_fr_l",ZPx(140.f),ZFill()); ZUILabel(ctx,"Fade Radius",ctx->Theme.TextDefault); ZUIEndColumn(ctx); ZUISliderFloat(ctx,"##sg_fr_s",&cfg.FadeRadius,10.f,2000.f); ZUIEndRow(ctx); ZUISpacer(ctx,2.f); if(cfg.FadeRadius!=prev) mark_grid_dirty(); }
                // Fade Strength
                { float prev=cfg.FadeStrength; ZUIBeginRow(ctx,"##sg_fs_r",ZFill(),ZPx(fh+4.f)); ZUISpacer(ctx,10.f); ZUIBox* lc=ZUIBeginColumn(ctx,"##sg_fs_l",ZPx(140.f),ZFill()); ZUILabel(ctx,"Fade Strength",ctx->Theme.TextDefault); ZUIEndColumn(ctx); ZUISliderFloat(ctx,"##sg_fs_s",&cfg.FadeStrength,0.1f,2.f); ZUIEndRow(ctx); ZUISpacer(ctx,2.f); if(cfg.FadeStrength!=prev) mark_grid_dirty(); }
                // Line Width
                { float prev=cfg.LineWidth;    ZUIBeginRow(ctx,"##sg_lw_r",ZFill(),ZPx(fh+4.f)); ZUISpacer(ctx,10.f); ZUIBox* lc=ZUIBeginColumn(ctx,"##sg_lw_l",ZPx(140.f),ZFill()); ZUILabel(ctx,"Line Width",ctx->Theme.TextDefault); ZUIEndColumn(ctx); ZUISliderFloat(ctx,"##sg_lw_s",&cfg.LineWidth,0.5f,4.f); ZUIEndRow(ctx); ZUISpacer(ctx,2.f); if(cfg.LineWidth!=prev) mark_grid_dirty(); }
                // Ground Y
                { float prev=cfg.GroundY;      ZUIBeginRow(ctx,"##sg_gy_r",ZFill(),ZPx(fh+4.f)); ZUISpacer(ctx,10.f); ZUIBox* lc=ZUIBeginColumn(ctx,"##sg_gy_l",ZPx(140.f),ZFill()); ZUILabel(ctx,"Ground Y",ctx->Theme.TextDefault); ZUIEndColumn(ctx); ZUISliderFloat(ctx,"##sg_gy_s",&cfg.GroundY,-100.f,100.f); ZUIEndRow(ctx); ZUISpacer(ctx,2.f); if(cfg.GroundY!=prev) mark_grid_dirty(); }
                // Max LOD
                { int prev=cfg.MaxLOD;         ZUIBeginRow(ctx,"##sg_ml_r",ZFill(),ZPx(fh+4.f)); ZUISpacer(ctx,10.f); ZUIBox* lc=ZUIBeginColumn(ctx,"##sg_ml_l",ZPx(140.f),ZFill()); ZUILabel(ctx,"Max LOD",ctx->Theme.TextDefault); ZUIEndColumn(ctx); ZUIDragInt(ctx,"##sg_ml_d",&cfg.MaxLOD,1.f,60.f); ZUIEndRow(ctx); ZUISpacer(ctx,2.f); if(cfg.MaxLOD!=prev) mark_grid_dirty(); }

                ZUISpacer(ctx, 6.f);

                // Color rows
                auto color_row = [&](const char* rk, const char* lk, const char* ck, const char* label, float col[4]) {
                    float p[4] = {col[0],col[1],col[2],col[3]};
                    ZUIBeginRow(ctx,rk,ZFill(),ZPx(fh+4.f)); ZUISpacer(ctx,10.f);
                    ZUIBox* lc=ZUIBeginColumn(ctx,lk,ZPx(140.f),ZFill()); ZUILabel(ctx,label,ctx->Theme.TextDefault); ZUIEndColumn(ctx);
                    ZUIColorEdit4(ctx,ck,col); ZUIEndRow(ctx); ZUISpacer(ctx,2.f);
                    if(col[0]!=p[0]||col[1]!=p[1]||col[2]!=p[2]||col[3]!=p[3]) mark_grid_dirty();
                };
                color_row("##sg_ct_r","##sg_ct_l","##sg_ct_c","Thin Lines",  cfg.ColorThin);
                color_row("##sg_ck_r","##sg_ck_l","##sg_ck_c","Thick Lines", cfg.ColorThick);
                color_row("##sg_cx_r","##sg_cx_l","##sg_cx_c","X Axis",      cfg.ColorXAxis);
                color_row("##sg_cz_r","##sg_cz_l","##sg_cz_c","Z Axis",      cfg.ColorZAxis);
            }
            else if (m_settings_page == 1) // Renderer
            {
                ZUISpacer(ctx, 12.f);
                ZUISpacer(ctx, 10.f); // align with sidebar padding
                ZUILabel(ctx, "No renderer settings yet.", ctx->Theme.TextDim);
            }
            else if (m_settings_page == 2) // Theme
            {
                ZUISpacer(ctx, 14.f);
                ZUISpacer(ctx, 10.f);
                ZUILabel(ctx, "Theme", ctx->Theme.TextDefault);
                ZUISpacer(ctx, 12.f);
                ZUIBeginRow(ctx, "##stg_thm_row", ZFill(), ZPx(80.f));
                ZUISpacer(ctx, 14.f);
                static int s_active_theme = 0;
                static const char* kThemeNames[2] = {"Dark", "Light"};
                static const char* kThemeKeys[2]  = {"##thm_0", "##thm_1"};
                for (int ti = 0; ti < 2; ++ti)
                {
                    bool tact = (s_active_theme == ti);
                    ZUIBox* tc = ZUIBeginColumn(ctx, kThemeKeys[ti], ZPx(110.f), ZFill());
                    tc->Flags  = tc->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable;
                    float bg   = (ti == 0) ? 0.10f : 0.88f;
                    ZUIBoxSetColor(tc, bg, bg, ti==0 ? 0.12f : 0.90f, 1.f);
                    ZUIBoxSetCornerRadius(tc, 4.f);
                    tc->BorderThickness = tact ? 2.f : 1.f;
                    tc->BorderColor[0]  = tact ? ctx->Theme.TabActiveBorder[0] : 0.28f;
                    tc->BorderColor[1]  = tact ? ctx->Theme.TabActiveBorder[1] : 0.28f;
                    tc->BorderColor[2]  = tact ? ctx->Theme.TabActiveBorder[2] : 0.32f;
                    tc->BorderColor[3]  = 1.f;
                    ZUISpacer(ctx, 20.f);
                    float lc[4] = {ti==0 ? 0.9f : 0.1f, ti==0 ? 0.9f : 0.1f, ti==0 ? 0.9f : 0.1f, 1.f};
                    ZUILabel(ctx, kThemeNames[ti], lc);
                    ZUISignal tsig = ZUISignalFromBox(ctx, tc);
                    ZUIEndColumn(ctx);
                    if (tsig.Flags & ZUI_SignalClicked) s_active_theme = ti;
                    ZUISpacer(ctx, 8.f);
                }
                ZUIEndRow(ctx);
            }
            else if (m_settings_page == 3 && ShellPanelManager) // Layout
            {
                static const char* kPanelNames[4] = {"Hierarchy", "Console", "Inspector", "Viewport"};
                static const char* kRowKeys[4]    = {"##lp_r0", "##lp_r1", "##lp_r2", "##lp_r3"};
                static const char* kCbKeys[4]     = {"##lp_c0", "##lp_c1", "##lp_c2", "##lp_c3"};

                ZUISpacer(ctx, 10.f);
                ZUILabel(ctx, "Panels", ctx->Theme.TextDefault);
                ZUISpacer(ctx, 8.f);

                for (int ni = 0; ni < 4; ++ni)
                {
                    bool vis  = ShellPanelManager->IsPanelVisible(kPanelNames[ni]);
                    bool prev = vis;
                    ZUIBeginRow(ctx, kRowKeys[ni], ZFill(), ZPx(fh + 4.f));
                    ZUISpacer(ctx, 10.f);
                    ZUICheckbox(ctx, kCbKeys[ni], &vis);
                    ZUISpacer(ctx, 6.f);
                    ZUILabel(ctx, kPanelNames[ni], ctx->Theme.TextDefault);
                    ZUIEndRow(ctx);
                    ZUISpacer(ctx, 2.f);
                    if (vis != prev)
                        ShellPanelManager->SetPanelVisible(kPanelNames[ni], vis);
                }

                ZUISpacer(ctx, 14.f);
                ZUIBeginRow(ctx, "##lp_rst_r", ZFill(), ZPx(fh + 4.f));
                ZUISpacer(ctx, 10.f);
                ZUISignal rst_sig = ZUIButton(ctx, "Reset Layout##lp_rst");
                if (rst_sig.Flags & ZUI_SignalClicked)
                    ShellPanelManager->ResetLayout();
                ZUIEndRow(ctx);
            }

            ZUIEndColumn(ctx); // content
            ZUIEndRow(ctx);    // body
            ZUIEndColumn(ctx); // window
        }

        ZUIEndColumn(ctx);
    }
} // namespace Tetragrama::Components
