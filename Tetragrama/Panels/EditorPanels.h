#pragma once
#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIWidgets.h>

namespace Tetragrama::Panels
{
    using namespace ZEngine::UI;

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
        const char* PlaceholderText = "No scene loaded";
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
        const char* PlaceholderText = "Viewport";
        void        BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            (void) rect;
            const float c[4] = {0.09f, 0.09f, 0.095f, 1.f};
            EmptyPanelBg(ctx, "##vp_bg", c, PlaceholderText);
        }
    };

    /// Inspector panel — 6 collapsible sections.
    ///
    /// Drop-slot visual rules (determined by the TARGET section state):
    ///   Expanded target, cursor in upper half → 2px teal divider at TOP of that section
    ///   Expanded target, cursor in lower half → 2px teal divider at BOTTOM of that section
    ///   Collapsed target                      → teal fill on the header row
    ///
    /// Sash appears only after EXPANDED sections (no content to resize when collapsed).
    struct InspectorPanel : ZUIPanelView
    {
        InspectorPanel()
        {
            Title = "Inspector";
        }

        // Display-order arrays — index = display position.
        // m_order, m_h, m_open all travel together when sections are reordered.
        int                          m_order[6]     = {0, 1, 2, 3, 4, 5};
        bool                         m_open[6]      = {true, true, true, false, false, false};
        float                        m_h[6]         = {200.f, 80.f, 100.f, 80.f, 60.f, 80.f};
        static constexpr float       kMinH          = 30.f;

        // Drag-reorder transient state
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

        // Height of a section in the scroll content, accounting for source placeholder.
        float                        SectionRunH(int di, float kHdrH, float kSashH) const
        {
            if (di == m_drag_di)
                return kHdrH; // source shows header-height placeholder only
            return kHdrH + (m_open[di] ? m_h[di] + kSashH : 0.f);
        }

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            using namespace ZEngine::UI;
            (void) rect;

            static constexpr int N      = 6;
            const float          kHdrH  = ZUIGetFrameHeight(ctx);
            const float          kSashH = 4.f;
            const float          fw     = rect[2] - rect[0] - 100.f - 16.f;

            ZUIBox*              bg     = ZUIBeginScrollRegion(ctx, "##insp_sr", ZFill(), ZFill());
            bg->Flags                   = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
            bg->EdgeSoftness          = 0.f;

            ZUIPersistentState* sr_ps = ZUIStateGetOrInsert(&ctx->StateStore, bg->Key);
            const float         scy   = sr_ps ? sr_ps->ScrollY : 0.f;

            // ── Drag detection: press on a header Y-range ──────────────────────
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
                    run += SectionRunH(di, kHdrH, kSashH);
                }
            }

            if (m_drag_di >= 0 && ctx->MouseDown[0] && !m_drag_active)
            {
                if (fabsf(ctx->MousePos[1] - m_drag_press_y) > 8.f)
                    m_drag_active = true;
            }

            // ── Drop slot: cursor position in scroll content ───────────────────
            // Expanded target → upper half = slot before it, lower half = slot after it.
            // Collapsed target → always slot before it.
            if (m_drag_active)
            {
                float cursor_rel = ctx->MousePos[1] - rect[1] + scy;
                float run        = 0.f;
                m_drop_slot      = 0;

                for (int di = 0; di < N; di++)
                {
                    float sh  = SectionRunH(di, kHdrH, kSashH);
                    float top = run;
                    float bot = run + sh;

                    if (cursor_rel >= top && cursor_rel < bot)
                    {
                        if (di != m_drag_di && m_open[di])
                        {
                            // Expanded: split top/bottom half
                            float mid   = top + sh * 0.5f;
                            m_drop_slot = (cursor_rel < mid) ? di : di + 1;
                        }
                        else
                        {
                            m_drop_slot = di;
                        }
                        break;
                    }
                    else if (cursor_rel < top)
                    {
                        m_drop_slot = di;
                        break;
                    }
                    else
                    {
                        m_drop_slot = di + 1;
                    }
                    run += sh;
                }

                if (m_drop_slot < 0)
                    m_drop_slot = 0;
                if (m_drop_slot > N)
                    m_drop_slot = N;
            }

            // ── Commit reorder on release ──────────────────────────────────────
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

            // drop_changes: drop would move the section to a new position
            bool         drop_changes = m_drag_active && m_drop_slot != m_drag_di && m_drop_slot != m_drag_di + 1;

            // Shared teal color for all drop visuals
            const float* tb           = ctx->Theme.TabActiveBorder;
            float        hi[4]        = {tb[0], tb[1], tb[2], 0.22f}; // header highlight (collapsed target)

            // Helper: render 2px bright teal divider line in flow
            auto         DrawDivider  = [&](const char* k) {
                ZUIBox* d       = ZUIPushBox(ctx, k, (uint32_t) strlen(k), ZUI_DrawBackground);
                d->Size[0]      = ZFill();
                d->Size[1]      = ZPx(2.f);
                d->EdgeSoftness = 0.f;
                ZUIBoxSetColor(d, tb[0], tb[1], tb[2], 1.f);
                ZUIPopBox(ctx);
            };

            // ── Section list ───────────────────────────────────────────────────
            const char* ghost_label = nullptr;
            auto        Row         = [&](const char* rk) {
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
                    // Source: thin teal border placeholder
                    ghost_label         = kLabels[s];
                    ZUIBox* ph          = ZUIPushBox(ctx, "##drag_ph", 9, ZUI_DrawBorder);
                    ph->Size[0]         = ZFill();
                    ph->Size[1]         = ZPx(kHdrH);
                    ph->EdgeSoftness    = 0.f;
                    ph->BorderColor[0]  = tb[0];
                    ph->BorderColor[1]  = tb[1];
                    ph->BorderColor[2]  = tb[2];
                    ph->BorderColor[3]  = 0.30f;
                    ph->BorderThickness = 1.f;
                    ZUIPopBox(ctx);
                    // No sash for source placeholder
                    continue;
                }

                // Drop visual BEFORE this section
                if (drop_changes && m_drop_slot == di)
                {
                    if (m_open[di])
                        DrawDivider("##div_top"); // expanded target: bright divider at top
                    // collapsed target: header highlight below — no pre-divider
                }

                // Header — teal fill when it's a collapsed drop target
                {
                    const float* hdr_col = (drop_changes && m_drop_slot == di && !m_open[di]) ? hi : nullptr;
                    ZUICollapsingHeader(ctx, kLabels[s], &m_open[di], hdr_col);
                }

                // Content
                if (m_open[di])
                {
                    snprintf(ck, sizeof(ck), "##cc%d", di);
                    ZUIBox* c       = ZUIBeginColumn(ctx, ck, ZFill(), ZPx(m_h[di]));
                    c->Flags        = c->Flags | ZUI_ClipChildren;
                    c->EdgeSoftness = 0.f;
                    ZUISpacer(ctx, 2.f);
                    switch (s)
                    {
                        case 0:
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
                        case 1:
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
                        case 2:
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
                        case 3:
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
                        case 4:
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
                        case 5:
                            ZUILabel(ctx, "No script attached", ctx->Theme.TextDim);
                            break;
                    }
                    ZUIEndColumn(ctx);

                    // Sash — only after expanded sections
                    if (di < N - 1)
                    {
                        snprintf(sk, sizeof(sk), "##sk%d", di);
                        ZUIPaneSash(ctx, sk, m_h, m_open, N, di, kMinH);
                    }

                    // Drop visual AFTER this expanded section (cursor was in lower half)
                    if (drop_changes && m_drop_slot == di + 1)
                        DrawDivider("##div_bot");
                }
            }

            // Drop at end of list
            if (drop_changes && m_drop_slot == N)
                DrawDivider("##div_end");

            ZUIEndScrollRegion(ctx);

            // Ghost — header row floating at cursor, painted on top of scroll content
            if (ghost_label)
            {
                ZUIBox* ghost       = ZUIBeginRow(ctx, "##insp_ghost", ZFill(), ZPx(kHdrH));
                ghost->Flags        = ghost->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY;
                ghost->FloatPos[0]  = 0.f;
                ghost->FloatPos[1]  = ctx->MousePos[1] - rect[1] - kHdrH * 0.5f;
                ghost->EdgeSoftness = 0.f;
                ZUIBoxSetColorArr(ghost, ctx->Theme.TitleBgActive);
                ghost->BorderColor[0]  = tb[0];
                ghost->BorderColor[1]  = tb[1];
                ghost->BorderColor[2]  = tb[2];
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
        int                          m_level       = 1;
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
