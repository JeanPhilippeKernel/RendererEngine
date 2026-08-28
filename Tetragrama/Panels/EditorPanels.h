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

    /// Inspector panel — 6 mini-panel sections in a vertical stack.
    ///
    /// Each ZUICollapsingHeader is a mini-panel: click = expand/collapse, no close button.
    /// Drag-to-reorder uses the same visual system as panel docking:
    ///   ZUIDropZoneFill   → shared teal drop-zone fill (identical to panel drop zones)
    ///   ZUIDockDividerH   → shared 2px insertion boundary line
    ///   ZUIDockGhostHeader → shared 3-layer drag ghost (shadow + body + header row)
    ///
    /// Drop zones are floated as SIBLINGS in the scroll region (correct Z-order, no
    /// first-child hack). Position is computed from running_y and scy.
    struct InspectorPanel : ZUIPanelView
    {
        InspectorPanel()
        {
            Title = "Inspector";
        }

        static constexpr float       kMinSectionH   = 80.f; // no expanded section may be shorter than this

        int                          m_order[6]     = {0, 1, 2, 3, 4, 5};
        bool                         m_open[6]      = {true, true, true, false, false, false};
        float                        m_h[6]         = {200.f, 80.f, 100.f, 80.f, 80.f, 80.f};

        int                          m_drag_di      = -1;
        bool                         m_drag_active  = false;
        float                        m_drag_acc_y   = 0.f;
        int                          m_drop_slot    = 0;
        bool                         m_drop_is_bot  = false; // true when slot was set by the lower half of an expanded section

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

        float                        ContentH(int di) const
        {
            return fmaxf(m_h[di], kMinSectionH);
        }

        float SectionRunH(int di, float hH) const
        {
            return (di == m_drag_di) ? hH : hH + (m_open[di] ? ContentH(di) : 0.f);
        }

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            using namespace ZEngine::UI;
            (void) rect;

            static constexpr int N     = 6;
            const float          kHdrH = ZUIGetFrameHeight(ctx);
            const float          fw    = rect[2] - rect[0] - 100.f - 16.f;
            const float          sectW = rect[2] - rect[0];

            ZUIBox*              bg    = ZUIBeginScrollRegion(ctx, "##insp_sr", ZFill(), ZFill());
            bg->Flags                  = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
            bg->EdgeSoftness          = 0.f;

            ZUIPersistentState* sr_ps = ZUIStateGetOrInsert(&ctx->StateStore, bg->Key);
            const float         scy   = sr_ps ? sr_ps->ScrollY : 0.f;

            // ── Drop slot (expanded = top/bottom half; collapsed = before) ────
            if (m_drag_active)
            {
                float cursor_rel = ctx->MousePos[1] - rect[1] + scy;
                float run        = 0.f;
                m_drop_slot      = 0;
                m_drop_is_bot    = false;
                for (int di = 0; di < N; di++)
                {
                    float sh  = SectionRunH(di, kHdrH);
                    float top = run;
                    float bot = run + sh;
                    if (cursor_rel >= top && cursor_rel < bot)
                    {
                        if (di != m_drag_di && m_open[di])
                        {
                            bool lower    = cursor_rel >= top + sh * 0.5f;
                            m_drop_slot   = lower ? di + 1 : di;
                            m_drop_is_bot = lower;
                        }
                        else
                        {
                            m_drop_slot   = di;
                            m_drop_is_bot = false;
                        }
                        break;
                    }
                    m_drop_slot   = (cursor_rel < top) ? di : di + 1;
                    m_drop_is_bot = false;
                    if (cursor_rel < top)
                        break;
                    run += sh;
                }
                if (m_drop_slot < 0)
                    m_drop_slot = 0;
                if (m_drop_slot > N)
                    m_drop_slot = N;
            }

            // ── Commit reorder on release ─────────────────────────────────────
            if (ctx->MouseReleased[0])
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
                m_drag_acc_y  = 0.f;
                m_drop_is_bot = false;
            }

            bool        drop_changes = m_drag_active && m_drop_slot != m_drag_di && m_drop_slot != m_drag_di + 1;

            // ── Section list ──────────────────────────────────────────────────
            const char* ghost_label  = nullptr;
            auto        Row          = [&](const char* rk) {
                ZUIBeginRow(ctx, rk, ZFill(), ZPx(kHdrH));
                ZUISpacer(ctx, 8.f);
            };
            auto  EndRow    = [&]() { ZUIEndRow(ctx); };

            float running_y = 0.f; // tracks section tops in scroll content

            for (int di = 0; di < N; di++)
            {
                int   s      = m_order[di];
                bool  is_src = m_drag_active && (di == m_drag_di);
                char  ck[32], bk[32], dzk[32];

                // Drop zone position for this section (relative to scroll region top)
                float sec_top_y  = running_y - scy;
                float full_sec_h = kHdrH + ContentH(di);
                float half_h     = full_sec_h * 0.5f;

                if (is_src)
                {
                    ghost_label         = kLabels[s];
                    ZUIBox* ph          = ZUIPushBox(ctx, "##drag_ph", 9, ZUI_DrawBorder);
                    ph->Size[0]         = ZFill();
                    ph->Size[1]         = ZPx(kHdrH);
                    ph->EdgeSoftness    = 0.f;
                    const float* tb     = ctx->Theme.TabActiveBorder;
                    ph->BorderColor[0]  = tb[0];
                    ph->BorderColor[1]  = tb[1];
                    ph->BorderColor[2]  = tb[2];
                    ph->BorderColor[3]  = 0.30f;
                    ph->BorderThickness = 1.f;
                    ZUIPopBox(ctx);
                    running_y += kHdrH;
                    continue;
                }

                // m_drop_is_bot discriminates top vs bottom half — exactly one indicator per boundary.
                bool top_tgt = drop_changes && (m_drop_slot == di) && m_open[di] && !m_drop_is_bot;
                bool bot_tgt = drop_changes && (m_drop_slot == di + 1) && m_open[di] && m_drop_is_bot;
                bool col_tgt = drop_changes && (m_drop_slot == di) && !m_open[di] && !m_drop_is_bot;

                // 2px divider before header (expanded top-half target)
                if (top_tgt)
                    ZUIDockDividerH(ctx, "##div_top");

                // Section border wrapper (1px panel-style border around header + content)
                {
                    float sec_h = kHdrH + (m_open[di] ? ContentH(di) : 0.f);
                    snprintf(bk, sizeof(bk), "##sb%d", di);
                    ZUIBox* sec          = ZUIBeginColumn(ctx, bk, ZFill(), ZPx(sec_h));
                    sec->Flags           = sec->Flags | ZUI_DrawBorder;
                    sec->BorderColor[0]  = ctx->Theme.Separator[0];
                    sec->BorderColor[1]  = ctx->Theme.Separator[1];
                    sec->BorderColor[2]  = ctx->Theme.Separator[2];
                    sec->BorderColor[3]  = ctx->Theme.Separator[3];
                    sec->BorderThickness = 1.f;
                    sec->EdgeSoftness    = 0.f;
                }

                // Header — teal fill for collapsed drop target
                {
                    float        hi[4]   = {ctx->Theme.TabActiveBorder[0], ctx->Theme.TabActiveBorder[1], ctx->Theme.TabActiveBorder[2], 0.22f};
                    const float* hdr_col = col_tgt ? hi : nullptr;
                    ZUISignal    sig     = ZUICollapsingHeader(ctx, kLabels[s], &m_open[di], hdr_col);

                    if (!m_drag_active && (sig.Flags & ZUI_SignalHeld))
                    {
                        m_drag_acc_y += sig.DragDelta[1];
                        if (fabsf(m_drag_acc_y) > 8.f)
                        {
                            m_drag_active = true;
                            m_drag_di     = di;
                            m_drag_acc_y  = 0.f;
                        }
                    }
                }

                if (m_open[di])
                {
                    snprintf(ck, sizeof(ck), "##cc%d", di);
                    ZUIBox* c       = ZUIBeginColumn(ctx, ck, ZFill(), ZPx(ContentH(di)));
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

                    ZUIEndColumn(ctx); // content column
                }

                ZUIEndColumn(ctx); // section border wrapper

                // Drop zone fills — floated as SIBLINGS in the scroll region AFTER the
                // section wrapper (renders on top). Position uses running_y + scy offset.
                // Teal fill — top half starts at section top, bottom half starts at section midpoint
                if (top_tgt || bot_tgt)
                {
                    snprintf(dzk, sizeof(dzk), "##dz%d", di);
                    float fill_y = sec_top_y + (bot_tgt ? half_h : 0.f);
                    ZUIDropZoneFill(ctx, dzk, 0.f, fill_y, sectW, half_h);
                }

                // 2px divider after section (bottom-half drop target)
                if (bot_tgt)
                    ZUIDockDividerH(ctx, "##div_bot");

                running_y += kHdrH + (m_open[di] ? ContentH(di) : 0.f);
            }

            // 2px divider at end of list
            if (drop_changes && m_drop_slot == N)
                ZUIDockDividerH(ctx, "##div_end");

            ZUIEndScrollRegion(ctx);

            // Ghost — uses shared ZUIDockGhostHeader, cursor in panel-content coords
            if (ghost_label)
            {
                float panel_h  = rect[3] - rect[1];
                float ghost_h  = ZUIGetFrameHeight(ctx) + ctx->Style.TabGhostContentH;
                float cursor_x = ctx->MousePos[0] - rect[0];
                float cursor_y = ctx->MousePos[1] - rect[1];
                if (cursor_y < ghost_h * 0.5f)
                    cursor_y = ghost_h * 0.5f;
                if (cursor_y > panel_h - ghost_h * 0.5f)
                    cursor_y = panel_h - ghost_h * 0.5f;
                ZUIDockGhostHeader(ctx, "##insp_ghost", ghost_label, cursor_x, cursor_y);
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
