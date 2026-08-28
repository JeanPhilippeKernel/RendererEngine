#pragma once
#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIWidgets.h>

namespace Tetragrama::Panels
{
    using namespace ZEngine::UI;

    // Full-panel background with a vertically and horizontally centered placeholder label.
    // Pass msg=nullptr to show no label (bare background only).
    static void EmptyPanelBg(ZUIContext* ctx, const char* key, const float col[4], const char* msg)
    {
        ZUIBox* bg = ZUIBeginColumn(ctx, key, ZFill(), ZFill());
        bg->Flags  = bg->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(bg, col);
        bg->EdgeSoftness = 0.f;
        if (msg && msg[0])
        {
            // top fill
            {
                char fk[48];
                snprintf(fk, 48, "##ept_%s", key);
                ZUIBox* f  = ZUIPushBox(ctx, fk, (uint32_t) strlen(fk), ZUI_None);
                f->Size[0] = ZFill();
                f->Size[1] = ZFill();
                ZUIPopBox(ctx);
            }
            // full-width centered label
            {
                char lk[48];
                snprintf(lk, 48, "##epl_%s", key);
                uint32_t mlen     = (uint32_t) strlen(msg);
                ZUIBox*  lbl      = ZUIPushBox(ctx, lk, (uint32_t) strlen(lk), ZUI_DrawText);
                lbl->Size[0]      = ZFill();
                lbl->Size[1]      = ZText();
                lbl->TextAlign    = ZUITextAlign::Center;
                lbl->Label        = ZUIPushStr(&ctx->FrameArena, msg, mlen);
                lbl->TextColor[0] = ctx->Theme.TextDim[0];
                lbl->TextColor[1] = ctx->Theme.TextDim[1];
                lbl->TextColor[2] = ctx->Theme.TextDim[2];
                lbl->TextColor[3] = ctx->Theme.TextDim[3];
                ZUIPopBox(ctx);
            }
            // bottom fill
            {
                char fk[48];
                snprintf(fk, 48, "##epb_%s", key);
                ZUIBox* f  = ZUIPushBox(ctx, fk, (uint32_t) strlen(fk), ZUI_None);
                f->Size[0] = ZFill();
                f->Size[1] = ZFill();
                ZUIPopBox(ctx);
            }
        }
        ZUIEndColumn(ctx);
    }

    struct HierarchyPanel : ZUIPanelView
    {
        HierarchyPanel()
        {
            Title = "Hierarchy";
        }
        const char* PlaceholderText = "No scene loaded"; ///< Shown when panel has no content. Set to nullptr to hide.
        void        BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            (void) rect;
            EmptyPanelBg(ctx, "##hier_bg", ctx->Theme.PanelBg, PlaceholderText);
        }
    };

    struct ViewportPanel : ZUIPanelView
    {
        ViewportPanel()
        {
            Title = "Viewport";
        }
        const char* PlaceholderText = "Viewport"; ///< Shown when panel has no content. Set to nullptr to hide.
        void        BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            (void) rect;
            const float c[4] = {0.09f, 0.09f, 0.095f, 1.f};
            EmptyPanelBg(ctx, "##vp_bg", c, PlaceholderText);
        }
    };

    struct InspectorPanel : ZUIPanelView
    {
        InspectorPanel()
        {
            Title = "Inspector";
        }

        // Section open/close state (6 sections total)
        bool                   m_open[6]      = {true, true, true, false, false, false};
        // 0=Transform 1=MeshRenderer 2=RigidBody 3=Lighting 4=AudioSource 5=Script

        // Content heights. Transform (index 0) is the "flex" section — large initial
        // height so it fills available space. Others start smaller.
        float                  m_h[6]         = {200.f, 80.f, 100.f, 80.f, 60.f, 80.f};
        static constexpr float kMinH          = 30.f;

        // Component values
        float                  m_position[3]  = {0.f, 0.f, 0.f};
        float                  m_rotation[3]  = {0.f, 0.f, 0.f};
        float                  m_scale[3]     = {1.f, 1.f, 1.f};
        bool                   m_cast_shadows = true, m_receive_shadows = true;
        float                  m_mass        = 1.f;
        bool                   m_use_gravity = true, m_is_kinematic = false;
        float                  m_light_intensity = 1.f, m_light_range = 10.f;
        float                  m_light_color[3] = {1.f, 1.f, 1.f};
        float                  m_audio_volume   = 1.f;
        bool                   m_audio_loop = false, m_audio_play_awake = true;

        void                   BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            using namespace ZEngine::UI;
            (void) rect;

            // VS Code layout: one parent scroll region wrapping all sections.
            // Resize is handled by ZUIPaneSash between sections, not by dragging headers.
            // Header = click-to-toggle only (pointer cursor, no resize affordance).
            ZUIBox* bg = ZUIBeginScrollRegion(ctx, "##insp_sr", ZFill(), ZFill());
            bg->Flags  = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
            bg->EdgeSoftness         = 0.f;

            static constexpr int N   = 6; // total number of sections
            const float          fw  = rect[2] - rect[0] - 100.f - 16.f;
            auto                 Row = [&](const char* rk) {
                ZUIBeginRow(ctx, rk, ZFill(), ZPx(ZUIGetFrameHeight(ctx)));
                ZUISpacer(ctx, 8.f);
            };
            auto EndRow     = [&]() { ZUIEndRow(ctx); };

            // Helper: render the fixed-height content area for an open section
            auto ContentBox = [&](const char* k, int idx) -> bool {
                if (!m_open[idx])
                    return false;
                ZUIBox* c       = ZUIBeginColumn(ctx, k, ZFill(), ZPx(m_h[idx]));
                c->Flags        = c->Flags | ZUI_ClipChildren;
                c->EdgeSoftness = 0.f;
                return true;
            };
            auto EndContent = [&]() { ZUIEndColumn(ctx); };

            // Transform (index 0) — the "flex" section: large height, fills remaining space
            ZUICollapsingHeader(ctx, "Transform", &m_open[0]);
            if (ContentBox("##ct", 0))
            {
                ZUISpacer(ctx, 2.f);
                Row("##ip_pos");
                ZUILabel(ctx, "Position", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUIDragFloat3(ctx, "##ip_p", m_position, 0.1f, fw / 3.f);
                ZUISpacer(ctx, 8.f);
                EndRow();
                Row("##ip_rot");
                ZUILabel(ctx, "Rotation", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUIDragFloat3(ctx, "##ip_r", m_rotation, 0.5f, fw / 3.f);
                ZUISpacer(ctx, 8.f);
                EndRow();
                Row("##ip_scl");
                ZUILabel(ctx, "Scale", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUIDragFloat3(ctx, "##ip_s", m_scale, 0.05f, fw / 3.f);
                ZUISpacer(ctx, 8.f);
                EndRow();
                EndContent();
            }
            ZUIPaneSash(ctx, "##s01", m_h, m_open, N, 0, kMinH); // sash between 0↕1

            // Mesh Renderer (index 1)
            ZUICollapsingHeader(ctx, "Mesh Renderer", &m_open[1]);
            if (ContentBox("##cm", 1))
            {
                ZUISpacer(ctx, 2.f);
                Row("##ip_cs");
                ZUILabel(ctx, "Cast Shadows", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUICheckbox(ctx, "##ip_csc", &m_cast_shadows);
                ZUISpacer(ctx, 8.f);
                EndRow();
                Row("##ip_rs");
                ZUILabel(ctx, "Recv Shadows", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUICheckbox(ctx, "##ip_rsc", &m_receive_shadows);
                ZUISpacer(ctx, 8.f);
                EndRow();
                EndContent();
            }
            ZUIPaneSash(ctx, "##s12", m_h, m_open, N, 1, kMinH); // sash between 1↕2

            // Rigid Body (index 2)
            ZUICollapsingHeader(ctx, "Rigid Body", &m_open[2]);
            if (ContentBox("##crb", 2))
            {
                ZUISpacer(ctx, 2.f);
                Row("##ip_ms");
                ZUILabel(ctx, "Mass", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUIDragFloat(ctx, "##ip_mv", &m_mass, 0.1f, fw);
                ZUISpacer(ctx, 8.f);
                EndRow();
                Row("##ip_ug");
                ZUILabel(ctx, "Use Gravity", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUICheckbox(ctx, "##ip_ugc", &m_use_gravity);
                ZUISpacer(ctx, 8.f);
                EndRow();
                Row("##ip_km");
                ZUILabel(ctx, "Kinematic", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUICheckbox(ctx, "##ip_kmc", &m_is_kinematic);
                ZUISpacer(ctx, 8.f);
                EndRow();
                EndContent();
            }
            ZUIPaneSash(ctx, "##s23", m_h, m_open, N, 2, kMinH); // sash between 2↕3

            // Lighting (index 3) — collapsed by default
            ZUICollapsingHeader(ctx, "Lighting", &m_open[3]);
            if (ContentBox("##clt", 3))
            {
                ZUISpacer(ctx, 2.f);
                Row("##ip_li");
                ZUILabel(ctx, "Intensity", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUIDragFloat(ctx, "##ip_in", &m_light_intensity, 0.05f, fw);
                ZUISpacer(ctx, 8.f);
                EndRow();
                Row("##ip_lr");
                ZUILabel(ctx, "Range", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUIDragFloat(ctx, "##ip_rn", &m_light_range, 0.5f, fw);
                ZUISpacer(ctx, 8.f);
                EndRow();
                Row("##ip_lc");
                ZUILabel(ctx, "Color", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUIDragFloat3(ctx, "##ip_lco", m_light_color, 0.01f, fw / 3.f);
                ZUISpacer(ctx, 8.f);
                EndRow();
                EndContent();
            }
            ZUIPaneSash(ctx, "##s34", m_h, m_open, N, 3, kMinH); // sash between 3↕4

            // Audio Source (index 4)
            ZUICollapsingHeader(ctx, "Audio Source", &m_open[4]);
            if (ContentBox("##cau", 4))
            {
                ZUISpacer(ctx, 2.f);
                Row("##ip_av");
                ZUILabel(ctx, "Volume", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUIDragFloat(ctx, "##ip_vol", &m_audio_volume, 0.02f, fw);
                ZUISpacer(ctx, 8.f);
                EndRow();
                Row("##ip_al");
                ZUILabel(ctx, "Loop", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUICheckbox(ctx, "##ip_lp", &m_audio_loop);
                ZUISpacer(ctx, 8.f);
                EndRow();
                Row("##ip_pa");
                ZUILabel(ctx, "Play Awake", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUICheckbox(ctx, "##ip_paw", &m_audio_play_awake);
                ZUISpacer(ctx, 8.f);
                EndRow();
                EndContent();
            }
            ZUIPaneSash(ctx, "##s45", m_h, m_open, N, 4, kMinH); // sash between 4↕5

            // Script (index 5) — placeholder, collapsed by default
            ZUICollapsingHeader(ctx, "Script", &m_open[5]);
            if (ContentBox("##csc", 5))
            {
                ZUISpacer(ctx, 4.f);
                ZUILabel(ctx, "No script attached", ctx->Theme.TextDim);
                EndContent();
            }

            ZUIEndScrollRegion(ctx);
        }
    };

    struct ConsolePanel : ZUIPanelView
    {
        ConsolePanel()
        {
            Title = "Console";
        }

        char                         m_search[256] = {};
        char                         m_filter[256] = "ZEngine";
        int                          m_level       = 1; // 0=Info 1=Warning 2=Error
        static constexpr const char* kLevels[]     = {"Info", "Warning", "Error"};

        void                         BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            using namespace ZEngine::UI;
            (void) rect;
            ZUIBox* bg = ZUIBeginColumn(ctx, "##con_bg", ZFill(), ZFill());
            bg->Flags  = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
            bg->EdgeSoftness = 0.f;

            ZUISpacer(ctx, 8.f);

            // Search field — test text selection + undo/redo
            ZUIBeginRow(ctx, "##con_row1", ZFill(), ZPx(ZUIGetFrameHeight(ctx)));
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, "Search", ctx->Theme.TextDim);
            ZUISpacer(ctx, 8.f);
            ZUITextField(ctx, "##con_search", m_search, sizeof(m_search), rect[2] - rect[0] - 24.f);
            ZUISpacer(ctx, 8.f);
            ZUIEndRow(ctx);

            ZUISpacer(ctx, 6.f);

            // Filter field — second text field (undo stack is independent)
            ZUIBeginRow(ctx, "##con_row2", ZFill(), ZPx(ZUIGetFrameHeight(ctx)));
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, "Filter", ctx->Theme.TextDim);
            ZUISpacer(ctx, 8.f);
            ZUITextField(ctx, "##con_filter", m_filter, sizeof(m_filter), rect[2] - rect[0] - 24.f);
            ZUISpacer(ctx, 8.f);
            ZUIEndRow(ctx);

            ZUISpacer(ctx, 6.f);

            // Level combo — test keyboard navigation
            ZUIBeginRow(ctx, "##con_row3", ZFill(), ZPx(ZUIGetFrameHeight(ctx)));
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, "Level ", ctx->Theme.TextDim);
            ZUISpacer(ctx, 4.f);
            if (ZUIBeginCombo(ctx, "##con_level", kLevels[m_level], ZPx(120.f)))
            {
                for (int i = 0; i < 3; ++i)
                    if (ZUIComboItem(ctx, kLevels[i], m_level == i))
                        m_level = i;
                ZUIEndCombo(ctx);
            }
            ZUIEndRow(ctx);

            ZUISpacer(ctx, 10.f);
            ZUISeparator(ctx);
            ZUISpacer(ctx, 6.f);

            // Help text
            ZUIBeginRow(ctx, "##con_hint1", ZFill(), ZPx(ZUIGetFrameHeight(ctx)));
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, "Shift+Arrow   select text", ctx->Theme.TextDim);
            ZUIEndRow(ctx);
            ZUIBeginRow(ctx, "##con_hint2", ZFill(), ZPx(ZUIGetFrameHeight(ctx)));
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, "Ctrl+A        select all", ctx->Theme.TextDim);
            ZUIEndRow(ctx);
            ZUIBeginRow(ctx, "##con_hint3", ZFill(), ZPx(ZUIGetFrameHeight(ctx)));
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, "Ctrl+Z / Y    undo / redo", ctx->Theme.TextDim);
            ZUIEndRow(ctx);
            ZUIBeginRow(ctx, "##con_hint4", ZFill(), ZPx(ZUIGetFrameHeight(ctx)));
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, "Arrow Up/Down navigate combo", ctx->Theme.TextDim);
            ZUIEndRow(ctx);

            ZUIEndColumn(ctx);
        }
    };

} // namespace Tetragrama::Panels
