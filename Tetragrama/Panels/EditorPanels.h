#pragma once
#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace Tetragrama::Panels
{
    using namespace ZEngine::UI;

    // ================================================================
    // HierarchyPanel — VS Code Explorer style
    // ================================================================
    struct HierarchyPanel : ZUIPanelView
    {
        HierarchyPanel() { Title = "Hierarchy"; }

        int  selected      = -1;
        bool states_seeded = false;
        char search_buf[64] = {};

        struct Actor {
            const char* name;
            int         parent;
            float       icon[4];
        };
        static constexpr Actor k_actors[] = {
            { "World",            -1, {0.50f,0.50f,0.90f,1.f} },
            { "DefaultScene",      0, {0.30f,0.78f,0.30f,1.f} },
            { "DirectionalLight",  1, {0.98f,0.85f,0.25f,1.f} },
            { "MainCamera",        1, {0.25f,0.78f,0.98f,1.f} },
            { "Cube",              1, {0.72f,0.50f,0.98f,1.f} },
            { "Sphere",            1, {0.72f,0.50f,0.98f,1.f} },
            { "Ground",            1, {0.72f,0.50f,0.98f,1.f} },
            { "SkyAtmosphere",     1, {0.30f,0.90f,0.90f,1.f} },
        };
        static constexpr int kCount = 8;

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            (void)rect;

            // Section header bar
            {
                ZUIBox* hdr = ZUIBeginRow(ctx, "##hdr", ZFill(), ZPx(24.f));
                hdr->Flags  = hdr->Flags | ZUI_DrawBackground;
                ZUIBoxSetColorArr(hdr, ctx->Theme.PanelBgAlt);
                hdr->EdgeSoftness = 0.f;

                ZUISpacer(ctx, 8.f);
                ZUILabel(ctx, "HIERARCHY", ctx->Theme.TextDim, ZUIFontSize::Small);

                // Fill
                { char fk[] = "##hfill"; ZUIBox* f = ZUIPushBox(ctx, fk, 7, ZUI_None);
                  f->Size[0] = ZFill(); f->Size[1] = ZPx(24.f); ZUIPopBox(ctx); }

                ZUISmallButton(ctx, "+");
                ZUISpacer(ctx, 4.f);
                ZUIEndRow(ctx);
            }

            // Search
            ZUISearchBox(ctx, "##hsearch", search_buf, sizeof(search_buf),
                         "Search actors...", ZFill());

            ZUISeparator(ctx);

            // Pre-seed open states on first build
            if (!states_seeded)
            {
                auto seed = [&](const char* lbl, int depth) {
                    uint64_t h = ZUIHashStr(lbl, (uint32_t)strlen(lbl)) ^ (uint64_t)depth;
                    ZUIPersistentState* s = ZUIStateGetOrInsert(&ctx->StateStore, h);
                    if (s) s->UserData = 1.f;
                };
                seed("World",        0);
                seed("DefaultScene", 1);
                states_seeded = true;
            }

            ZUIBeginTreeView(ctx, "##hier");

            for (int i = 0; i < kCount; ++i)
            {
                if (k_actors[i].parent != -1) { continue; }

                bool is_sel = (selected == i);
                if (ZUITreeViewBeginNode(ctx, k_actors[i].name, is_sel, k_actors[i].icon, true))
                {
                    if (is_sel) selected = i;
                    for (int j = 0; j < kCount; ++j)
                    {
                        if (k_actors[j].parent != i) { continue; }
                        bool csel = (selected == j);
                        bool j_has_children = false;
                        for (int k = 0; k < kCount; ++k)
                            if (k_actors[k].parent == j) { j_has_children = true; break; }

                        if (j_has_children)
                        {
                            if (ZUITreeViewBeginNode(ctx, k_actors[j].name, csel, k_actors[j].icon, true))
                            {
                                if (csel) selected = j;
                                for (int k = 0; k < kCount; ++k)
                                {
                                    if (k_actors[k].parent != j) continue;
                                    bool ksel = (selected == k);
                                    if (ZUITreeViewLeaf(ctx, k_actors[k].name, ksel, k_actors[k].icon))
                                        selected = k;
                                }
                                ZUITreeViewEndNode(ctx);
                            }
                            else if (csel) { selected = j; }
                        }
                        else
                        {
                            if (ZUITreeViewLeaf(ctx, k_actors[j].name, csel, k_actors[j].icon))
                                selected = j;
                        }
                    }
                    ZUITreeViewEndNode(ctx);
                }
                else if (is_sel) { selected = i; }
            }

            ZUIEndTreeView(ctx);
        }
    };

    // ================================================================
    // InspectorPanel — Unreal Details style with full theme colors
    // ================================================================
    struct InspectorPanel : ZUIPanelView
    {
        InspectorPanel() { Title = "Inspector"; }

        bool  xfm_open   = true;
        bool  mesh_open  = true;
        bool  mat_open   = true;
        bool  phys_open  = false;
        float pos[3]     = {0.f, 0.f, 0.f};
        float rot[3]     = {0.f, 0.f, 0.f};
        float scl[3]     = {1.f, 1.f, 1.f};
        float roughness  = 0.45f;
        float metallic   = 0.f;
        float mat_color[4]  = {0.8f, 0.4f, 0.2f, 1.f};
        float emit_color[4] = {0.f,  0.f,  0.f,  1.f};
        bool  cast_shadow       = true;
        bool  simulate_physics  = false;
        int   mobility          = 2; // 0=Static 1=Stationary 2=Movable
        char  search_buf[64] = {};

        static constexpr float kLW = 110.f; // label column width
        static constexpr float kRH = 19.f;  // row height (ImGui GetFrameHeight)

        // XYZ property row: colored axis bars + three DragFloat fields
        void XYZRow(ZUIContext* ctx, const char* label, const char* key, float v[3])
        {
            static const float kAcc[3][3] = {
                {0.85f,0.22f,0.22f},
                {0.22f,0.72f,0.22f},
                {0.22f,0.45f,0.85f}
            };
            char rk[64]; snprintf(rk, sizeof(rk), "##xr_%s", key);
            ZUIBox* row = ZUIBeginRow(ctx, rk, ZFill(), ZPx(kRH));
            ZUIBoxSetColor(row, 0.f, 0.f, 0.f, 0.f);

            char lk[64]; snprintf(lk, sizeof(lk), "%s##xl_%s", label, key);
            ZUIBox* lb = ZUIPushBox(ctx, lk, (uint32_t)strlen(lk), ZUI_DrawText);
            lb->Size[0] = ZPx(kLW); lb->Size[1] = ZFill(); lb->Padding[0] = 8.f;
            lb->TextColor[0] = ctx->Theme.TextDim[0]; lb->TextColor[1] = ctx->Theme.TextDim[1];
            lb->TextColor[2] = ctx->Theme.TextDim[2]; lb->TextColor[3] = 1.f;
            ZUIPopBox(ctx);

            for (int i = 0; i < 3; ++i)
            {
                if (i > 0) ZUISpacer(ctx, 2.f);
                char ak[64]; snprintf(ak, sizeof(ak), "##xacc%d_%s", i, key);
                ZUIBox* acc = ZUIPushBox(ctx, ak, (uint32_t)strlen(ak), ZUI_DrawBackground);
                acc->Size[0] = ZPx(2.f); acc->Size[1] = ZPx(kRH - 4.f);
                ZUIBoxSetColor(acc, kAcc[i][0], kAcc[i][1], kAcc[i][2], 1.f);
                acc->EdgeSoftness = 0.f; ZUIPopBox(ctx);

                char fk[64]; snprintf(fk, sizeof(fk), "##xf%d_%s", i, key);
                ZUIDragFloat(ctx, fk, &v[i], 0.1f, 58.f);
            }
            ZUIEndRow(ctx);
        }

        // Standard label + value row (caller fills the value then calls PropEnd)
        void PropStart(ZUIContext* ctx, const char* label, const char* rkey)
        {
            char rk[80]; snprintf(rk, sizeof(rk), "##pr_%s", rkey);
            ZUIBeginRow(ctx, rk, ZFill(), ZPx(kRH));
            char lk[80]; snprintf(lk, sizeof(lk), "%s##lbl_%s", label, rkey);
            ZUIBox* lb = ZUIPushBox(ctx, lk, (uint32_t)strlen(lk), ZUI_DrawText);
            lb->Size[0] = ZPx(kLW); lb->Size[1] = ZFill(); lb->Padding[0] = 8.f;
            lb->TextColor[0] = ctx->Theme.TextDim[0]; lb->TextColor[1] = ctx->Theme.TextDim[1];
            lb->TextColor[2] = ctx->Theme.TextDim[2]; lb->TextColor[3] = 1.f;
            ZUIPopBox(ctx);
        }
        void PropEnd(ZUIContext* ctx) { ZUIEndRow(ctx); }

        // Mobility segmented control using theme colors
        void MobilityRow(ZUIContext* ctx)
        {
            PropStart(ctx, "Mobility", "mob");
            static const char* kLbl[3] = {"Static", "Stationary", "Movable"};
            static const float kW[3]   = {48.f, 66.f, 55.f};
            for (int i = 0; i < 3; ++i)
            {
                if (i > 0) ZUISpacer(ctx, 1.f);
                char bk[40]; snprintf(bk, sizeof(bk), "%s##mob%d", kLbl[i], i);
                bool sel = (mobility == i);
                ZUIBox* btn = ZUIPushBox(ctx, bk, (uint32_t)strlen(bk),
                    ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DrawText | ZUI_Clickable);
                btn->Size[0]    = ZPx(kW[i]); btn->Size[1] = ZPx(kRH - 2.f);
                btn->TextAlign  = ZUITextAlign::Center;
                btn->BorderThickness = 1.f;
                ZUIBoxSetCornerRadius(btn, 2.f); btn->EdgeSoftness = 0.f;
                if (sel)
                {
                    ZUIBoxSetColorArr(btn, ctx->Theme.ButtonActiveBg);
                    btn->BorderColor[0] = ctx->Theme.TabActiveBorder[0];
                    btn->BorderColor[1] = ctx->Theme.TabActiveBorder[1];
                    btn->BorderColor[2] = ctx->Theme.TabActiveBorder[2];
                    btn->BorderColor[3] = 1.f;
                    btn->TextColor[0]   = ctx->Theme.TextDefault[0];
                    btn->TextColor[1]   = ctx->Theme.TextDefault[1];
                    btn->TextColor[2]   = ctx->Theme.TextDefault[2];
                    btn->TextColor[3]   = 1.f;
                }
                else
                {
                    ZUIBoxSetColorArr(btn, ctx->Theme.InputBg);
                    btn->BorderColor[0] = ctx->Theme.InputBorder[0];
                    btn->BorderColor[1] = ctx->Theme.InputBorder[1];
                    btn->BorderColor[2] = ctx->Theme.InputBorder[2];
                    btn->BorderColor[3] = 0.8f;
                    btn->TextColor[0]   = ctx->Theme.TextDim[0];
                    btn->TextColor[1]   = ctx->Theme.TextDim[1];
                    btn->TextColor[2]   = ctx->Theme.TextDim[2];
                    btn->TextColor[3]   = 1.f;
                }
                ZUISignal sig = ZUISignalFromBox(ctx, btn); ZUIPopBox(ctx);
                if (sig.Flags & ZUI_SignalClicked) mobility = i;
            }
            PropEnd(ctx);
        }

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            (void)rect;

            // Actor header — name + class + Add Component
            {
                ZUIBox* hdr = ZUIBeginRow(ctx, "##ah", ZFill(), ZPx(40.f));
                hdr->Flags  = hdr->Flags | ZUI_DrawBackground;
                ZUIBoxSetColorArr(hdr, ctx->Theme.PanelBgAlt);
                hdr->EdgeSoftness = 0.f;
                ZUISpacer(ctx, 10.f);

                // Type icon
                ZUIBox* ic = ZUIPushBox(ctx, "##ahic", 6, ZUI_DrawBackground);
                ic->Size[0] = ZPx(18.f); ic->Size[1] = ZPx(18.f);
                ZUIBoxSetColorArr(ic, ctx->Theme.TabActiveBorder);
                ZUIBoxSetCornerRadius(ic, 3.f); ic->EdgeSoftness = 0.5f;
                ZUIPopBox(ctx);
                ZUISpacer(ctx, 8.f);

                // Name + class stacked
                ZUIBeginColumn(ctx, "##ahtxt", ZFit(), ZFit());
                    ZUILabel(ctx, "Cube", ctx->Theme.TextDefault, ZUIFontSize::Header);
                    ZUILabel(ctx, "StaticMeshActor", ctx->Theme.TextDim);
                ZUIEndColumn(ctx);

                // Fill
                { char fk[] = "##ahfill"; ZUIBox* f = ZUIPushBox(ctx, fk, 8, ZUI_None);
                  f->Size[0] = ZFill(); f->Size[1] = ZPx(40.f); ZUIPopBox(ctx); }

                ZUISmallButton(ctx, "+ Add");
                ZUISpacer(ctx, 8.f);
                ZUIEndRow(ctx);
            }

            // Search
            ZUISearchBox(ctx, "##inspsearch", search_buf, sizeof(search_buf),
                         "Search properties...", ZFill());
            ZUISeparator(ctx);

            // Scrollable properties
            ZUIBeginScrollRegion(ctx, "##insp", ZFill(), ZFill());

            // Transform section
            if (ZUICollapsingHeader(ctx, "Transform", &xfm_open))
            {
                XYZRow(ctx, "Location", "loc", pos);
                XYZRow(ctx, "Rotation", "rot", rot);
                XYZRow(ctx, "Scale",    "scl", scl);
                MobilityRow(ctx);
            }

            ZUISeparator(ctx);

            // Static Mesh section
            if (ZUICollapsingHeader(ctx, "Static Mesh", &mesh_open))
            {
                PropStart(ctx, "Mesh", "mesh_v");
                if (ZUIBeginCombo(ctx, "##mesh_combo", "SM_Cube.zemesh", ZFill()))
                {
                    ZUIComboItem(ctx, "SM_Cube.zemesh",   true);
                    ZUIComboItem(ctx, "SM_Sphere.zemesh",  false);
                    ZUIComboItem(ctx, "SM_Plane.zemesh",   false);
                    ZUIEndCombo(ctx);
                }
                PropEnd(ctx);

                PropStart(ctx, "Cast Shadow", "cshadow");
                ZUICheckbox(ctx, "##cshadow_cb", &cast_shadow);
                PropEnd(ctx);

                PropStart(ctx, "LOD", "lod_v");
                ZUILabel(ctx, "Auto", ctx->Theme.TextAccent);
                PropEnd(ctx);
            }

            ZUISeparator(ctx);

            // Material section
            if (ZUICollapsingHeader(ctx, "Material", &mat_open))
            {
                PropStart(ctx, "Base Color", "basecol");
                ZUIColorEdit4(ctx, "##basecol", mat_color);
                PropEnd(ctx);

                PropStart(ctx, "Emissive", "emissive");
                ZUIColorEdit4(ctx, "##emissive", emit_color);
                PropEnd(ctx);

                PropStart(ctx, "Roughness", "roughness");
                ZUISliderFloat(ctx, "##roughness", &roughness, 0.f, 1.f, ZFill(), ZPx(kRH - 2.f));
                PropEnd(ctx);

                PropStart(ctx, "Metallic", "metallic");
                ZUISliderFloat(ctx, "##metallic", &metallic, 0.f, 1.f, ZFill(), ZPx(kRH - 2.f));
                PropEnd(ctx);
            }

            ZUISeparator(ctx);

            // Physics section
            if (ZUICollapsingHeader(ctx, "Physics", &phys_open))
            {
                PropStart(ctx, "Simulate", "phys_sim");
                ZUICheckbox(ctx, "##phys_sim_cb", &simulate_physics);
                PropEnd(ctx);
            }

            ZUISpacer(ctx, 8.f);

            // Add Component button — full width, centered
            {
                ZUIBeginRow(ctx, "##addcmp", ZFill(), ZPx(26.f));
                ZUISpacer(ctx, 8.f);
                ZUIButton(ctx, "+ Add Component", ZFill(), ZPx(24.f));
                ZUISpacer(ctx, 8.f);
                ZUIEndRow(ctx);
            }

            ZUIEndScrollRegion(ctx);
        }
    };

    // ================================================================
    // ViewportPanel — 3D viewport with floating overlay toolbar
    // ================================================================
    struct ViewportPanel : ZUIPanelView
    {
        ViewportPanel() { Title = "Viewport"; }

        int  view_mode    = 0; // 0=Perspective 1=Top 2=Front 3=Right
        int  shading_mode = 0; // 0=Lit 1=Unlit 2=Wireframe
        bool snap_enabled = false;

        static constexpr float k_fps[32] = {
            90,88,91,87,92,89,85,93,90,88,
            94,87,89,91,88,92,90,86,93,89,
            91,88,90,92,87,93,88,91,89,90,
            92,88
        };

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            // Dark background — represents the 3D scene area
            ZUIBox* bg = ZUIBeginColumn(ctx, "##vpbg", ZFill(), ZFill());
            bg->Flags   = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColor(bg, 0.09f, 0.09f, 0.095f, 1.f);
            bg->EdgeSoftness = 0.f;

            // Scene placeholder text
            ZUISpacer(ctx, 24.f);
            ZUILabel(ctx, "[ 3D Scene Viewport ]",        ctx->Theme.TextDim);
            ZUISpacer(ctx, 4.f);
            ZUILabel(ctx, "Scene renderer attaches here.", ctx->Theme.TextDim);
            ZUISpacer(ctx, 4.f);
            ZUILabel(ctx, "Camera: X 0.0  Y 5.0  Z -8.0", ctx->Theme.TextDim);
            ZUISpacer(ctx, 8.f);
            {
                ZUIBox* row = ZUIBeginRow(ctx, "##fpsrow", ZFill(), ZFit());
                ZUISpacer(ctx, 8.f);
                ZUILabel(ctx, "FPS: 90", ctx->Theme.TextDim);
                ZUISpacer(ctx, 8.f);
                ZUIPlotLines(ctx, "##fps", k_fps, 32, 60.f, 120.f, nullptr,
                             ZPx(80.f), ZPx(20.f));
                ZUIEndRow(ctx);
            }

            ZUIEndColumn(ctx);

            // Floating overlay toolbar — pinned to top of viewport rect
            {
                float tw = rect[2] - rect[0];
                ZUIBox* tb = ZUIPushBox(ctx, "##vptb", 6,
                    ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
                tb->Size[0]     = ZPx(tw);
                tb->Size[1]     = ZPx(26.f);
                tb->FloatPos[0] = rect[0];
                tb->FloatPos[1] = rect[1];
                tb->LayoutAxis  = ZUIAxis::X;
                ZUIBoxSetColor(tb, 0.10f, 0.10f, 0.105f, 0.92f);
                tb->EdgeSoftness = 0.f;

                // View mode buttons
                ZUISpacer(ctx, 6.f);
                static const char* kVm[] = {"Persp", "Top", "Front", "Right"};
                for (int i = 0; i < 4; ++i)
                {
                    char bk[32]; snprintf(bk, sizeof(bk), "%s##vm%d", kVm[i], i);
                    bool sel = (view_mode == i);
                    ZUIBox* btn = ZUIPushBox(ctx, bk, (uint32_t)strlen(bk),
                        ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable);
                    btn->Size[0] = ZText(); btn->Size[1] = ZPx(20.f);
                    btn->Padding[0] = btn->Padding[2] = 7.f;
                    ZUIBoxSetCornerRadius(btn, 2.f); btn->EdgeSoftness = 0.f;
                    if (sel)
                    {
                        ZUIBoxSetColorArr(btn, ctx->Theme.ButtonActiveBg);
                        btn->TextColor[0] = ctx->Theme.TextDefault[0];
                        btn->TextColor[1] = ctx->Theme.TextDefault[1];
                        btn->TextColor[2] = ctx->Theme.TextDefault[2];
                        btn->TextColor[3] = 1.f;
                    }
                    else
                    {
                        ZUIBoxSetColor(btn, 0.20f, 0.20f, 0.22f, 1.f);
                        btn->TextColor[0] = ctx->Theme.TextDim[0];
                        btn->TextColor[1] = ctx->Theme.TextDim[1];
                        btn->TextColor[2] = ctx->Theme.TextDim[2];
                        btn->TextColor[3] = 1.f;
                    }
                    ZUISignal s = ZUISignalFromBox(ctx, btn); ZUIPopBox(ctx);
                    if (s.Flags & ZUI_SignalClicked) view_mode = i;
                    if (i < 3) ZUISpacer(ctx, 1.f);
                }

                ZUISpacer(ctx, 8.f);
                // Thin separator
                { char sk[] = "##vpsep"; ZUIBox* sp = ZUIPushBox(ctx, sk, 7, ZUI_DrawBackground);
                  sp->Size[0] = ZPx(1.f); sp->Size[1] = ZPx(14.f);
                  ZUIBoxSetColorArr(sp, ctx->Theme.Separator); ZUIPopBox(ctx); }
                ZUISpacer(ctx, 8.f);

                // Shading mode buttons
                static const char* kSh[] = {"Lit", "Unlit", "Wire"};
                for (int i = 0; i < 3; ++i)
                {
                    char bk[32]; snprintf(bk, sizeof(bk), "%s##sh%d", kSh[i], i);
                    bool sel = (shading_mode == i);
                    ZUIBox* btn = ZUIPushBox(ctx, bk, (uint32_t)strlen(bk),
                        ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable);
                    btn->Size[0] = ZText(); btn->Size[1] = ZPx(20.f);
                    btn->Padding[0] = btn->Padding[2] = 7.f;
                    ZUIBoxSetCornerRadius(btn, 2.f); btn->EdgeSoftness = 0.f;
                    if (sel)
                    {
                        ZUIBoxSetColorArr(btn, ctx->Theme.ButtonActiveBg);
                        btn->TextColor[0] = ctx->Theme.TextDefault[0];
                        btn->TextColor[1] = ctx->Theme.TextDefault[1];
                        btn->TextColor[2] = ctx->Theme.TextDefault[2];
                        btn->TextColor[3] = 1.f;
                    }
                    else
                    {
                        ZUIBoxSetColor(btn, 0.20f, 0.20f, 0.22f, 1.f);
                        btn->TextColor[0] = ctx->Theme.TextDim[0];
                        btn->TextColor[1] = ctx->Theme.TextDim[1];
                        btn->TextColor[2] = ctx->Theme.TextDim[2];
                        btn->TextColor[3] = 1.f;
                    }
                    ZUISignal s = ZUISignalFromBox(ctx, btn); ZUIPopBox(ctx);
                    if (s.Flags & ZUI_SignalClicked) shading_mode = i;
                    if (i < 2) ZUISpacer(ctx, 1.f);
                }

                ZUISpacer(ctx, 8.f);
                ZUIToggleButton(ctx, "Snap##vsnap", &snap_enabled, ZText(), ZPx(20.f));

                ZUIPopBox(ctx); // end floating toolbar
            }
        }
    };
    constexpr float ViewportPanel::k_fps[32];

    // ================================================================
    // OutputPanel — VS Code Output style log viewer
    // ================================================================
    struct OutputPanel : ZUIPanelView
    {
        OutputPanel() { Title = "Output"; }

        int  filter       = 0;
        bool auto_scroll  = true;

        struct LogEntry { const char* time; const char* level; const char* text; int kind; };
        static constexpr LogEntry k_log[] = {
            {"15:20:01","INFO ","Engine initialized successfully",          0},
            {"15:20:01","INFO ","VFS mounted: /ZodiacEngine",               0},
            {"15:20:02","INFO ","GPU: Apple M2 Pro  —  MoltenVK 1.3",      0},
            {"15:20:02","INFO ","Vulkan swapchain: 3024x1834  (Retina)",    0},
            {"15:20:03","INFO ","Scene loaded: DefaultScene",               0},
            {"15:20:03","WARN ","No skybox texture set — using atmosphere",  1},
            {"15:20:03","INFO ","ZUI panel system ready  (UIScale 2.0)",    0},
            {"15:20:03","INFO ","Draw list renderer active",                0},
            {"15:20:04","INFO ","8 actors in scene",                        0},
            {"15:20:05","WARN ","Shader cache miss: recompiling 3 shaders", 1},
            {"15:20:06","INFO ","Ready.",                                   0},
        };
        static constexpr int kLogCount = 11;

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            (void)rect;

            // Filter + clear bar
            {
                ZUIBox* fb = ZUIBeginRow(ctx, "##outfb", ZFill(), ZPx(26.f));
                fb->Flags  = fb->Flags | ZUI_DrawBackground;
                ZUIBoxSetColorArr(fb, ctx->Theme.PanelBgAlt);
                fb->EdgeSoftness = 0.f;
                ZUISpacer(ctx, 6.f);

                static const char* kF[]        = {"All", "Info", "Warn", "Error"};
                static const float kFCol[4][4] = {
                    {0.70f,0.70f,0.72f,1.f},
                    {0.53f,0.80f,0.53f,1.f},
                    {0.95f,0.74f,0.14f,1.f},
                    {0.94f,0.33f,0.31f,1.f},
                };

                for (int i = 0; i < 4; ++i)
                {
                    char bk[24]; snprintf(bk, sizeof(bk), "%s##flt%d", kF[i], i);
                    bool sel = (filter == i);
                    ZUIBox* btn = ZUIPushBox(ctx, bk, (uint32_t)strlen(bk),
                        ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DrawText | ZUI_Clickable);
                    btn->Size[0] = ZText(); btn->Size[1] = ZPx(20.f);
                    btn->Padding[0] = btn->Padding[2] = 8.f;
                    btn->BorderThickness = 1.f;
                    ZUIBoxSetCornerRadius(btn, 3.f); btn->EdgeSoftness = 0.f;
                    if (sel)
                    {
                        float bg[4] = {kFCol[i][0]*0.25f, kFCol[i][1]*0.25f, kFCol[i][2]*0.25f, 1.f};
                        ZUIBoxSetColorArr(btn, bg);
                        btn->BorderColor[0] = kFCol[i][0];
                        btn->BorderColor[1] = kFCol[i][1];
                        btn->BorderColor[2] = kFCol[i][2];
                        btn->BorderColor[3] = 0.9f;
                    }
                    else
                    {
                        ZUIBoxSetColorArr(btn, ctx->Theme.InputBg);
                        btn->BorderColor[0] = ctx->Theme.InputBorder[0];
                        btn->BorderColor[1] = ctx->Theme.InputBorder[1];
                        btn->BorderColor[2] = ctx->Theme.InputBorder[2];
                        btn->BorderColor[3] = 0.6f;
                    }
                    btn->TextColor[0] = kFCol[i][0];
                    btn->TextColor[1] = kFCol[i][1];
                    btn->TextColor[2] = kFCol[i][2];
                    btn->TextColor[3] = sel ? 1.f : 0.75f;
                    ZUISignal s = ZUISignalFromBox(ctx, btn); ZUIPopBox(ctx);
                    if (s.Flags & ZUI_SignalClicked) filter = i;
                    ZUISpacer(ctx, 4.f);
                }

                // Fill + auto-scroll toggle + clear
                { char fk[] = "##outfill"; ZUIBox* f = ZUIPushBox(ctx, fk, 8, ZUI_None);
                  f->Size[0] = ZFill(); f->Size[1] = ZPx(26.f); ZUIPopBox(ctx); }
                ZUICheckbox(ctx, "Scroll##autoscroll", &auto_scroll);
                ZUISpacer(ctx, 4.f);
                ZUISmallButton(ctx, "Clear");
                ZUISpacer(ctx, 6.f);
                ZUIEndRow(ctx);
            }

            ZUISeparator(ctx);
            ZUIBeginScrollRegion(ctx, "##outlog", ZFill(), ZFill());

            for (int i = 0; i < kLogCount; ++i)
            {
                const LogEntry& e = k_log[i];
                if (filter == 1 && e.kind != 0) continue;
                if (filter == 2 && e.kind != 1) continue;
                if (filter == 3 && e.kind != 2) continue;

                // Level color
                const float* lc = (e.kind == 1) ? ctx->Theme.TextWarn
                                 : (e.kind == 2) ? ctx->Theme.TextError
                                 : ctx->Theme.TextDim;

                char rk[32]; snprintf(rk, sizeof(rk), "##ol%d", i);
                ZUIBox* row = ZUIBeginRow(ctx, rk, ZFill(), ZPx(20.f));
                row->Flags  = row->Flags | ZUI_DrawBackground;
                bool hov    = (ctx->HotKey == row->Key);
                ZUIBoxSetColor(row, 0.f, 0.f, 0.f, hov ? 0.08f : 0.f);

                ZUISpacer(ctx, 8.f);

                // Timestamp (dim monospace feel)
                float tc[4] = {0.35f, 0.35f, 0.37f, 1.f};
                ZUILabel(ctx, e.time, tc);
                ZUISpacer(ctx, 6.f);

                // Level badge
                ZUILabel(ctx, e.level, lc);
                ZUISpacer(ctx, 2.f);

                // Message
                ZUILabel(ctx, e.text, ctx->Theme.TextDefault);
                ZUIEndRow(ctx);
            }

            ZUIEndScrollRegion(ctx);
        }
    };
    constexpr OutputPanel::LogEntry OutputPanel::k_log[11];

    // ================================================================
    // ContentBrowserPanel — asset grid viewer
    // ================================================================
    struct ContentBrowserPanel : ZUIPanelView
    {
        ContentBrowserPanel() { Title = "Content"; }

        int  selected    = -1;
        int  view_size   = 1;
        char search_buf[64] = {};
        static constexpr float kCellSizes[3] = {72.f, 96.f, 120.f};

        struct Asset {
            const char* name;
            const char* ext;
            float       icon[4];
            bool        is_dir;
        };
        static constexpr Asset k_assets[] = {
            {"Meshes",      "",         {0.98f,0.82f,0.28f,1.f}, true  },
            {"Textures",    "",         {0.98f,0.82f,0.28f,1.f}, true  },
            {"Materials",   "",         {0.98f,0.82f,0.28f,1.f}, true  },
            {"SM_Cube",     ".zemesh",  {0.40f,0.78f,1.00f,1.f}, false },
            {"SM_Sphere",   ".zemesh",  {0.40f,0.78f,1.00f,1.f}, false },
            {"SM_Cylinder", ".zemesh",  {0.40f,0.78f,1.00f,1.f}, false },
            {"T_Diffuse",   ".png",     {1.00f,0.55f,0.30f,1.f}, false },
            {"T_Normal",    ".png",     {0.50f,0.65f,1.00f,1.f}, false },
            {"T_Roughness", ".png",     {0.70f,0.70f,0.72f,1.f}, false },
            {"M_Wood",      ".zemat",   {0.45f,0.85f,0.40f,1.f}, false },
            {"M_Metal",     ".zemat",   {0.45f,0.85f,0.40f,1.f}, false },
            {"DefaultScene",".zescene", {0.80f,0.50f,1.00f,1.f}, false },
            {"Env_Day",     ".hdr",     {0.30f,0.90f,0.90f,1.f}, false },
            {"SFX_Impact",  ".wav",     {0.98f,0.82f,0.25f,1.f}, false },
        };
        static constexpr int kAssetCount = 14;

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            (void)rect;

            // Toolbar
            {
                ZUIBox* tb = ZUIBeginRow(ctx, "##cbtb", ZFill(), ZPx(26.f));
                tb->Flags  = tb->Flags | ZUI_DrawBackground;
                ZUIBoxSetColorArr(tb, ctx->Theme.PanelBgAlt);
                tb->EdgeSoftness = 0.f;
                ZUISpacer(ctx, 6.f);

                ZUISmallButton(ctx, "Import");
                ZUISpacer(ctx, 4.f);
                ZUISmallButton(ctx, "New Folder");
                ZUISpacer(ctx, 6.f);

                ZUISearchBox(ctx, "##cbsearch", search_buf, sizeof(search_buf),
                             "Search assets...", ZFill());

                ZUISpacer(ctx, 6.f);

                // View size S/M/L
                static const char* kSz[] = {"S", "M", "L"};
                for (int i = 0; i < 3; ++i)
                {
                    char bk[16]; snprintf(bk, sizeof(bk), "%s##vs%d", kSz[i], i);
                    bool sel = (view_size == i);
                    ZUIBox* btn = ZUIPushBox(ctx, bk, (uint32_t)strlen(bk),
                        ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable);
                    btn->Size[0]   = ZPx(22.f); btn->Size[1] = ZPx(20.f);
                    btn->TextAlign = ZUITextAlign::Center;
                    ZUIBoxSetCornerRadius(btn, 2.f); btn->EdgeSoftness = 0.f;
                    if (sel) ZUIBoxSetColorArr(btn, ctx->Theme.ButtonActiveBg);
                    else     ZUIBoxSetColorArr(btn, ctx->Theme.InputBg);
                    btn->TextColor[0] = sel ? ctx->Theme.TextDefault[0] : ctx->Theme.TextDim[0];
                    btn->TextColor[1] = sel ? ctx->Theme.TextDefault[1] : ctx->Theme.TextDim[1];
                    btn->TextColor[2] = sel ? ctx->Theme.TextDefault[2] : ctx->Theme.TextDim[2];
                    btn->TextColor[3] = 1.f;
                    ZUISignal s = ZUISignalFromBox(ctx, btn); ZUIPopBox(ctx);
                    if (s.Flags & ZUI_SignalClicked) view_size = i;
                    if (i < 2) ZUISpacer(ctx, 1.f);
                }
                ZUISpacer(ctx, 6.f);
                ZUIEndRow(ctx);
            }

            // Breadcrumb path
            {
                ZUIBox* br = ZUIBeginRow(ctx, "##cbbr", ZFill(), ZPx(20.f));
                br->Flags  = br->Flags | ZUI_DrawBackground;
                ZUIBoxSetColor(br, 0.f, 0.f, 0.f, 0.15f);
                ZUISpacer(ctx, 8.f);
                ZUILabel(ctx, "Assets", ctx->Theme.TextAccent);
                ZUILabel(ctx, "  /  ",  ctx->Theme.TextDim);
                ZUILabel(ctx, "All",    ctx->Theme.TextDefault);
                ZUIEndRow(ctx);
            }

            ZUISeparator(ctx);

            float cell = kCellSizes[view_size];
            ZUIBeginGridView(ctx, "##cbgrid", cell, cell + 22.f);

            for (int i = 0; i < kAssetCount; ++i)
            {
                const Asset& a = k_assets[i];
                char ik[32]; snprintf(ik, sizeof(ik), "##cb%d", i);
                if (ZUIGridViewNextItem(ctx, ik, selected == i))
                    selected = i;

                ZUISpacer(ctx, 5.f);

                // Icon block
                char iconk[32]; snprintf(iconk, sizeof(iconk), "##cbi%d", i);
                ZUIBox* icon = ZUIPushBox(ctx, iconk, (uint32_t)strlen(iconk), ZUI_DrawBackground);
                float isz    = cell - 14.f;
                icon->Size[0] = ZPx(isz); icon->Size[1] = ZPx(isz);
                ZUIBoxSetColorArr(icon, a.icon);
                ZUIBoxSetCornerRadius(icon, a.is_dir ? 5.f : 8.f);
                icon->EdgeSoftness = 0.5f; ZUIPopBox(ctx);

                ZUISpacer(ctx, 3.f);
                ZUILabel(ctx, a.name,
                         selected == i ? ctx->Theme.TextDefault : ctx->Theme.TextDim,
                         ZUIFontSize::Small);

                ZUIGridViewEndItem(ctx);
            }

            ZUIEndGridView(ctx);
        }
    };
    constexpr float ContentBrowserPanel::kCellSizes[3];

} // namespace Tetragrama::Panels
