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
            {
                char fk[48];
                snprintf(fk, 48, "##ept_%s", key);
                ZUIBox* f  = ZUIPushBox(ctx, fk, (uint32_t) strlen(fk), ZUI_None);
                f->Size[0] = ZFill();
                f->Size[1] = ZFill();
                ZUIPopBox(ctx);
            }
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
        const char* PlaceholderText = "No scene loaded"; ///< Shown when panel has no content.
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
        const char* PlaceholderText = "Viewport"; ///< Shown when panel has no content.
        void        BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            (void) rect;
            const float c[4] = {0.09f, 0.09f, 0.095f, 1.f};
            EmptyPanelBg(ctx, "##vp_bg", c, PlaceholderText);
        }
    };

    /// Inspector panel with VS Code-style collapsible sections.
    ///
    /// Interactions:
    ///   - Click header  → collapse/expand
    ///   - Drag sash     → resize adjacent sections (greedy cascade)
    ///   - Drag header   → reorder sections vertically
    ///     Source slot   → thin teal border placeholder
    ///     Target header → teal background highlight (VS Code drop-target model)
    ///     Ghost         → labeled header floating at cursor
    struct InspectorPanel : ZUIPanelView
    {
        InspectorPanel()
        {
            Title = "Inspector";
        }

        // Display-order arrays — index is display position.
        // m_order[di] = section type at position di  (0=Transform … 5=Script).
        // m_h and m_open are stored per display position and travel with the section on reorder.
        int                          m_order[6]     = {0, 1, 2, 3, 4, 5};
        bool                         m_open[6]      = {true, true, true, false, false, false};
        float                        m_h[6]         = {200.f, 80.f, 100.f, 80.f, 60.f, 80.f};
        static constexpr float       kMinH          = 30.f;

        // Drag-reorder transient state (not serialized)
        int                          m_drag_di      = -1;
        bool                         m_drag_active  = false;
        float                        m_drag_press_y = 0.f;
        int                          m_drop_slot    = 0;

        // Component data — keyed by section type (m_order[di])
        float                        m_position[3]  = {0.f, 0.f, 0.f};
        float                        m_rotation[3]  = {0.f, 0.f, 0.f};
        float                        m_scale[3]     = {1.f, 1.f, 1.f};
        bool                         m_cast_shadows = true, m_receive_shadows = true;
        float                        m_mass        = 1.f;
        bool                         m_use_gravity = true, m_is_kinematic = false;
        float                        m_light_intensity = 1.f, m_light_range = 10.f;
        float                        m_light_color[3] = {1.f, 1.f, 1.f};
        float                        m_audio_volume   = 1.f;
        bool                         m_audio_loop = false, m_audio_play_awake = true;

        static constexpr const char* kLabels[6] = {"Transform", "Mesh Renderer", "Rigid Body", "Lighting", "Audio Source", "Script"};

        void                         BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            using namespace ZEngine::UI;
            (void) rect;

            static constexpr int N      = 6;
            const float          kHdrH  = ZUIGetFrameHeight(ctx);
            const float          kSashH = 4.f;
            const float          fw     = rect[2] - rect[0] - 100.f - 16.f;

            // Open the scroll region first so ScrollY is available for hit detection
            ZUIBox*              bg     = ZUIBeginScrollRegion(ctx, "##insp_sr", ZFill(), ZFill());
            bg->Flags                   = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
            bg->EdgeSoftness          = 0.f;

            ZUIPersistentState* sr_ps = ZUIStateGetOrInsert(&ctx->StateStore, bg->Key);
            const float         scy   = sr_ps ? sr_ps->ScrollY : 0.f;

            // ── Drag state machine ─────────────────────────────────────────────

            if (ctx->MousePressed[0] && !m_drag_active)
            {
                float run = 0.f;
                m_drag_di = -1;
                for (int di = 0; di < N; di++)
                {
                    float y0 = rect[1] - scy + run;
                    float y1 = y0 + kHdrH;
                    if (ctx->MousePos[1] >= y0 && ctx->MousePos[1] < y1)
                    {
                        m_drag_di      = di;
                        m_drag_press_y = ctx->MousePos[1];
                        break;
                    }
                    run += kHdrH + (m_open[di] ? m_h[di] : 0.f) + kSashH;
                }
            }

            if (m_drag_di >= 0 && ctx->MouseDown[0] && !m_drag_active)
            {
                if (fabsf(ctx->MousePos[1] - m_drag_press_y) > 8.f)
                    m_drag_active = true;
            }

            if (m_drag_active)
            {
                float delta = ctx->MousePos[1] - m_drag_press_y;
                m_drop_slot = m_drag_di + (int) roundf(delta / (kHdrH + kSashH));
                if (m_drop_slot < 0)
                    m_drop_slot = 0;
                if (m_drop_slot > N)
                    m_drop_slot = N;
            }

            if (ctx->MouseReleased[0] && m_drag_di >= 0)
            {
                if (m_drag_active && m_drop_slot != m_drag_di && m_drop_slot != m_drag_di + 1)
                {
                    int   old_s    = m_order[m_drag_di];
                    float old_h    = m_h[m_drag_di];
                    bool  old_open = m_open[m_drag_di];
                    for (int i = m_drag_di; i < N - 1; i++)
                    {
                        m_order[i] = m_order[i + 1];
                        m_h[i]     = m_h[i + 1];
                        m_open[i]  = m_open[i + 1];
                    }
                    int ins = (m_drop_slot > m_drag_di) ? m_drop_slot - 1 : m_drop_slot;
                    if (ins < 0)
                        ins = 0;
                    if (ins >= N)
                        ins = N - 1;
                    for (int i = N - 1; i > ins; i--)
                    {
                        m_order[i] = m_order[i - 1];
                        m_h[i]     = m_h[i - 1];
                        m_open[i]  = m_open[i - 1];
                    }
                    m_order[ins] = old_s;
                    m_h[ins]     = old_h;
                    m_open[ins]  = old_open;
                }
                m_drag_di     = -1;
                m_drag_active = false;
            }

            // ── Section list ───────────────────────────────────────────────────

            // Drop-target highlight color — teal fill passed as bg_color to the target header.
            // Only applied when the drop would actually change the order.
            bool        drop_changes = m_drag_active && m_drop_slot != m_drag_di && m_drop_slot != m_drag_di + 1;
            float       drop_hi[4]   = {ctx->Theme.TabActiveBorder[0], ctx->Theme.TabActiveBorder[1], ctx->Theme.TabActiveBorder[2], 0.22f};

            const char* ghost_label  = nullptr;
            auto        Row          = [&](const char* rk) {
                ZUIBeginRow(ctx, rk, ZFill(), ZPx(kHdrH));
                ZUISpacer(ctx, 8.f);
            };
            auto EndRow = [&]() { ZUIEndRow(ctx); };

            for (int di = 0; di < N; di++)
            {
                int  s      = m_order[di];
                bool is_src = m_drag_active && (di == m_drag_di);
                char ck[32], sk[16];

                if (is_src)
                {
                    // Source slot: thin teal border placeholder — section is "in the air"
                    ghost_label         = kLabels[s];
                    ZUIBox* ph          = ZUIPushBox(ctx, "##drag_ph", 9, ZUI_DrawBorder);
                    ph->Size[0]         = ZFill();
                    ph->Size[1]         = ZPx(kHdrH);
                    ph->EdgeSoftness    = 0.f;
                    ph->BorderColor[0]  = ctx->Theme.TabActiveBorder[0];
                    ph->BorderColor[1]  = ctx->Theme.TabActiveBorder[1];
                    ph->BorderColor[2]  = ctx->Theme.TabActiveBorder[2];
                    ph->BorderColor[3]  = 0.30f;
                    ph->BorderThickness = 1.f;
                    ZUIPopBox(ctx);
                }
                else
                {
                    // Drop target: highlight the header row of the section we will land before
                    const float* hdr_col = (drop_changes && m_drop_slot == di) ? drop_hi : nullptr;
                    ZUICollapsingHeader(ctx, kLabels[s], &m_open[di], hdr_col);

                    if (m_open[di])
                    {
                        snprintf(ck, sizeof(ck), "##cc%d", di);
                        ZUIBox* c       = ZUIBeginColumn(ctx, ck, ZFill(), ZPx(m_h[di]));
                        c->Flags        = c->Flags | ZUI_ClipChildren;
                        c->EdgeSoftness = 0.f;
                        ZUISpacer(ctx, 2.f);
                        switch (s)
                        {
                            case 0: // Transform
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
                                break;
                            case 1: // Mesh Renderer
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
                                break;
                            case 2: // Rigid Body
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
                                break;
                            case 3: // Lighting
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
                                break;
                            case 4: // Audio Source
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
                                break;
                            case 5: // Script
                                ZUILabel(ctx, "No script attached", ctx->Theme.TextDim);
                                break;
                        }
                        ZUIEndColumn(ctx);
                    }
                }

                if (di < N - 1)
                {
                    snprintf(sk, sizeof(sk), "##sk%d", di);
                    ZUIPaneSash(ctx, sk, m_h, m_open, N, di, kMinH);
                }
            }

            // Drop at end of list: teal bar after the last section (no header exists there)
            if (drop_changes && m_drop_slot == N)
            {
                ZUIBox* ind       = ZUIPushBox(ctx, "##dind_end", 9, ZUI_DrawBackground);
                ind->Size[0]      = ZFill();
                ind->Size[1]      = ZPx(kHdrH);
                ind->EdgeSoftness = 0.f;
                ZUIBoxSetColor(ind, drop_hi[0], drop_hi[1], drop_hi[2], drop_hi[3]);
                ZUIPopBox(ctx);
            }

            ZUIEndScrollRegion(ctx);

            // Ghost header — floated after the scroll region so it renders on top
            if (ghost_label)
            {
                ZUIBox* ghost       = ZUIBeginRow(ctx, "##insp_ghost", ZFill(), ZPx(kHdrH));
                ghost->Flags        = ghost->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY;
                ghost->FloatPos[0]  = 0.f;
                ghost->FloatPos[1]  = ctx->MousePos[1] - rect[1] - kHdrH * 0.5f;
                ghost->EdgeSoftness = 0.f;
                ZUIBoxSetColorArr(ghost, ctx->Theme.TitleBgActive);
                ghost->BorderColor[0]  = ctx->Theme.TabActiveBorder[0];
                ghost->BorderColor[1]  = ctx->Theme.TabActiveBorder[1];
                ghost->BorderColor[2]  = ctx->Theme.TabActiveBorder[2];
                ghost->BorderColor[3]  = 0.8f;
                ghost->BorderThickness = 1.f;
                ZUISpacer(ctx, ZUIGetFramePadX(ctx));
                ZUILabel(ctx, ghost_label, ctx->Theme.TextDefault);
                ZUIEndRow(ctx);
            }
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

            ZUIBeginRow(ctx, "##con_row1", ZFill(), ZPx(ZUIGetFrameHeight(ctx)));
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, "Search", ctx->Theme.TextDim);
            ZUISpacer(ctx, 8.f);
            ZUITextField(ctx, "##con_search", m_search, sizeof(m_search), rect[2] - rect[0] - 24.f);
            ZUISpacer(ctx, 8.f);
            ZUIEndRow(ctx);

            ZUISpacer(ctx, 6.f);

            ZUIBeginRow(ctx, "##con_row2", ZFill(), ZPx(ZUIGetFrameHeight(ctx)));
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, "Filter", ctx->Theme.TextDim);
            ZUISpacer(ctx, 8.f);
            ZUITextField(ctx, "##con_filter", m_filter, sizeof(m_filter), rect[2] - rect[0] - 24.f);
            ZUISpacer(ctx, 8.f);
            ZUIEndRow(ctx);

            ZUISpacer(ctx, 6.f);

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
