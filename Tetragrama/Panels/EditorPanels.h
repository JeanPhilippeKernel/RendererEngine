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
        bool  m_transform_open  = true;
        bool  m_mesh_open       = true;
        bool  m_rigidbody_open  = false;

        // Transform values
        float m_position[3]     = {0.f, 0.f, 0.f};
        float m_rotation[3]     = {0.f, 0.f, 0.f};
        float m_scale[3]        = {1.f, 1.f, 1.f};

        // Mesh Renderer values
        bool  m_cast_shadows    = true;
        bool  m_receive_shadows = true;

        // Rigid Body values
        float m_mass            = 1.f;
        bool  m_use_gravity     = true;
        bool  m_is_kinematic    = false;

        void  BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            using namespace ZEngine::UI;
            (void) rect;

            ZUIBox* bg = ZUIBeginColumn(ctx, "##insp_bg", ZFill(), ZFill());
            bg->Flags  = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
            bg->EdgeSoftness    = 0.f;

            const float label_w = 90.f;
            const float field_w = rect[2] - rect[0] - label_w - 16.f;

            auto        Row     = [&](const char* rk) {
                ZUIBeginRow(ctx, rk, ZFill(), ZPx(ZUIGetFrameHeight(ctx)));
                ZUISpacer(ctx, 8.f);
            };
            auto EndRow = [&]() { ZUIEndRow(ctx); };

            // Transform
            ZUISpacer(ctx, 2.f);
            if (ZUICollapsingHeader(ctx, "Transform", &m_transform_open))
            {
                ZUISpacer(ctx, 2.f);
                Row("##ip_pos");
                ZUILabel(ctx, "Position", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUIDragFloat3(ctx, "##ip_position", m_position, 0.1f, field_w / 3.f);
                ZUISpacer(ctx, 8.f);
                EndRow();

                Row("##ip_rot");
                ZUILabel(ctx, "Rotation", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUIDragFloat3(ctx, "##ip_rotation", m_rotation, 0.5f, field_w / 3.f);
                ZUISpacer(ctx, 8.f);
                EndRow();

                Row("##ip_scl");
                ZUILabel(ctx, "Scale", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUIDragFloat3(ctx, "##ip_scale", m_scale, 0.05f, field_w / 3.f);
                ZUISpacer(ctx, 8.f);
                EndRow();
                ZUISpacer(ctx, 4.f);
            }

            // Mesh Renderer
            ZUISeparator(ctx);
            ZUISpacer(ctx, 2.f);
            if (ZUICollapsingHeader(ctx, "Mesh Renderer", &m_mesh_open))
            {
                ZUISpacer(ctx, 2.f);
                Row("##ip_mcshadow");
                ZUILabel(ctx, "Cast Shadows", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUICheckbox(ctx, "##ip_cast_shad", &m_cast_shadows);
                ZUISpacer(ctx, 8.f);
                EndRow();

                Row("##ip_mrshadow");
                ZUILabel(ctx, "Recv Shadows", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUICheckbox(ctx, "##ip_recv_shad", &m_receive_shadows);
                ZUISpacer(ctx, 8.f);
                EndRow();
                ZUISpacer(ctx, 4.f);
            }

            // Rigid Body
            ZUISeparator(ctx);
            ZUISpacer(ctx, 2.f);
            if (ZUICollapsingHeader(ctx, "Rigid Body", &m_rigidbody_open))
            {
                ZUISpacer(ctx, 2.f);
                Row("##ip_mass");
                ZUILabel(ctx, "Mass", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUIDragFloat(ctx, "##ip_mass_val", &m_mass, 0.1f, field_w);
                ZUISpacer(ctx, 8.f);
                EndRow();

                Row("##ip_grav");
                ZUILabel(ctx, "Use Gravity", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUICheckbox(ctx, "##ip_gravity", &m_use_gravity);
                ZUISpacer(ctx, 8.f);
                EndRow();

                Row("##ip_kine");
                ZUILabel(ctx, "Kinematic", ctx->Theme.TextDim);
                ZUISpacer(ctx, 4.f);
                ZUICheckbox(ctx, "##ip_kinematic", &m_is_kinematic);
                ZUISpacer(ctx, 8.f);
                EndRow();
                ZUISpacer(ctx, 4.f);
            }

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
