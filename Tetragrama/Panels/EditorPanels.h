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

        // Section open/close state
        bool                   m_transform_open  = true;
        bool                   m_mesh_open       = true;
        bool                   m_rigidbody_open  = true;

        // Section content heights (logical px). Drag the strip at the top of each header
        // to resize: UP = that section grows (covers section above), DOWN = shrinks
        // (cascades to push next section when min height reached).
        float                  m_heights[3]      = {100.f, 80.f, 100.f}; // Transform, MeshRenderer, RigidBody
        static constexpr float kMinSectionH      = 30.f;

        // Transform values
        float                  m_position[3]     = {0.f, 0.f, 0.f};
        float                  m_rotation[3]     = {0.f, 0.f, 0.f};
        float                  m_scale[3]        = {1.f, 1.f, 1.f};

        // Mesh Renderer values
        bool                   m_cast_shadows    = true;
        bool                   m_receive_shadows = true;

        // Rigid Body values
        float                  m_mass            = 1.f;
        bool                   m_use_gravity     = true;
        bool                   m_is_kinematic    = false;

        // Apply cascade resize between section[above] and section[below].
        // delta_y > 0 = drag DOWN = above grows, below shrinks.
        // delta_y < 0 = drag UP   = above shrinks, below grows.
        // Cascades: when a section hits kMinSectionH, excess delta spills to its neighbour.
        void                   ApplyResize(int above_idx, float delta_y)
        {
            if (above_idx < 0 || above_idx >= 2)
                return;
            float& A  = m_heights[above_idx];
            float& B  = m_heights[above_idx + 1];
            A        += delta_y;
            B        -= delta_y;
            if (A < kMinSectionH)
            {
                float spill = kMinSectionH - A;
                A           = kMinSectionH;
                // Cascade upward: try to compensate from prev-prev section
                if (above_idx > 0)
                    m_heights[above_idx - 1] -= spill;
            }
            if (B < kMinSectionH)
            {
                float spill = kMinSectionH - B;
                B           = kMinSectionH;
                // Cascade downward: try to compensate from next section
                int next    = above_idx + 2;
                if (next < 3)
                    m_heights[next] -= spill;
            }
            // Enforce min on all sections
            for (float& h : m_heights)
                if (h < kMinSectionH)
                    h = kMinSectionH;
        }

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            using namespace ZEngine::UI;
            (void) rect;

            ZUIBox* bg = ZUIBeginColumn(ctx, "##insp_bg", ZFill(), ZFill());
            bg->Flags  = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
            bg->EdgeSoftness    = 0.f;

            const float field_w = rect[2] - rect[0] - 100.f - 16.f;
            auto        Row     = [&](const char* rk) {
                ZUIBeginRow(ctx, rk, ZFill(), ZPx(ZUIGetFrameHeight(ctx)));
                ZUISpacer(ctx, 8.f);
            };
            auto  EndRow = [&]() { ZUIEndRow(ctx); };
            float dy     = 0.f;

            // Transform — no drag (first section, nothing above to push against)
            ZUISpacer(ctx, 2.f);
            if (ZUICollapsingHeader(ctx, "Transform", &m_transform_open))
            {
                ZUIBeginScrollRegion(ctx, "##sr_t", ZFill(), ZPx(m_heights[0]));
                ZUISpacer(ctx, 2.f);
                Row("##ip_pos");
                ZUILabel(ctx, "Position", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUIDragFloat3(ctx, "##ip_pos", m_position, 0.1f, field_w / 3.f);
                ZUISpacer(ctx, 8.f);
                EndRow();
                Row("##ip_rot");
                ZUILabel(ctx, "Rotation", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUIDragFloat3(ctx, "##ip_rot", m_rotation, 0.5f, field_w / 3.f);
                ZUISpacer(ctx, 8.f);
                EndRow();
                Row("##ip_scl");
                ZUILabel(ctx, "Scale", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUIDragFloat3(ctx, "##ip_scl", m_scale, 0.05f, field_w / 3.f);
                ZUISpacer(ctx, 8.f);
                EndRow();
                ZUIEndScrollRegion(ctx);
            }

            // Mesh Renderer — drag strip adjusts boundary 0 (Transform ↕ Mesh)
            ZUISeparator(ctx);
            ZUISpacer(ctx, 2.f);
            dy = 0.f;
            if (ZUICollapsingHeader(ctx, "Mesh Renderer", &m_mesh_open, &dy))
            {
                ZUIBeginScrollRegion(ctx, "##sr_m", ZFill(), ZPx(m_heights[1]));
                ZUISpacer(ctx, 2.f);
                Row("##ip_mcs");
                ZUILabel(ctx, "Cast Shadows", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUICheckbox(ctx, "##ip_cs", &m_cast_shadows);
                ZUISpacer(ctx, 8.f);
                EndRow();
                Row("##ip_mrs");
                ZUILabel(ctx, "Recv Shadows", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUICheckbox(ctx, "##ip_rs", &m_receive_shadows);
                ZUISpacer(ctx, 8.f);
                EndRow();
                ZUIEndScrollRegion(ctx);
            }
            if (dy != 0.f)
                ApplyResize(0, dy); // boundary 0: Transform(0) ↕ Mesh(1)

            // Rigid Body — drag strip adjusts boundary 1 (Mesh ↕ Rigid Body)
            ZUISeparator(ctx);
            ZUISpacer(ctx, 2.f);
            dy = 0.f;
            if (ZUICollapsingHeader(ctx, "Rigid Body", &m_rigidbody_open, &dy))
            {
                ZUIBeginScrollRegion(ctx, "##sr_rb", ZFill(), ZPx(m_heights[2]));
                ZUISpacer(ctx, 2.f);
                Row("##ip_mass");
                ZUILabel(ctx, "Mass", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUIDragFloat(ctx, "##ip_mv", &m_mass, 0.1f, field_w);
                ZUISpacer(ctx, 8.f);
                EndRow();
                Row("##ip_grav");
                ZUILabel(ctx, "Use Gravity", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUICheckbox(ctx, "##ip_ug", &m_use_gravity);
                ZUISpacer(ctx, 8.f);
                EndRow();
                Row("##ip_kine");
                ZUILabel(ctx, "Kinematic", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUICheckbox(ctx, "##ip_km", &m_is_kinematic);
                ZUISpacer(ctx, 8.f);
                EndRow();
                ZUIEndScrollRegion(ctx);
            }
            if (dy != 0.f)
                ApplyResize(1, dy); // boundary 1: Mesh(1) ↕ RigidBody(2)

            ZUIEndColumn(ctx);
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
