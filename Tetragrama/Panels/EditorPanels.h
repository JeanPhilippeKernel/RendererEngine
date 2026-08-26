#pragma once
#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>
#include <cstring>

namespace Tetragrama::Panels
{
    using namespace ZEngine::UI;

    // ---------------------------------------------------------------
    // HierarchyPanel — scene actor tree
    // ---------------------------------------------------------------
    struct HierarchyPanel : ZUIPanelView
    {
        HierarchyPanel() { Title = "Hierarchy"; }

        struct Entry { const char* label; int depth; float icon[4]; };
        static constexpr Entry k_actors[] = {
            { "World Root",       0, {0.50f,0.50f,0.90f,1.f} },
            { "DefaultScene",     1, {0.30f,0.78f,0.30f,1.f} },
            { "DirectionalLight", 2, {0.98f,0.85f,0.25f,1.f} },
            { "MainCamera",       2, {0.25f,0.78f,0.98f,1.f} },
            { "Cube",             2, {0.72f,0.50f,0.98f,1.f} },
            { "Sphere",           2, {0.72f,0.50f,0.98f,1.f} },
            { "Ground",           2, {0.72f,0.50f,0.98f,1.f} },
            { "SkyAtmosphere",    2, {0.40f,0.85f,0.95f,1.f} },
        };
        static constexpr int kCount = 8;
        int selected = -1;

        void BuildContent(ZUIContext* ctx, float[4]) override
        {
            ZUIBeginScrollRegion(ctx, "##hier", ZFill(), ZFill());
            ZUISpacer(ctx, 3.f);

            for (int i = 0; i < kCount; ++i)
            {
                const Entry& e = k_actors[i];
                char rk[24]; snprintf(rk, sizeof(rk), "##hr%d", i);

                ZUIBox* row = ZUIBeginRow(ctx, rk, ZFill(), ZSPx(ctx, 22.f));
                row->Flags = row->Flags | ZUI_DrawBackground | ZUI_Clickable;
                if (selected == i)
                    ZUIBoxSetColorArr(row, ctx->Theme.RowSelectedBg);
                else
                    ZUIBoxSetColor(row, 0.f, 0.f, 0.f, 0.f);

                // Indent
                ZUISpacer(ctx, (float)(e.depth * 16) + 6.f);

                // Type icon (small colored square)
                char ik[20]; snprintf(ik, sizeof(ik), "##ic%d", i);
                ZUIBox* ic = ZUIPushBox(ctx, ik, (uint32_t)strlen(ik), ZUI_DrawBackground);
                ic->Size[0] = ZPx(10.f);
                ic->Size[1] = ZPx(10.f);
                ZUIBoxSetColorArr(ic, e.icon);
                ZUIBoxSetCornerRadius(ic, 2.f);
                ic->EdgeSoftness = 0.5f;
                ZUIPopBox(ctx);
                ZUISpacer(ctx, 6.f);

                ZUILabel(ctx, e.label,
                         selected == i ? ctx->Theme.TextDefault : ctx->Theme.TextDim);

                ZUISignal sig = ZUISignalFromBox(ctx, row);
                ZUIEndRow(ctx);
                if (sig.Flags & ZUI_SignalClicked) { selected = i; }
            }

            ZUIEndScrollRegion(ctx);
        }
    };

    // ---------------------------------------------------------------
    // InspectorPanel — component property editor
    // ---------------------------------------------------------------
    struct InspectorPanel : ZUIPanelView
    {
        InspectorPanel() { Title = "Inspector"; }

        bool  xfm_open  = true;
        bool  mesh_open = true;
        bool  mat_open  = false;
        float pos[3]    = {0.f, 0.f, 0.f};
        float rot[3]    = {0.f, 0.f, 0.f};
        float scl[3]    = {1.f, 1.f, 1.f};

        float mat_color[4] = {0.8f, 0.4f, 0.2f, 1.f};
        int   layer_id     = 0;

        void LabeledRow(ZUIContext* ctx, const char* label,
                        const char* key, float* v)
        {
            char rk[40]; snprintf(rk, sizeof(rk), "##lrow%s", key);
            ZUIBeginRow(ctx, rk, ZFill(), ZSPx(ctx, 24.f));
            ZUISpacer(ctx, 8.f);
            ZUIBox* lb = ZUIPushBox(ctx, label, (uint32_t)strlen(label), ZUI_DrawText);
            lb->Size[0] = ZPx(68.f); lb->Size[1] = ZText();
            lb->TextColor[0]=ctx->Theme.TextDim[0]; lb->TextColor[1]=ctx->Theme.TextDim[1];
            lb->TextColor[2]=ctx->Theme.TextDim[2]; lb->TextColor[3]=ctx->Theme.TextDim[3];
            ZUIPopBox(ctx);
            ZUISpacer(ctx, 4.f);
            ZUIDragFloat3(ctx, key, v, 0.05f, 0.f);
            ZUIEndRow(ctx);
        }

        void BuildContent(ZUIContext* ctx, float[4]) override
        {
            ZUIBeginScrollRegion(ctx, "##insp", ZFill(), ZFill());

            // Actor header
            {
                ZUIBox* hdr = ZUIBeginRow(ctx, "##ah", ZFill(), ZSPx(ctx, 42.f));
                hdr->Flags = hdr->Flags | ZUI_DrawBackground;
                ZUIBoxSetColor(hdr, 0.160f, 0.160f, 0.170f, 1.f);
                ZUISpacer(ctx, 10.f);
                ZUIBeginColumn(ctx, "##ahtxt", ZFit(), ZFit());
                    ZUILabel(ctx, "Cube", ctx->Theme.TextDefault, ZUIFontSize::Header);
                    ZUILabel(ctx, "Static Mesh Actor", ctx->Theme.TextDim);
                ZUIEndColumn(ctx);
                ZUIEndRow(ctx);
            }
            ZUISpacer(ctx, 2.f);
            ZUISeparator(ctx);

            // Transform — uses ZUIDragFloat3 (colored X/Y/Z chips)
            if (ZUICollapsingHeader(ctx, "Transform", &xfm_open) && xfm_open)
            {
                ZUISpacer(ctx, 3.f);
                LabeledRow(ctx, "Location", "loc", pos);
                LabeledRow(ctx, "Rotation", "rot", rot);
                LabeledRow(ctx, "Scale",    "scl", scl);
                ZUISpacer(ctx, 4.f);
            }
            ZUISeparator(ctx);

            // Static Mesh
            if (ZUICollapsingHeader(ctx, "Static Mesh", &mesh_open) && mesh_open)
            {
                ZUISpacer(ctx, 4.f);
                ZUIBeginRow(ctx, "##mr", ZFill(), ZSPx(ctx, 20.f));
                    ZUISpacer(ctx, 10.f);
                    ZUILabel(ctx, "Mesh", ctx->Theme.TextDim);
                    ZUISpacer(ctx, 10.f);
                    ZUILabel(ctx, "SM_Cube.zemesh", ctx->Theme.TextAccent);
                ZUIEndRow(ctx);
                ZUISpacer(ctx, 4.f);
                // Layer ID — ZUIDragInt demo
                ZUIBeginRow(ctx, "##layerrow", ZFill(), ZSPx(ctx, 20.f));
                    ZUISpacer(ctx, 10.f);
                    ZUILabel(ctx, "Layer", ctx->Theme.TextDim);
                    ZUISpacer(ctx, 10.f);
                    ZUIDragInt(ctx, "##layer", &layer_id, 1.f, 50.f);
                ZUIEndRow(ctx);
                ZUISpacer(ctx, 4.f);
            }
            ZUISeparator(ctx);

            // Material — ZUIColorEdit4 demo
            if (ZUICollapsingHeader(ctx, "Material", &mat_open) && mat_open)
            {
                ZUISpacer(ctx, 4.f);
                ZUIBeginRow(ctx, "##colrow", ZFill(), ZSPx(ctx, 24.f));
                    ZUISpacer(ctx, 10.f);
                    ZUILabel(ctx, "Base Color", ctx->Theme.TextDim);
                    ZUISpacer(ctx, 8.f);
                    ZUIColorEdit4(ctx, "##basecolor", mat_color);
                ZUIEndRow(ctx);
                ZUISpacer(ctx, 4.f);
            }
            ZUISeparator(ctx);

            // Add Component
            ZUISpacer(ctx, 8.f);
            ZUIBeginRow(ctx, "##acrow", ZFill(), ZSPx(ctx, 28.f));
                ZUISpacer(ctx, 8.f);
                ZUIButton(ctx, "+ Add Component", ZFill(), ZSPx(ctx, 26.f));
            ZUIEndRow(ctx);

            ZUIEndScrollRegion(ctx);
        }
    };

    // ---------------------------------------------------------------
    // ViewportPanel — 3D scene view placeholder
    // ---------------------------------------------------------------
    struct ViewportPanel : ZUIPanelView
    {
        ViewportPanel() { Title = "Viewport"; }

        void BuildContent(ZUIContext* ctx, float[4]) override
        {
            // Viewport toolbar strip at top
            {
                ZUIBox* tb = ZUIBeginRow(ctx, "##vptb", ZFill(), ZSPx(ctx, 26.f));
                tb->Flags = tb->Flags | ZUI_DrawBackground;
                ZUIBoxSetColor(tb, 0.130f, 0.130f, 0.135f, 1.f);
                ZUISpacer(ctx, 6.f);
                ZUIButton(ctx, "Perspective", ZFit(), ZSPx(ctx, 22.f));
                ZUISpacer(ctx, 4.f);
                ZUIButton(ctx, "Lit",         ZFit(), ZSPx(ctx, 22.f));
                ZUISpacer(ctx, 4.f);
                ZUIButton(ctx, "Show",        ZFit(), ZSPx(ctx, 22.f));
                ZUIEndRow(ctx);
            }

            // Scene area (dark fill)
            ZUIBox* scene = ZUIBeginColumn(ctx, "##vpscene", ZFill(), ZFill());
            scene->Flags = scene->Flags | ZUI_DrawBackground;
            ZUIBoxSetColor(scene, 0.094f, 0.094f, 0.098f, 1.f);
                ZUISpacer(ctx, 28.f);
                ZUILabel(ctx, "3D Viewport", ctx->Theme.TextDim);
                ZUISpacer(ctx, 8.f);
                ZUISpinner(ctx, "##vpload", 8.f);
                ZUISpacer(ctx, 4.f);
                float hint[4] = {0.28f, 0.28f, 0.30f, 1.f};
                ZUILabel(ctx, "Scene renderer will attach here.", hint);
                ZUISpacer(ctx, 4.f);
                ZUILabel(ctx, "Camera  |  Perspective  |  90 FOV", hint);
            ZUIEndColumn(ctx);
        }
    };

    // ---------------------------------------------------------------
    // OutputPanel — engine console / log
    // ---------------------------------------------------------------
    struct OutputPanel : ZUIPanelView
    {
        OutputPanel() { Title = "Output"; }

        struct LogEntry { const char* level; const char* text; int kind; };
        // kind: 0=info  1=warn  2=error
        static constexpr LogEntry k_log[] = {
            { "[INFO] ",  "Engine initialized successfully",          0 },
            { "[INFO] ",  "VFS mounted: /ZodiacEngine",               0 },
            { "[INFO] ",  "GPU: Apple M2 Pro  —  MoltenVK 1.3",      0 },
            { "[INFO] ",  "Vulkan swapchain: 3024 x 1834  (Retina)", 0 },
            { "[INFO] ",  "Scene loaded: DefaultScene",               0 },
            { "[WARN] ",  "No skybox texture set — using atmosphere", 1 },
            { "[INFO] ",  "ZUI panel system ready  (UIScale 2.0)",    0 },
            { "[INFO] ",  "2 actors in scene",                        0 },
            { "[WARN] ",  "Shader cache miss: recompiling 3 shaders", 1 },
            { "[INFO] ",  "Ready.",                                    0 },
        };
        static constexpr int kLogCount = 10;

        void BuildContent(ZUIContext* ctx, float[4]) override
        {
            // Filter bar
            {
                ZUIBox* fb = ZUIBeginRow(ctx, "##outfb", ZFill(), ZSPx(ctx, 24.f));
                fb->Flags = fb->Flags | ZUI_DrawBackground;
                ZUIBoxSetColor(fb, 0.130f, 0.130f, 0.135f, 1.f);
                ZUISpacer(ctx, 6.f);
                ZUIButton(ctx, "All",   ZFit(), ZSPx(ctx, 20.f));
                ZUISpacer(ctx, 4.f);
                ZUIButton(ctx, "Info",  ZFit(), ZSPx(ctx, 20.f));
                ZUISpacer(ctx, 4.f);
                ZUIButton(ctx, "Warn",  ZFit(), ZSPx(ctx, 20.f));
                ZUISpacer(ctx, 4.f);
                ZUIButton(ctx, "Error", ZFit(), ZSPx(ctx, 20.f));
                ZUIEndRow(ctx);
            }

            ZUIBeginScrollRegion(ctx, "##outlog", ZFill(), ZFill());
            ZUISpacer(ctx, 2.f);

            for (int i = 0; i < kLogCount; ++i)
            {
                const LogEntry& e = k_log[i];
                ZUIBeginRow(ctx, e.text, ZFill(), ZSPx(ctx, 19.f));
                ZUISpacer(ctx, 6.f);

                // Level badge
                const float* lc = (e.kind == 1) ? ctx->Theme.TextWarn
                                : (e.kind == 2) ? ctx->Theme.TextError
                                :                 ctx->Theme.TextDim;
                ZUILabel(ctx, e.level, lc);
                ZUILabel(ctx, e.text,  ctx->Theme.TextDim);
                ZUIEndRow(ctx);
            }

            ZUIEndScrollRegion(ctx);
        }
    };

    // ---------------------------------------------------------------
    // ContentBrowserPanel — asset grid with ZUIGridView
    // ---------------------------------------------------------------
    struct ContentBrowserPanel : ZUIPanelView
    {
        ContentBrowserPanel() { Title = "Content"; }

        struct Asset { const char* name; const char* ext; float col[4]; };
        static constexpr Asset k_assets[] = {
            { "SM_Cube",      ".zemesh",    {0.40f,0.78f,1.00f,1.f} },
            { "SM_Sphere",    ".zemesh",    {0.40f,0.78f,1.00f,1.f} },
            { "SM_Plane",     ".zemesh",    {0.40f,0.78f,1.00f,1.f} },
            { "T_Diffuse",    ".png",       {1.00f,0.65f,0.30f,1.f} },
            { "T_Normal",     ".png",       {1.00f,0.65f,0.30f,1.f} },
            { "M_Wood",       ".zemat",     {0.70f,0.85f,0.40f,1.f} },
            { "M_Metal",      ".zemat",     {0.70f,0.85f,0.40f,1.f} },
            { "DefaultScene", ".zescene",   {0.80f,0.50f,1.00f,1.f} },
            { "Env_Day",      ".hdr",       {0.30f,0.90f,0.90f,1.f} },
            { "SFX_Impact",   ".wav",       {0.98f,0.85f,0.20f,1.f} },
            { "SFX_Ambient",  ".wav",       {0.98f,0.85f,0.20f,1.f} },
            { "SK_Character", ".zemesh",    {0.40f,0.78f,1.00f,1.f} },
        };
        static constexpr int kCount = 12;
        int selected = -1;

        void BuildContent(ZUIContext* ctx, float[4]) override
        {
            // Toolbar
            {
                ZUIBox* tb = ZUIBeginRow(ctx, "##cbtb", ZFill(), ZPx(26.f));
                tb->Flags = tb->Flags | ZUI_DrawBackground;
                ZUIBoxSetColor(tb, 0.13f, 0.13f, 0.14f, 1.f);
                ZUISpacer(ctx, 6.f);
                ZUIButton(ctx, "Import",  ZFit(), ZPx(22.f));
                ZUISpacer(ctx, 4.f);
                ZUIButton(ctx, "New Folder", ZFit(), ZPx(22.f));
                ZUISpacer(ctx, 8.f);
                ZUILabel(ctx, "Assets /", ctx->Theme.TextDim);
                ZUIEndRow(ctx);
            }

            // Grid view — 88px wide cells
            ZUIBeginGridView(ctx, "##cbgrid", 88.f, 100.f);

            for (int i = 0; i < kCount; ++i)
            {
                const Asset& a = k_assets[i];
                char ik[32]; snprintf(ik, sizeof(ik), "##cb%d", i);

                if (ZUIGridViewNextItem(ctx, ik, selected == i))
                    selected = i;

                // Icon square (fills most of cell width)
                ZUISpacer(ctx, 6.f);
                ZUIBox* icon = ZUIPushBox(ctx, a.name, (uint32_t)strlen(a.name),
                                          ZUI_DrawBackground);
                icon->Size[0] = ZPx(60.f);
                icon->Size[1] = ZPx(60.f);
                ZUIBoxSetColorArr(icon, a.col);
                ZUIBoxSetCornerRadius(icon, 6.f);
                icon->EdgeSoftness = 0.5f;
                ZUIPopBox(ctx);
                ZUISpacer(ctx, 2.f);

                // Name label (truncated)
                ZUILabel(ctx, a.name, ctx->Theme.TextDim);

                ZUIGridViewEndItem(ctx);
            }

            ZUIEndGridView(ctx);
        }
    };

} // namespace Tetragrama::Panels
