#pragma once
#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>

// ---------------------------------------------------------------
// Editor panel views with realistic dummy data.
// These connect to ZUIPanelManager's tab system.
// ---------------------------------------------------------------

namespace Tetragrama::Panels
{
    using namespace ZEngine::UI;

    // ---------------------------------------------------------------
    struct HierarchyPanel : ZUIPanelView
    {
        HierarchyPanel() { Title = "Hierarchy"; }

        struct Entry { const char* name; int depth; bool is_dir; int icon; };
        static constexpr Entry k_entries[] = {
            {"World",           0, true,  0},
            {"  DefaultScene",  1, true,  1},
            {"    DirectionalLight", 2, false, 2},
            {"    Camera",      2, false, 3},
            {"    Cube",        2, false, 4},
            {"    Sphere",      2, false, 4},
            {"    Plane",       2, false, 4},
        };
        static constexpr float kIconColors[][4] = {
            {0.5f, 0.5f, 0.9f, 1.f}, // world
            {0.3f, 0.8f, 0.3f, 1.f}, // scene
            {1.f, 0.9f, 0.2f, 1.f},  // light
            {0.2f, 0.8f, 1.f, 1.f},  // camera
            {0.7f, 0.5f, 1.f, 1.f},  // mesh
        };
        int selected = -1;

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            ZUIBeginScrollRegion(ctx, "##hier_scroll", ZFill(), ZFill());
            ZUISpacer(ctx, 2.f);
            for (int i = 0; i < 7; ++i)
            {
                const Entry& e = k_entries[i];
                char rk[32]; snprintf(rk, sizeof(rk), "##hr%d", i);
                ZUIBox* row = ZUIBeginRow(ctx, rk, ZFill(), ZSPx(ctx, 20.f));
                row->Flags  = row->Flags | ZUI_DrawBackground | ZUI_Clickable;
                if (selected == i) ZUIBoxSetColorArr(row, ctx->Theme.RowSelectedBg);
                else               ZUIBoxSetColor(row, 0.40f, 0.45f, 0.55f, 0.f);

                ZUISpacer(ctx, (float)(e.depth * 14) + 4.f);
                // type icon
                char ik[24]; snprintf(ik, sizeof(ik), "##ic%d", i);
                ZUIBox* ic = ZUIPushBox(ctx, ik, (uint32_t)strlen(ik), ZUI_DrawBackground);
                ic->Size[0] = ZPx(10.f); ic->Size[1] = ZPx(10.f);
                ZUIBoxSetColorArr(ic, kIconColors[e.icon]);
                ZUIBoxSetCornerRadius(ic, 2.f); ic->EdgeSoftness = 0.5f;
                ZUIPopBox(ctx);
                ZUISpacer(ctx, 4.f);
                ZUILabel(ctx, e.name, selected == i ? ctx->Theme.TextDefault : ctx->Theme.TextDim);

                ZUISignal sig = ZUISignalFromBox(ctx, row);
                ZUIEndRow(ctx);
                if (sig.Flags & ZUI_SignalClicked) selected = i;
            }
            ZUIEndScrollRegion(ctx);
        }
    };

    // ---------------------------------------------------------------
    struct InspectorPanel : ZUIPanelView
    {
        InspectorPanel() { Title = "Inspector"; }
        bool xfm_open = true, mesh_open = true, mat_open = false;

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            ZUIBeginScrollRegion(ctx, "##insp_scroll", ZFill(), ZFill());
            ZUISpacer(ctx, 4.f);

            // Actor header
            {
                ZUIBox* hdr = ZUIBeginColumn(ctx, "##ah", ZFill(), ZSPx(ctx, 36.f));
                hdr->Flags = hdr->Flags | ZUI_DrawBackground;
                ZUIBoxSetColor(hdr, 0.18f, 0.18f, 0.22f, 1.f);
                    ZUISpacer(ctx, 4.f);
                    ZUILabel(ctx, "Cube", ctx->Theme.TextDefault, ZUIFontSize::Header);
                    ZUILabel(ctx, "Static Mesh Actor", ctx->Theme.TextDim);
                ZUIEndColumn(ctx);
            }
            ZUISpacer(ctx, 4.f);
            ZUISeparator(ctx);

            // Transform
            if (ZUICollapsingHeader(ctx, "Transform", &xfm_open) && xfm_open)
            {
                static float pos[3] = {0.f, 0.f, 0.f};
                static float rot[3] = {0.f, 0.f, 0.f};
                static float scl[3] = {1.f, 1.f, 1.f};
                auto drag_row = [&](const char* lbl, const char* key, float* v) {
                    ZUIBeginRow(ctx, key, ZFill(), ZSPx(ctx, 22.f));
                    // Label column
                    ZUISpacer(ctx, 6.f);
                    ZUIBox* lbox = ZUIPushBox(ctx, lbl, (uint32_t)strlen(lbl), ZUI_DrawText);
                    lbox->Size[0] = ZPx(70.f); lbox->Size[1] = ZText();
                    lbox->TextColor[0]=ctx->Theme.TextDim[0]; lbox->TextColor[1]=ctx->Theme.TextDim[1];
                    lbox->TextColor[2]=ctx->Theme.TextDim[2]; lbox->TextColor[3]=ctx->Theme.TextDim[3];
                    ZUIPopBox(ctx);
                    ZUISpacer(ctx, 4.f);
                    char kx[32]; snprintf(kx, sizeof(kx), "##x%s", key);
                    char ky[32]; snprintf(ky, sizeof(ky), "##y%s", key);
                    char kz[32]; snprintf(kz, sizeof(kz), "##z%s", key);
                    ZUIDragFloat(ctx, kx, &v[0], 0.05f, 58.f);
                    ZUISpacer(ctx, 2.f);
                    ZUIDragFloat(ctx, ky, &v[1], 0.05f, 58.f);
                    ZUISpacer(ctx, 2.f);
                    ZUIDragFloat(ctx, kz, &v[2], 0.05f, 58.f);
                    ZUIEndRow(ctx);
                };
                drag_row("Location", "##loc", pos);
                drag_row("Rotation", "##rot", rot);
                drag_row("Scale",    "##scl", scl);
            }
            ZUISeparator(ctx);

            // Mesh
            if (ZUICollapsingHeader(ctx, "Mesh", &mesh_open) && mesh_open)
            {
                ZUIBeginRow(ctx, "##mr", ZFill(), ZSPx(ctx, 20.f));
                ZUILabel(ctx, "Mesh", ctx->Theme.TextDim);
                ZUISpacer(ctx, 8.f);
                ZUILabel(ctx, "SM_Cube.zemesh", ctx->Theme.TextDefault);
                ZUIEndRow(ctx);
            }
            ZUISeparator(ctx);

            // Material (collapsed)
            ZUICollapsingHeader(ctx, "Material", &mat_open);

            ZUIEndScrollRegion(ctx);
        }
    };

    // ---------------------------------------------------------------
    struct ViewportPanel : ZUIPanelView
    {
        ViewportPanel() { Title = "Viewport"; }
        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            ZUIBeginScrollRegion(ctx, "##vp_scroll", ZFill(), ZFill());
            ZUISpacer(ctx, 16.f);
            ZUILabel(ctx, "[ 3D Viewport ]", ctx->Theme.TextDim);
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, "Scene renderer will be added here.", ctx->Theme.TextDim);
            ZUISpacer(ctx, 4.f);
            ZUILabel(ctx, "Camera: X: 0.0  Y: 5.0  Z: -8.0", ctx->Theme.TextDim);
            ZUISpacer(ctx, 4.f);
            ZUILabel(ctx, "FPS: 90000  |  Drawcalls: 256", ctx->Theme.TextDim);
            ZUIEndScrollRegion(ctx);
        }
    };

    // ---------------------------------------------------------------
    struct OutputPanel : ZUIPanelView
    {
        OutputPanel() { Title = "Output"; }

        struct LogEntry { const char* text; float col[4]; };
        static constexpr LogEntry k_log[] = {
            {"[INFO]  Engine initialized",           {0.7f,0.7f,0.7f,1.f}},
            {"[INFO]  VFS mounted: /ZodiacEngine",   {0.7f,0.7f,0.7f,1.f}},
            {"[INFO]  GPU: Apple M2 Pro (MoltenVK)", {0.7f,0.7f,0.7f,1.f}},
            {"[INFO]  Scene loaded: DefaultScene",   {0.7f,0.7f,0.7f,1.f}},
            {"[WARN]  No skybox set",                {1.0f,0.8f,0.2f,1.f}},
            {"[INFO]  ZUI panel system ready",       {0.7f,0.7f,0.7f,1.f}},
            {"[INFO]  2 actors in scene",            {0.7f,0.7f,0.7f,1.f}},
        };

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            ZUIBeginScrollRegion(ctx, "##out_scroll", ZFill(), ZFill());
            ZUISpacer(ctx, 2.f);
            for (auto& e : k_log)
            {
                ZUISpacer(ctx, 1.f);
                ZUILabel(ctx, e.text, e.col);
            }
            ZUIEndScrollRegion(ctx);
        }
    };

    // ---------------------------------------------------------------
    struct ProjectPanel : ZUIPanelView
    {
        ProjectPanel() { Title = "Project"; }

        struct FileEntry { const char* name; bool is_dir; float col[4]; };
        static constexpr FileEntry k_files[] = {
            {"Assets",         true,  {1.0f,0.9f,0.2f,1.f}},
            {"  Meshes",       true,  {1.0f,0.9f,0.2f,1.f}},
            {"    SM_Cube.zemesh",   false, {0.4f,0.8f,1.f,1.f}},
            {"    SM_Sphere.zemesh", false, {0.4f,0.8f,1.f,1.f}},
            {"  Textures",     true,  {1.0f,0.9f,0.2f,1.f}},
            {"    T_Default.png",    false, {1.f,0.6f,0.3f,1.f}},
            {"  Materials",    true,  {1.0f,0.9f,0.2f,1.f}},
            {"Scenes",         true,  {1.0f,0.9f,0.2f,1.f}},
            {"  DefaultScene.zescene",false,{0.8f,0.5f,1.f,1.f}},
            {"project.json",   false, {0.5f,0.9f,0.5f,1.f}},
        };

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            ZUIBeginScrollRegion(ctx, "##proj_scroll", ZFill(), ZFill());
            ZUISpacer(ctx, 2.f);
            for (int i = 0; i < 10; ++i)
            {
                const FileEntry& f = k_files[i];
                char rk[24]; snprintf(rk, sizeof(rk), "##pf%d", i);
                ZUIBox* row = ZUIBeginRow(ctx, rk, ZFill(), ZSPx(ctx, 22.f));
                row->Flags = row->Flags | ZUI_DrawBackground | ZUI_Clickable;
                ZUIBoxSetColor(row, 0.4f, 0.4f, 0.5f, 0.f);

                // icon dot
                char ik[20]; snprintf(ik, sizeof(ik), "##fi%d", i);
                ZUIBox* ic = ZUIPushBox(ctx, ik, (uint32_t)strlen(ik), ZUI_DrawBackground);
                ic->Size[0] = ZPx(8.f); ic->Size[1] = ZPx(8.f);
                ZUIBoxSetColorArr(ic, f.col); ZUIBoxSetCornerRadius(ic, 2.f);
                ic->EdgeSoftness = 0.5f;
                ZUIPopBox(ctx);
                ZUISpacer(ctx, 4.f);
                ZUILabel(ctx, f.name, f.col);
                ZUIEndRow(ctx);
            }
            ZUIEndScrollRegion(ctx);
        }
    };

    // ---------------------------------------------------------------
    struct WatchPanel : ZUIPanelView
    {
        WatchPanel() { Title = "Watch"; }
        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            ZUIBeginScrollRegion(ctx, "##watch_scroll", ZFill(), ZFill());
            ZUISpacer(ctx, 2.f);
            const char* exprs[] = {"pos.x", "pos.y", "pos.z", "fps", "dt"};
            const char* vals[]  = {"0.000", "5.000", "-8.000", "90321", "0.011"};
            for (int i = 0; i < 5; ++i)
            {
                ZUIBeginRow(ctx, exprs[i], ZFill(), ZSPx(ctx, 20.f));
                ZUILabel(ctx, exprs[i], ctx->Theme.TextDim);
                ZUISpacer(ctx, 8.f);
                ZUILabel(ctx, vals[i], ctx->Theme.TextAccent);
                ZUIEndRow(ctx);
            }
            ZUIEndScrollRegion(ctx);
        }
    };

    // ---------------------------------------------------------------
    struct TypesPanel : ZUIPanelView
    {
        TypesPanel() { Title = "Types"; }
        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            ZUIBeginScrollRegion(ctx, "##types_scroll", ZFill(), ZFill());
            ZUISpacer(ctx, 2.f);
            const char* types[] = {"TransformComponent", "MeshComponent",
                                   "LightComponent", "CameraComponent",
                                   "ScriptComponent"};
            for (auto* t : types)
            {
                ZUIBeginRow(ctx, t, ZFill(), ZSPx(ctx, 20.f));
                ZUILabel(ctx, t, ctx->Theme.TextDim);
                ZUIEndRow(ctx);
            }
            ZUIEndScrollRegion(ctx);
        }
    };

} // namespace Tetragrama::Panels
