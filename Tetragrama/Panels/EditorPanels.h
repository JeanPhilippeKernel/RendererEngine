#pragma once
#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIWidgets.h>

namespace Tetragrama::Panels
{
    using namespace ZEngine::UI;

    // ── Shared helpers ────────────────────────────────────────────────────────

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
                snprintf(fk, sizeof(fk), "##ept_%s", key);
                ZUIBox* f  = ZUIPushBox(ctx, fk, (uint32_t) strlen(fk), ZUI_None);
                f->Size[0] = ZFill();
                f->Size[1] = ZFill();
                ZUIPopBox(ctx);
            }
            {
                char lk[48];
                snprintf(lk, sizeof(lk), "##epl_%s", key);
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
                snprintf(fk, sizeof(fk), "##epb_%s", key);
                ZUIBox* f  = ZUIPushBox(ctx, fk, (uint32_t) strlen(fk), ZUI_None);
                f->Size[0] = ZFill();
                f->Size[1] = ZFill();
                ZUIPopBox(ctx);
            }
        }
        ZUIEndColumn(ctx);
    }

    // ── Hierarchy panel ───────────────────────────────────────────────────────
    //
    // Tree view of scene entities — up to 9 levels of nesting.
    // ZUITreeNode uses the VS Code chevron (∨/›) built-in.
    // Indentation via wrapper column Padding[0] = depth * IndentSpacing.
    //
    struct HierarchyPanel : ZUIPanelView
    {
        struct Node
        {
            const char* name;
            int         parent; // -1 = root
        };

        static constexpr int  kN         = 22;

        // Sample scene — deepest path: World → Player → Armature → Hips → Spine
        //                              → Chest → Shoulder R → UpperArm R
        //                              → LowerArm R → Hand R  (depth 9)
        static constexpr Node kNodes[kN] = {
            {      "World", -1}, // 0  depth 0
            {     "Camera",  0}, // 1  depth 1
            {"Main Camera",  1}, // 2  depth 2
            {   "Lighting",  0}, // 3  depth 1
            {"Directional",  3}, // 4  depth 2
            {"Point Light",  3}, // 5  depth 2
            {     "Player",  0}, // 6  depth 1
            {   "Armature",  6}, // 7  depth 2
            {       "Hips",  7}, // 8  depth 3
            {      "Spine",  8}, // 9  depth 4
            {      "Chest",  9}, // 10 depth 5
            { "Shoulder R", 10}, // 11 depth 6
            { "UpperArm R", 11}, // 12 depth 7
            { "LowerArm R", 12}, // 13 depth 8
            {     "Hand R", 13}, // 14 depth 9  ← max depth
            { "Shoulder L", 10}, // 15 depth 6
            { "UpperArm L", 15}, // 16 depth 7
            { "LowerArm L", 16}, // 17 depth 8
            {     "Hand L", 17}, // 18 depth 9
            {"Environment",  0}, // 19 depth 1
            {     "Ground", 19}, // 20 depth 2
            {      "Trees", 19}, // 21 depth 2
        };

        bool m_open[kN] = {true, true, false, true, false, false, true, true, true, true, true, true, true, true, false, true, true, true, false, true, false, false};
        int  m_selected = -1;

        HierarchyPanel()
        {
            Title = "Hierarchy";
        }

        int Depth(int i) const
        {
            int d = 0, p = kNodes[i].parent;
            while (p >= 0)
            {
                d++;
                p = kNodes[p].parent;
            }
            return d;
        }

        bool HasChildren(int i) const
        {
            for (int j = 0; j < kN; j++)
                if (kNodes[j].parent == i)
                    return true;
            return false;
        }

        bool AncestorCollapsed(int i) const
        {
            int p = kNodes[i].parent;
            while (p >= 0)
            {
                if (!m_open[p])
                    return true;
                p = kNodes[p].parent;
            }
            return false;
        }

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            using namespace ZEngine::UI;
            (void) rect;

            const float hdrH = ZUIGetFrameHeight(ctx);

            ZUIBox*     bg   = ZUIBeginScrollRegion(ctx, "##hier_sr", ZFill(), ZFill());
            bg->Flags        = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
            bg->EdgeSoftness = 0.f;

            ZUISpacer(ctx, 4.f);

            for (int i = 0; i < kN; i++)
            {
                if (AncestorCollapsed(i))
                    continue;

                float indent   = (float) Depth(i) * ctx->Style.IndentSpacing;
                bool  has_chld = HasChildren(i);
                bool  is_sel   = (m_selected == i);
                char  ck[32];
                snprintf(ck, sizeof(ck), "##hn%d", i);

                // Wrapper column — provides indentation via left padding
                ZUIBox* col       = ZUIBeginColumn(ctx, ck, ZFill(), ZPx(hdrH));
                col->Padding[0]   = indent;
                col->EdgeSoftness = 0.f;
                if (is_sel)
                {
                    col->Flags = col->Flags | ZUI_DrawBackground;
                    ZUIBoxSetColorArr(col, ctx->Theme.RowSelectedBg);
                }

                if (has_chld)
                {
                    ZUISignal sig = ZUITreeNode(ctx, kNodes[i].name, &m_open[i]);
                    if (sig.Flags & ZUI_SignalClicked)
                        m_selected = i;
                }
                else
                {
                    // Leaf — indent to align label with parent tree-node text
                    bool sel = is_sel;
                    if (ZUISelectable(ctx, kNodes[i].name, &sel, ZPx(hdrH)))
                        m_selected = i;
                }

                ZUIEndColumn(ctx);
            }

            ZUIEndScrollRegion(ctx);
        }
    };

    struct ViewportPanel : ZUIPanelView
    {
        ViewportPanel()
        {
            Title = "Viewport";
        }
        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            (void) rect;
            const float c[4] = {0.09f, 0.09f, 0.095f, 1.f};
            EmptyPanelBg(ctx, "##vp_bg", c, "Viewport");
        }
    };

    // ── Inspector panel ───────────────────────────────────────────────────────
    //
    // Six mini-panel sections in a vertical stack.
    // Each section is a ZUICollapsingHeader (click = collapse/expand, no close).
    // Drag header to reorder using panel-docking visual helpers.
    // Sash (resize) appears only below expanded sections.
    //
    struct InspectorPanel : ZUIPanelView
    {
        // ── Config ─────────────────────────────────────────────────────────
        static constexpr int         N              = 6;
        static constexpr float       kMinH          = 80.f; // minimum expanded content height
        static constexpr float       kSashH         = 6.f;  // resize grip height

        // ── Section display state (indexed by display position) ────────────
        int                          m_order[N]     = {0, 1, 2, 3, 4, 5}; // section type at each display slot
        bool                         m_open[N]      = {true, true, true, false, false, false};
        float                        m_h[N]         = {200.f, 80.f, 100.f, 80.f, 80.f, 80.f};

        // ── Drag-reorder state ─────────────────────────────────────────────
        int                          m_drag_di      = -1;
        bool                         m_drag_active  = false;
        float                        m_drag_acc_y   = 0.f;
        int                          m_drop_slot    = 0;
        bool                         m_drop_is_bot  = false;

        // ── Component data (by section type) ──────────────────────────────
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

        static constexpr const char* kLabels[N] = {"Transform", "Mesh Renderer", "Rigid Body", "Lighting", "Audio Source", "Script"};

        InspectorPanel()
        {
            Title = "Inspector";
        }

        // Content height for section at display position di, clamped to kMinH
        float ContentH(int di) const
        {
            return fmaxf(m_h[di], kMinH);
        }

        // Total scroll-content height for section di (placeholder height when source)
        float SectionRunH(int di, float hdrH) const
        {
            if (di == m_drag_di)
                return hdrH;
            float h    = hdrH + (m_open[di] ? ContentH(di) : 0.f);
            float sash = (m_open[di] && di < N - 1) ? kSashH : 0.f;
            return h + sash;
        }

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            using namespace ZEngine::UI;
            (void) rect;

            const float hdrH  = ZUIGetFrameHeight(ctx);
            const float sectW = rect[2] - rect[0];
            const float fw    = sectW - 100.f - 16.f;

            // ── Open scroll region ────────────────────────────────────────
            ZUIBox*     sr    = ZUIBeginScrollRegion(ctx, "##insp_sr", ZFill(), ZFill());
            sr->Flags         = sr->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(sr, ctx->Theme.PanelBg);
            sr->EdgeSoftness         = 0.f;

            ZUIPersistentState* srps = ZUIStateGetOrInsert(&ctx->StateStore, sr->Key);
            const float         scy  = srps ? srps->ScrollY : 0.f;

            // ── Drop slot from cursor ─────────────────────────────────────
            if (m_drag_active)
            {
                float crel    = ctx->MousePos[1] - rect[1] + scy;
                float run     = 0.f;
                m_drop_slot   = 0;
                m_drop_is_bot = false;
                for (int di = 0; di < N; di++)
                {
                    float sh  = SectionRunH(di, hdrH);
                    float top = run, bot = run + sh;
                    if (crel >= top && crel < bot)
                    {
                        if (di != m_drag_di && m_open[di])
                        {
                            bool bot_half = crel >= top + sh * 0.5f;
                            m_drop_slot   = bot_half ? di + 1 : di;
                            m_drop_is_bot = bot_half;
                        }
                        else
                        {
                            m_drop_slot = di;
                        }
                        break;
                    }
                    m_drop_slot = (crel < top) ? di : di + 1;
                    if (crel < top)
                        break;
                    run += sh;
                }
                m_drop_slot = (m_drop_slot < 0) ? 0 : (m_drop_slot > N ? N : m_drop_slot);
            }

            // Cancel drag if cursor exits the inspector panel rect — sections are vertical-only
            if (m_drag_active)
            {
                bool inside = ctx->MousePos[0] >= rect[0] && ctx->MousePos[0] <= rect[2] && ctx->MousePos[1] >= rect[1] && ctx->MousePos[1] <= rect[3];
                if (!inside)
                {
                    m_drag_di     = -1;
                    m_drag_active = false;
                    m_drag_acc_y  = 0.f;
                    m_drop_is_bot = false;
                }
            }

            // ── Commit reorder ────────────────────────────────────────────
            if (ctx->MouseReleased[0] && m_drag_di >= 0)
            {
                if (m_drag_active && m_drop_slot != m_drag_di && m_drop_slot != m_drag_di + 1)
                {
                    int   os = m_order[m_drag_di];
                    float oh = m_h[m_drag_di];
                    bool  oo = m_open[m_drag_di];
                    for (int i = m_drag_di; i < N - 1; i++)
                    {
                        m_order[i] = m_order[i + 1];
                        m_h[i]     = m_h[i + 1];
                        m_open[i]  = m_open[i + 1];
                    }
                    int ins = (m_drop_slot > m_drag_di) ? m_drop_slot - 1 : m_drop_slot;
                    ins     = (ins < 0) ? 0 : (ins >= N ? N - 1 : ins);
                    for (int i = N - 1; i > ins; i--)
                    {
                        m_order[i] = m_order[i - 1];
                        m_h[i]     = m_h[i - 1];
                        m_open[i]  = m_open[i - 1];
                    }
                    m_order[ins] = os;
                    m_h[ins]     = oh;
                    m_open[ins]  = oo;
                }
                m_drag_di     = -1;
                m_drag_active = false;
                m_drag_acc_y  = 0.f;
                m_drop_is_bot = false;
            }

            bool            drop_chg  = m_drag_active && m_drop_slot != m_drag_di && m_drop_slot != m_drag_di + 1;
            const float*    tb        = ctx->Theme.TabActiveBorder;

            // ── Section loop ──────────────────────────────────────────────
            const char*     ghost_lbl = nullptr;
            constexpr float kTopPad   = 6.f;
            float           run_y     = kTopPad;
            ZUISpacer(ctx, kTopPad);

            auto Row = [&](const char* rk) {
                ZUIBeginRow(ctx, rk, ZFill(), ZPx(hdrH));
                ZUISpacer(ctx, 8.f);
            };
            auto EndRow = [&]() { ZUIEndRow(ctx); };

            for (int di = 0; di < N; di++)
            {
                int  s      = m_order[di];
                bool is_src = m_drag_active && (di == m_drag_di);
                char ck[32];

                // Source slot: border placeholder
                if (is_src)
                {
                    ghost_lbl           = kLabels[s];
                    ZUIBox* ph          = ZUIPushBox(ctx, "##ph", 4, ZUI_DrawBorder);
                    ph->Size[0]         = ZFill();
                    ph->Size[1]         = ZPx(hdrH);
                    ph->EdgeSoftness    = 0.f;
                    ph->BorderColor[0]  = tb[0];
                    ph->BorderColor[1]  = tb[1];
                    ph->BorderColor[2]  = tb[2];
                    ph->BorderColor[3]  = 0.30f;
                    ph->BorderThickness = 1.f;
                    ZUIPopBox(ctx);
                    run_y += hdrH;
                    continue;
                }

                // Drop zone visual — before this section
                bool  top_tgt = drop_chg && (m_drop_slot == di) && m_open[di] && !m_drop_is_bot;
                bool  bot_tgt = drop_chg && (m_drop_slot == di + 1) && m_open[di] && m_drop_is_bot;
                bool  col_tgt = drop_chg && (m_drop_slot == di) && !m_open[di] && !m_drop_is_bot;

                float sec_top = run_y - scy;

                if (top_tgt)
                    ZUIDockDividerH(ctx, "##dtop");

                // Header — collapsed target gets teal highlight
                {
                    float        hi[4] = {tb[0], tb[1], tb[2], 0.22f};
                    const float* hcol  = col_tgt ? hi : nullptr;
                    ZUISignal    sig   = ZUICollapsingHeader(ctx, kLabels[s], &m_open[di], hcol);

                    // Drag detection via signal — suppressed naturally when the divider hit zone
                    // owns ctx->ActiveKey, since ZUI_SignalHeld requires ActiveKey == header->Key.
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

                // Content
                if (m_open[di])
                {
                    float cH     = ContentH(di);
                    float half_h = (hdrH + cH) * 0.5f;

                    snprintf(ck, sizeof(ck), "##cc%d", di);
                    ZUIBox* c       = ZUIBeginColumn(ctx, ck, ZFill(), ZPx(cH));
                    c->Flags        = c->Flags | ZUI_ClipChildren;
                    c->EdgeSoftness = 0.f;
                    ZUISpacer(ctx, 2.f);

                    switch (s)
                    {
                        case 0: // Transform
                            Row("##r_pos");
                            ZUILabel(ctx, "Position", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUIDragFloat3(ctx, "##pos", m_position, 0.1f, fw / 3.f);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            Row("##r_rot");
                            ZUILabel(ctx, "Rotation", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUIDragFloat3(ctx, "##rot", m_rotation, 0.5f, fw / 3.f);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            Row("##r_scl");
                            ZUILabel(ctx, "Scale", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUIDragFloat3(ctx, "##scl", m_scale, 0.05f, fw / 3.f);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            break;
                        case 1: // Mesh Renderer
                            Row("##r_cs");
                            ZUILabel(ctx, "Cast Shadows", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUICheckbox(ctx, "##cs", &m_cast_shadows);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            Row("##r_rs");
                            ZUILabel(ctx, "Recv Shadows", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUICheckbox(ctx, "##rs", &m_receive_shadows);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            break;
                        case 2: // Rigid Body
                            Row("##r_ms");
                            ZUILabel(ctx, "Mass", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUIDragFloat(ctx, "##ms", &m_mass, 0.1f, fw);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            Row("##r_ug");
                            ZUILabel(ctx, "Use Gravity", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUICheckbox(ctx, "##ug", &m_use_gravity);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            Row("##r_km");
                            ZUILabel(ctx, "Kinematic", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUICheckbox(ctx, "##km", &m_is_kinematic);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            break;
                        case 3: // Lighting
                            Row("##r_li");
                            ZUILabel(ctx, "Intensity", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUIDragFloat(ctx, "##li", &m_light_intensity, 0.05f, fw);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            Row("##r_lr");
                            ZUILabel(ctx, "Range", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUIDragFloat(ctx, "##lr", &m_light_range, 0.5f, fw);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            Row("##r_lc");
                            ZUILabel(ctx, "Color", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUIDragFloat3(ctx, "##lc", m_light_color, 0.01f, fw / 3.f);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            break;
                        case 4: // Audio Source
                            Row("##r_av");
                            ZUILabel(ctx, "Volume", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUIDragFloat(ctx, "##av", &m_audio_volume, 0.02f, fw);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            Row("##r_al");
                            ZUILabel(ctx, "Loop", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUICheckbox(ctx, "##al", &m_audio_loop);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            Row("##r_pa");
                            ZUILabel(ctx, "Play Awake", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUICheckbox(ctx, "##pa", &m_audio_play_awake);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            break;
                        case 5: // Script
                            ZUILabel(ctx, "No script attached", ctx->Theme.TextDim);
                            break;
                    }

                    ZUIEndColumn(ctx);

                    // Sash — after expanded sections only
                    if (di < N - 1)
                    {
                        char sk[16], vk[16];
                        snprintf(sk, sizeof(sk), "##sk%d", di);
                        snprintf(vk, sizeof(vk), "##sv%d", di);

                        ZUIBox* sash       = ZUIPushBox(ctx, sk, (uint32_t) strlen(sk), ZUI_Clickable);
                        sash->Size[0]      = ZFill();
                        sash->Size[1]      = ZPx(kSashH);
                        sash->EdgeSoftness = 0.f;

                        bool shot          = (ctx->HotKey == sash->Key) || (ctx->ActiveKey == sash->Key);
                        if (shot)
                            ctx->ResizeCursor = 2;

                        ZUIBox* vis       = ZUIPushBox(ctx, vk, (uint32_t) strlen(vk), ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
                        vis->Size[0]      = ZFill();
                        vis->Size[1]      = ZPx(2.f);
                        vis->FloatPos[0]  = 0.f;
                        vis->FloatPos[1]  = (kSashH - 2.f) * 0.5f;
                        vis->EdgeSoftness = shot ? (ctx->ActiveKey == sash->Key ? 3.f : 2.f) : 0.f;
                        if (shot)
                            ZUIBoxSetColor(vis, tb[0], tb[1], tb[2], ctx->ActiveKey == sash->Key ? 0.70f : 0.50f);
                        else
                            ZUIBoxSetColor(vis, ctx->Theme.Separator[0], ctx->Theme.Separator[1], ctx->Theme.Separator[2], ctx->Theme.Separator[3]);
                        ZUIPopBox(ctx);

                        ZUISignal ssig = ZUISignalFromBox(ctx, sash);
                        ZUIPopBox(ctx);

                        if ((ssig.Flags & ZUI_SignalHeld) && fabsf(ssig.DragDelta[1]) > 0.05f)
                            m_h[di] = fmaxf(m_h[di] + ssig.DragDelta[1], kMinH);
                    }

                    // Drop zone teal fill — floated sibling after section wrapper
                    if (top_tgt || bot_tgt)
                    {
                        float fill_y = sec_top + (bot_tgt ? half_h : 0.f);
                        char  dzk[16];
                        snprintf(dzk, sizeof(dzk), "##dz%d", di);
                        ZUIDropZoneFill(ctx, dzk, 0.f, fill_y, sectW, half_h);
                    }

                    // 2px divider after section (bottom-half target)
                    if (bot_tgt)
                        ZUIDockDividerH(ctx, "##dbot");
                }

                run_y += SectionRunH(di, hdrH);
            }

            // Divider at end of list
            if (drop_chg && m_drop_slot == N)
                ZUIDockDividerH(ctx, "##dend");

            ZUIEndScrollRegion(ctx);

            // Ghost — floated after scroll region, panel-content coords.
            // cx is centered in the inspector width so the ghost never renders over other panels.
            // cy is clamped to stay within the panel height.
            if (ghost_lbl)
            {
                float panel_h = rect[3] - rect[1];
                float ghost_h = hdrH + ctx->Style.TabGhostContentH;
                float cx      = sectW * 0.5f; // centered — ghost stays inside inspector
                float cy      = ctx->MousePos[1] - rect[1];
                cy            = fmaxf(ghost_h * 0.5f, fminf(cy, panel_h - ghost_h * 0.5f));
                ZUIDockGhostHeader(ctx, "##ghost", ghost_lbl, cx, cy);
            }
        }
    };

    // ── Console panel ─────────────────────────────────────────────────────────

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

            ZUIBeginRow(ctx, "##con_r1", ZFill(), ZPx(ZUIGetFrameHeight(ctx)));
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, "Search", ctx->Theme.TextDim);
            ZUISpacer(ctx, 8.f);
            ZUITextField(ctx, "##con_search", m_search, sizeof(m_search), rect[2] - rect[0] - 24.f);
            ZUISpacer(ctx, 8.f);
            ZUIEndRow(ctx);

            ZUISpacer(ctx, 6.f);

            ZUIBeginRow(ctx, "##con_r2", ZFill(), ZPx(ZUIGetFrameHeight(ctx)));
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, "Filter", ctx->Theme.TextDim);
            ZUISpacer(ctx, 8.f);
            ZUITextField(ctx, "##con_filter", m_filter, sizeof(m_filter), rect[2] - rect[0] - 24.f);
            ZUISpacer(ctx, 8.f);
            ZUIEndRow(ctx);

            ZUISpacer(ctx, 6.f);

            ZUIBeginRow(ctx, "##con_r3", ZFill(), ZPx(ZUIGetFrameHeight(ctx)));
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

            ZUIEndColumn(ctx);
        }
    };

} // namespace Tetragrama::Panels
