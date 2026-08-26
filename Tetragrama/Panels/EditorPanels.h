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
        HierarchyPanel() {
            Title = "Hierarchy";
            TabColor[0]=0.30f; TabColor[1]=0.78f; TabColor[2]=0.30f; TabColor[3]=0.70f;
        }

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
        InspectorPanel() {
            Title = "Inspector";
            TabColor[0]=0.40f; TabColor[1]=0.65f; TabColor[2]=1.00f; TabColor[3]=0.70f;
        }

        // --- State ---
        bool  xfm_open   = true;
        bool  mesh_open  = true;
        bool  mat_open   = false;
        bool  light_open = false;
        float pos[3] = {0.f, 0.f, 0.f};
        float rot[3] = {0.f, 0.f, 0.f};
        float scl[3] = {1.f, 1.f, 1.f};
        float mat_color[4]   = {0.8f, 0.4f, 0.2f, 1.f};
        float emit_color[4]  = {0.f, 0.f, 0.f, 1.f};
        float roughness      = 0.5f;
        float metallic       = 0.f;
        int   mobility       = 2;  // 0=Static 1=Stationary 2=Movable
        bool  cast_shadow    = true;
        char  search_buf[64] = {};
        int   cat_sel        = 0;  // 0=All 1=General 2=Physics ...

        // Consistent column widths
        static constexpr float kLW  = 118.f; // label column
        static constexpr float kRH  = 22.f;  // row height
        static constexpr float kFW  = 62.f;  // xyz field width

        // ---- Helpers ----

        // Two-column property label (left side)
        void PLabel(ZUIContext* ctx, const char* label) const
        {
            char k[80]; snprintf(k, sizeof(k), "%s##pl", label);
            ZUIBox* lb = ZUIPushBox(ctx, k, (uint32_t)strlen(k), ZUI_DrawText);
            lb->Size[0] = ZPx(kLW); lb->Size[1] = ZPx(kRH);
            lb->Padding[0] = 10.f;
            lb->TextColor[0]=0.62f; lb->TextColor[1]=0.62f;
            lb->TextColor[2]=0.62f; lb->TextColor[3]=1.f;
            ZUIPopBox(ctx);
        }

        // Simple property row: label + single value (float/int/text)
        void PropRowBegin(ZUIContext* ctx, const char* label, const char* rkey)
        {
            char rk[80]; snprintf(rk, sizeof(rk), "##pr_%s", rkey);
            ZUIBox* row = ZUIBeginRow(ctx, rk, ZFill(), ZPx(kRH));
            row->Flags = row->Flags | ZUI_DrawBackground;
            bool hov = (ctx->HotKey == row->Key);
            ZUIBoxSetColor(row, hov ? 0.13f : 0.10f, hov ? 0.13f : 0.10f,
                                hov ? 0.13f : 0.10f, hov ? 1.f : 0.f);
            PLabel(ctx, label);
        }
        void PropRowEnd(ZUIContext* ctx) { ZUIEndRow(ctx); }

        // Unreal-style XYZ fields: each has a 2px colored left accent + dark input
        void XYZRow(ZUIContext* ctx, const char* label, const char* key, float v[3])
        {
            static const float kAcc[3][3] = {
                {0.86f, 0.22f, 0.22f},  // X red
                {0.22f, 0.72f, 0.22f},  // Y green
                {0.22f, 0.47f, 0.88f},  // Z blue
            };
            char rk[64]; snprintf(rk, sizeof(rk), "##xr_%s", key);
            ZUIBox* row = ZUIBeginRow(ctx, rk, ZFill(), ZPx(kRH));
            row->Flags = row->Flags | ZUI_DrawBackground;
            bool hov = (ctx->HotKey == row->Key);
            ZUIBoxSetColor(row, hov ? 0.13f : 0.f, hov ? 0.13f : 0.f,
                                hov ? 0.13f : 0.f, hov ? 1.f : 0.f);
            PLabel(ctx, label);
            for (int i = 0; i < 3; ++i)
            {
                if (i > 0) ZUISpacer(ctx, 2.f);
                // Colored 2px accent strip
                char ak[64]; snprintf(ak, sizeof(ak), "##xacc%d_%s", i, key);
                ZUIBox* acc = ZUIPushBox(ctx, ak, (uint32_t)strlen(ak), ZUI_DrawBackground);
                acc->Size[0] = ZPx(2.f); acc->Size[1] = ZPx(kRH - 4.f);
                ZUIBoxSetColor(acc, kAcc[i][0], kAcc[i][1], kAcc[i][2], 1.f);
                acc->EdgeSoftness = 0.f;
                ZUIPopBox(ctx);
                // Field
                char fk[64]; snprintf(fk, sizeof(fk), "##xf%d_%s", i, key);
                ZUIDragFloat(ctx, fk, &v[i], 0.1f, kFW);
            }
            ZUIEndRow(ctx);
        }

        // Mobility toggle: Static | Stationary | Movable
        void MobilityRow(ZUIContext* ctx)
        {
            PropRowBegin(ctx, "Mobility", "mob");
            static const char* kLabels[3] = {"Static", "Stationary", "Movable"};
            for (int i = 0; i < 3; ++i)
            {
                char bk[40]; snprintf(bk, sizeof(bk), "%s##mob%d", kLabels[i], i);
                float w = (i == 1) ? 68.f : 50.f; // Stationary is wider
                ZUIBox* btn = ZUIPushBox(ctx, bk, (uint32_t)strlen(bk),
                                         ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DrawText | ZUI_Clickable);
                btn->Size[0] = ZPx(w); btn->Size[1] = ZPx(kRH - 2.f);
                btn->TextAlign = ZUITextAlign::Center;
                bool sel = (mobility == i);
                bool bhov = (ctx->HotKey == btn->Key);
                if (sel)
                    ZUIBoxSetColor(btn, 0.f, 0.47f, 0.80f, 1.f);  // blue = selected
                else if (bhov)
                    ZUIBoxSetColor(btn, 0.22f, 0.22f, 0.25f, 1.f);
                else
                    ZUIBoxSetColor(btn, 0.14f, 0.14f, 0.15f, 1.f);
                btn->BorderColor[0]=0.25f; btn->BorderColor[1]=0.25f;
                btn->BorderColor[2]=0.28f; btn->BorderColor[3]=sel ? 0.f : 0.8f;
                btn->BorderThickness = 1.f;
                btn->TextColor[0]=btn->TextColor[1]=btn->TextColor[2]=1.f;
                btn->TextColor[3]= sel ? 1.f : 0.65f;
                ZUIBoxSetCornerRadius(btn, 2.f);
                btn->EdgeSoftness = 0.f;
                ZUISignal sig = ZUISignalFromBox(ctx, btn);
                ZUIPopBox(ctx);
                if (sig.Flags & ZUI_SignalClicked) mobility = i;
                if (i < 2) ZUISpacer(ctx, 1.f);
            }
            PropRowEnd(ctx);
        }

        // Section header — bold style, accent-colored text option
        void SectionHdr(ZUIContext* ctx, const char* label, bool* open,
                        float r=1.f, float g=1.f, float b=1.f)
        {
            char sk[64]; snprintf(sk, sizeof(sk), "##sec_%s", label);
            ZUIBox* hdr = ZUIBeginRow(ctx, sk, ZFill(), ZPx(kRH + 4.f));
            hdr->Flags = hdr->Flags | ZUI_DrawBackground | ZUI_Clickable;
            ZUIBoxSetColor(hdr, 0.15f, 0.15f, 0.16f, 1.f);
            ZUISpacer(ctx, 6.f);
            // Expand arrow
            const char* arrow = (*open) ? "v" : ">";
            ZUIBox* arr = ZUIPushBox(ctx, arrow, 1, ZUI_DrawText);
            arr->Size[0] = ZPx(14.f); arr->Size[1] = ZPx(kRH);
            arr->TextColor[0]=0.55f; arr->TextColor[1]=0.55f; arr->TextColor[2]=0.60f; arr->TextColor[3]=1.f;
            ZUIPopBox(ctx);
            ZUISpacer(ctx, 2.f);
            // Label
            uint32_t llen = (uint32_t)strlen(label);
            ZUIBox* lbl = ZUIPushBox(ctx, label, llen, ZUI_DrawText);
            lbl->Size[0] = ZFill(); lbl->Size[1] = ZPx(kRH);
            lbl->TextColor[0]=r; lbl->TextColor[1]=g; lbl->TextColor[2]=b; lbl->TextColor[3]=1.f;
            ZUIPopBox(ctx);
            ZUISignal sig = ZUISignalFromBox(ctx, hdr);
            ZUIEndRow(ctx);
            if (sig.Flags & ZUI_SignalClicked) *open = !(*open);
        }

        void BuildContent(ZUIContext* ctx, float[4]) override
        {
            // --- Actor header ---
            {
                ZUIBox* hdr = ZUIBeginRow(ctx, "##ah", ZFill(), ZPx(34.f));
                hdr->Flags = hdr->Flags | ZUI_DrawBackground;
                ZUIBoxSetColor(hdr, 0.14f, 0.14f, 0.16f, 1.f);
                ZUISpacer(ctx, 8.f);
                // Actor icon (colored square)
                ZUIBox* ic = ZUIPushBox(ctx, "##ahic", 6, ZUI_DrawBackground);
                ic->Size[0] = ZPx(14.f); ic->Size[1] = ZPx(14.f);
                ZUIBoxSetColor(ic, 0.25f, 0.65f, 1.f, 1.f);
                ZUIBoxSetCornerRadius(ic, 3.f); ic->EdgeSoftness = 0.5f;
                ZUIPopBox(ctx);
                ZUISpacer(ctx, 6.f);
                ZUILabel(ctx, "Cube", ctx->Theme.TextDefault);
                // Fill + buttons right
                char fk[] = "##ahfill"; ZUIBox* f = ZUIPushBox(ctx, fk, 8, ZUI_None);
                f->Size[0]=ZFill(); f->Size[1]=ZPx(34.f); ZUIPopBox(ctx);
                ZUISmallButton(ctx, "+ Add");
                ZUISpacer(ctx, 6.f);
                ZUIEndRow(ctx);
            }
            // Component breadcrumb
            {
                ZUIBox* cr = ZUIBeginRow(ctx, "##cr", ZFill(), ZPx(26.f));
                cr->Flags = cr->Flags | ZUI_DrawBackground;
                ZUIBoxSetColor(cr, 0.f, 0.47f, 0.80f, 0.20f); // blue tint = selected
                ZUISpacer(ctx, 8.f);
                ZUILabel(ctx, "Cube (Instance)", ctx->Theme.TextDefault);
                ZUIEndRow(ctx);
            }
            {
                ZUIBox* cr2 = ZUIBeginRow(ctx, "##cr2", ZFill(), ZPx(24.f));
                cr2->Flags = cr2->Flags | ZUI_DrawBackground;
                ZUIBoxSetColor(cr2, 0.f, 0.f, 0.f, 0.f);
                ZUISpacer(ctx, 20.f);
                ZUILabel(ctx, "StaticMeshComponent0", ctx->Theme.TextDim);
                char fk2[] = "##cr2fill"; ZUIBox* f2 = ZUIPushBox(ctx, fk2, 9, ZUI_None);
                f2->Size[0]=ZFill(); f2->Size[1]=ZPx(24.f); ZUIPopBox(ctx);
                ZUILabel(ctx, "Edit in C++", ctx->Theme.TextAccent);
                ZUISpacer(ctx, 8.f);
                ZUIEndRow(ctx);
            }

            // --- Search bar ---
            ZUISpacer(ctx, 2.f);
            {
                ZUIBox* sr = ZUIBeginRow(ctx, "##sbr", ZFill(), ZPx(26.f));
                sr->Flags = sr->Flags | ZUI_DrawBackground;
                ZUIBoxSetColor(sr, 0.10f, 0.10f, 0.10f, 1.f);
                ZUISpacer(ctx, 6.f);
                ZUILabel(ctx, "Q", ctx->Theme.TextDim); // search icon approx
                ZUISpacer(ctx, 4.f);
                ZUITextField(ctx, "##search", search_buf, sizeof(search_buf), 200.f);
                ZUIEndRow(ctx);
            }

            // --- Category tabs ---
            ZUISpacer(ctx, 2.f);
            {
                ZUIBox* cats = ZUIBeginRow(ctx, "##cats", ZFill(), ZPx(24.f));
                cats->Flags = cats->Flags | ZUI_DrawBackground;
                ZUIBoxSetColor(cats, 0.12f, 0.12f, 0.13f, 1.f);
                ZUISpacer(ctx, 4.f);
                static const char* kCats[] = { "General","Actor","Physics","Rendering","All" };
                for (int ci = 0; ci < 5; ++ci)
                {
                    char bk[32]; snprintf(bk, sizeof(bk), "%s##cat%d", kCats[ci], ci);
                    bool sel = (cat_sel == ci);
                    ZUIBox* cb = ZUIPushBox(ctx, bk, (uint32_t)strlen(bk),
                                            ZUI_DrawBackground | ZUI_DrawText | ZUI_DrawBorder | ZUI_Clickable);
                    cb->Size[0] = ZText(); cb->Size[1] = ZPx(20.f);
                    cb->Padding[0] = cb->Padding[2] = 8.f;
                    cb->TextAlign = ZUITextAlign::Center;
                    if (sel)
                        ZUIBoxSetColor(cb, 0.f, 0.47f, 0.80f, 1.f);
                    else
                        ZUIBoxSetColor(cb, 0.18f, 0.18f, 0.20f, 1.f);
                    cb->BorderColor[0]=0.28f; cb->BorderColor[1]=0.28f;
                    cb->BorderColor[2]=0.30f; cb->BorderColor[3]=0.60f;
                    cb->BorderThickness=1.f;
                    cb->TextColor[0]=cb->TextColor[1]=cb->TextColor[2]=1.f;
                    cb->TextColor[3]= sel ? 1.f : 0.60f;
                    ZUIBoxSetCornerRadius(cb, 3.f); cb->EdgeSoftness=0.f;
                    ZUISignal csig = ZUISignalFromBox(ctx, cb);
                    ZUIPopBox(ctx);
                    if (csig.Flags & ZUI_SignalClicked) cat_sel = ci;
                    ZUISpacer(ctx, 3.f);
                }
                ZUIEndRow(ctx);
            }
            ZUISeparator(ctx);

            ZUIBeginScrollRegion(ctx, "##insp", ZFill(), ZFill());

            // ---- Transform ----
            SectionHdr(ctx, "Transform", &xfm_open);
            if (xfm_open)
            {
                XYZRow(ctx, "Location", "loc", pos);
                XYZRow(ctx, "Rotation", "rot", rot);
                XYZRow(ctx, "Scale",    "scl", scl);
                MobilityRow(ctx);
            }
            ZUISeparator(ctx);

            // ---- Static Mesh Component ----
            SectionHdr(ctx, "Static Mesh Component", &mesh_open, 0.75f, 0.90f, 1.0f);
            if (mesh_open)
            {
                PropRowBegin(ctx, "Mesh", "mesh_v");
                if (ZUIBeginCombo(ctx, "##mesh_combo", "SM_Cube.zemesh", ZFill()))
                    ZUIEndCombo(ctx);
                PropRowEnd(ctx);

                PropRowBegin(ctx, "Cast Shadow", "cshadow");
                ZUIToggleButton(ctx, "On##cshadow", &cast_shadow, ZPx(36.f), ZPx(kRH-2.f));
                PropRowEnd(ctx);
            }
            ZUISeparator(ctx);

            // ---- Material ----
            SectionHdr(ctx, "Material", &mat_open, 0.9f, 0.75f, 0.55f);
            if (mat_open)
            {
                PropRowBegin(ctx, "Base Color", "basecol");
                ZUIColorEdit4(ctx, "##basecol", mat_color);
                PropRowEnd(ctx);

                PropRowBegin(ctx, "Emissive", "emissive");
                ZUIColorEdit4(ctx, "##emissive", emit_color);
                PropRowEnd(ctx);

                PropRowBegin(ctx, "Roughness", "roughness");
                ZUISliderFloat(ctx, "##roughness", &roughness, 0.f, 1.f, ZFill(), ZPx(kRH-2.f));
                PropRowEnd(ctx);

                PropRowBegin(ctx, "Metallic", "metallic");
                ZUISliderFloat(ctx, "##metallic", &metallic, 0.f, 1.f, ZFill(), ZPx(kRH-2.f));
                PropRowEnd(ctx);
            }
            ZUISeparator(ctx);

            // + Add Component button
            ZUISpacer(ctx, 6.f);
            {
                ZUIBox* row = ZUIBeginRow(ctx, "##addcmp", ZFill(), ZPx(28.f));
                row->Flags = row->Flags | ZUI_DrawBackground;
                ZUIBoxSetColor(row, 0.f, 0.f, 0.f, 0.f);
                ZUISpacer(ctx, 8.f);
                ZUIButton(ctx, "+ Add Component", ZFill(), ZPx(26.f));
                ZUISpacer(ctx, 8.f);
                ZUIEndRow(ctx);
            }

            // --- Performance plots ---
            ZUISeparatorText(ctx, "Performance");
            ZUISpacer(ctx, 4.f);
            {
                // Simulated frame time history (sine wave + noise approximation)
                static float s_frame_ms[64] = {};
                static float s_t = 0.f;
                s_t += ctx->DeltaTime;
                for (int i = 63; i > 0; --i) s_frame_ms[i] = s_frame_ms[i-1];
                s_frame_ms[0] = 16.67f + 3.f * sinf(s_t * 4.f)
                              + 1.5f * sinf(s_t * 11.f);
                ZUILabel(ctx, "Frame Time (ms)", ctx->Theme.TextDim);
                ZUIPlotLines(ctx, "##frametimes", s_frame_ms, 64,
                             10.f, 24.f, nullptr, ZFill(), ZPx(48.f));
            }
            ZUISpacer(ctx, 4.f);
            {
                // Simulated draw call histogram
                static float s_draws[16] = {
                    120,145,160,132,118,155,170,148,
                    135,162,140,128,152,165,144,138};
                ZUILabel(ctx, "Draw Calls", ctx->Theme.TextDim);
                ZUIPlotHistogram(ctx, "##drawcalls", s_draws, 16,
                                 100.f, 180.f, nullptr, ZFill(), ZPx(40.f));
            }
            ZUISpacer(ctx, 8.f);

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
        OutputPanel() {
            Title = "Output";
            TabColor[0]=0.95f; TabColor[1]=0.74f; TabColor[2]=0.14f; TabColor[3]=0.70f;
        }

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
        ContentBrowserPanel() {
            Title = "Content";
            TabColor[0]=0.40f; TabColor[1]=0.78f; TabColor[2]=1.00f; TabColor[3]=0.70f;
        }

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
