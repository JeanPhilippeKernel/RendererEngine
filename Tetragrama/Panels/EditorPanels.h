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
    // HierarchyPanel — scene actor tree using ZUITreeView
    // ================================================================
    struct HierarchyPanel : ZUIPanelView
    {
        HierarchyPanel() {
            Title = "Hierarchy";
            TabColor[0]=0.30f; TabColor[1]=0.78f; TabColor[2]=0.30f; TabColor[3]=0.70f;
        }

        int  selected           = -1;
        bool states_seeded      = false; // open states pre-seeded on first frame
        char search_buf[64]     = {};

        struct Actor {
            const char* name;
            int         parent; // -1 = root
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
        bool open[kCount] = {true,true,false,false,false,false,false,false};

        void BuildContent(ZUIContext* ctx, float[4]) override
        {
            // --- Header toolbar ---
            {
                ZUIBox* tb = ZUIBeginRow(ctx, "##htb", ZFill(), ZPx(26.f));
                tb->Flags = tb->Flags | ZUI_DrawBackground;
                ZUIBoxSetColor(tb, 0.12f, 0.12f, 0.13f, 1.f);
                ZUISpacer(ctx, 6.f);
                ZUITextField(ctx, "##hsearch", search_buf, sizeof(search_buf), 160.f);
                ZUISpacer(ctx, 6.f);
                ZUISmallButton(ctx, "+");
                ZUISpacer(ctx, 4.f);
                ZUIEndRow(ctx);
            }

            // Pre-seed open states once so root nodes start expanded
            if (!states_seeded)
            {
                auto open_node = [&](const char* lbl, int depth)
                {
                    uint64_t h = ZUIHashStr(lbl, (uint32_t)strlen(lbl)) ^ (uint64_t)depth;
                    ZUIPersistentState* s = ZUIStateGetOrInsert(&ctx->StateStore, h);
                    if (s) s->UserData = 1.f;
                };
                open_node("World",        0);
                open_node("DefaultScene", 1);
                states_seeded = true;
            }

            ZUIBeginTreeView(ctx, "##hier");

            // Render only depth-0 and depth-1 actors (simple 2-level tree)
            for (int i = 0; i < kCount; ++i)
            {
                if (k_actors[i].parent != -1) { continue; } // skip non-roots

                bool is_sel = (selected == i);
                if (ZUITreeViewBeginNode(ctx, k_actors[i].name, is_sel, k_actors[i].icon, true))
                {
                    if (is_sel) selected = i;
                    for (int j = 0; j < kCount; ++j)
                    {
                        if (k_actors[j].parent != i) { continue; }
                        bool csel = (selected == j);
                        // Check if j has its own children
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
    // InspectorPanel — component property editor, Unreal Details style
    // ================================================================
    struct InspectorPanel : ZUIPanelView
    {
        InspectorPanel() {
            Title = "Inspector";
            TabColor[0]=0.40f; TabColor[1]=0.65f; TabColor[2]=1.00f; TabColor[3]=0.70f;
        }

        bool  xfm_open  = true;
        bool  mesh_open = true;
        bool  mat_open  = true;
        bool  light_open= false;
        float pos[3]    = {0.f, 0.f, 0.f};
        float rot[3]    = {0.f, 0.f, 0.f};
        float scl[3]    = {1.f, 1.f, 1.f};
        float mat_color[4]  = {0.8f, 0.4f, 0.2f, 1.f};
        float emit_color[4] = {0.f,  0.f,  0.f,  1.f};
        float roughness = 0.45f;
        float metallic  = 0.f;
        int   mobility  = 2;
        bool  cast_shadow = true;
        char  search_buf[64] = {};
        int   cat_sel = 0;

        static constexpr float kLW = 118.f;
        static constexpr float kRH = 24.f; // VS Code row height — slightly roomier

        void PropRow(ZUIContext* ctx, const char* label, const char* rkey)
        {
            char rk[80]; snprintf(rk, sizeof(rk), "##pr_%s", rkey);
            ZUIBox* row = ZUIBeginRow(ctx, rk, ZFill(), ZPx(kRH));
            row->Flags = row->Flags | ZUI_DrawBackground;
            bool hov = (ctx->HotKey == row->Key);
            ZUIBoxSetColor(row, hov?0.13f:0.f, hov?0.13f:0.f, hov?0.13f:0.f, hov?1.f:0.f);
            // Label column
            char lk[80]; snprintf(lk, sizeof(lk), "%s##lbl_%s", label, rkey);
            ZUIBox* lb = ZUIPushBox(ctx, lk, (uint32_t)strlen(lk), ZUI_DrawText);
            lb->Size[0]=ZPx(kLW); lb->Size[1]=ZPx(kRH); lb->Padding[0]=8.f;
            lb->TextColor[0]=0.62f; lb->TextColor[1]=0.62f;
            lb->TextColor[2]=0.62f; lb->TextColor[3]=1.f;
            ZUIPopBox(ctx);
        }
        void PropRowEnd(ZUIContext* ctx) { ZUIEndRow(ctx); }

        void XYZRow(ZUIContext* ctx, const char* label, const char* key, float v[3])
        {
            static const float kAcc[3][3]={{0.85f,0.22f,0.22f},{0.22f,0.72f,0.22f},{0.22f,0.45f,0.85f}};
            char rk[64]; snprintf(rk, sizeof(rk), "##xr_%s", key);
            ZUIBox* row = ZUIBeginRow(ctx, rk, ZFill(), ZPx(kRH));
            row->Flags = row->Flags | ZUI_DrawBackground;
            ZUIBoxSetColor(row, 0.f, 0.f, 0.f, 0.f);
            char lk[64]; snprintf(lk, sizeof(lk), "%s##xl_%s", label, key);
            ZUIBox* lb = ZUIPushBox(ctx, lk, (uint32_t)strlen(lk), ZUI_DrawText);
            lb->Size[0]=ZPx(kLW); lb->Size[1]=ZPx(kRH); lb->Padding[0]=8.f;
            lb->TextColor[0]=0.62f; lb->TextColor[1]=0.62f; lb->TextColor[2]=0.62f; lb->TextColor[3]=1.f;
            ZUIPopBox(ctx);
            for (int i=0; i<3; ++i)
            {
                if (i>0) ZUISpacer(ctx,2.f);
                char ak[64]; snprintf(ak,sizeof(ak),"##xacc%d_%s",i,key);
                ZUIBox* acc=ZUIPushBox(ctx,ak,(uint32_t)strlen(ak),ZUI_DrawBackground);
                acc->Size[0]=ZPx(2.f); acc->Size[1]=ZPx(kRH-4.f);
                ZUIBoxSetColor(acc,kAcc[i][0],kAcc[i][1],kAcc[i][2],1.f);
                acc->EdgeSoftness=0.f; ZUIPopBox(ctx);
                char fk[64]; snprintf(fk,sizeof(fk),"##xf%d_%s",i,key);
                ZUIDragFloat(ctx,fk,&v[i],0.1f,58.f);
            }
            ZUIEndRow(ctx);
        }

        void MobilityRow(ZUIContext* ctx)
        {
            PropRow(ctx,"Mobility","mob");
            static const char* kLbl[3]={"Static","Stationary","Movable"};
            static const float kW[3]={48.f,66.f,55.f};
            for (int i=0; i<3; ++i)
            {
                if (i>0) ZUISpacer(ctx,1.f);
                char bk[40]; snprintf(bk,sizeof(bk),"%s##mob%d",kLbl[i],i);
                bool sel=(mobility==i);
                ZUIBox* btn=ZUIPushBox(ctx,bk,(uint32_t)strlen(bk),
                    ZUI_DrawBackground|ZUI_DrawBorder|ZUI_DrawText|ZUI_Clickable);
                btn->Size[0]=ZPx(kW[i]); btn->Size[1]=ZPx(kRH-2.f);
                btn->TextAlign=ZUITextAlign::Center;
                bool bh=(ctx->HotKey==btn->Key);
                if (sel) ZUIBoxSetColor(btn,0.f,0.47f,0.80f,1.f);
                else if (bh) ZUIBoxSetColor(btn,0.22f,0.22f,0.25f,1.f);
                else ZUIBoxSetColor(btn,0.14f,0.14f,0.15f,1.f);
                btn->BorderColor[0]=0.25f; btn->BorderColor[1]=0.25f;
                btn->BorderColor[2]=0.28f; btn->BorderColor[3]=sel?0.f:0.8f;
                btn->BorderThickness=1.f;
                btn->TextColor[0]=btn->TextColor[1]=btn->TextColor[2]=1.f;
                btn->TextColor[3]=sel?1.f:0.65f;
                ZUIBoxSetCornerRadius(btn,2.f); btn->EdgeSoftness=0.f;
                ZUISignal sig=ZUISignalFromBox(ctx,btn);
                ZUIPopBox(ctx);
                if (sig.Flags&ZUI_SignalClicked) mobility=i;
            }
            PropRowEnd(ctx);
        }

        void SectionHdr(ZUIContext* ctx, const char* label, bool* open,
                        float r=1.f, float g=1.f, float b=1.f)
        {
            char sk[64]; snprintf(sk,sizeof(sk),"##sec_%s",label);
            ZUIBox* hdr=ZUIBeginRow(ctx,sk,ZFill(),ZPx(kRH+4.f));
            hdr->Flags=hdr->Flags|ZUI_DrawBackground|ZUI_Clickable;
            ZUIBoxSetColor(hdr,0.15f,0.15f,0.16f,1.f);
            ZUISpacer(ctx,6.f);
            char ak[64]; snprintf(ak,sizeof(ak),"##sa_%s",label);
            ZUIBox* arr=ZUIPushBox(ctx,ak,(uint32_t)strlen(ak),ZUI_DrawTriArrow);
            arr->Size[0]=ZPx(14.f); arr->Size[1]=ZPx(kRH);
            float ac[4]={0.55f,0.55f,0.60f,1.f}; arr->TextColor[0]=ac[0]; arr->TextColor[1]=ac[1]; arr->TextColor[2]=ac[2]; arr->TextColor[3]=ac[3];
            { auto* ps=ZUIStateGetOrInsert(&ctx->StateStore,arr->Key); if(ps) ps->UserData=(*open)?1.f:0.f; }
            ZUIPopBox(ctx);
            ZUISpacer(ctx,2.f);
            uint32_t llen=(uint32_t)strlen(label);
            ZUIBox* lbl=ZUIPushBox(ctx,label,llen,ZUI_DrawText);
            lbl->Size[0]=ZFill(); lbl->Size[1]=ZPx(kRH);
            lbl->TextColor[0]=r; lbl->TextColor[1]=g; lbl->TextColor[2]=b; lbl->TextColor[3]=1.f;
            ZUIPopBox(ctx);
            ZUISignal sig=ZUISignalFromBox(ctx,hdr);
            ZUIEndRow(ctx);
            if (sig.Flags&ZUI_SignalClicked) *open=!(*open);
        }

        void BuildContent(ZUIContext* ctx, float[4]) override
        {
            // Actor header
            {
                ZUIBox* hdr=ZUIBeginRow(ctx,"##ah",ZFill(),ZPx(36.f));
                hdr->Flags=hdr->Flags|ZUI_DrawBackground;
                ZUIBoxSetColor(hdr,0.14f,0.14f,0.16f,1.f);
                ZUISpacer(ctx,8.f);
                ZUIBox* ic=ZUIPushBox(ctx,"##ahic",6,ZUI_DrawBackground);
                ic->Size[0]=ZPx(16.f); ic->Size[1]=ZPx(16.f);
                ZUIBoxSetColor(ic,0.25f,0.65f,1.f,1.f);
                ZUIBoxSetCornerRadius(ic,3.f); ic->EdgeSoftness=0.5f;
                ZUIPopBox(ctx);
                ZUISpacer(ctx,6.f);
                ZUIBeginColumn(ctx,"##ahtxt",ZFit(),ZFit());
                    ZUILabel(ctx,"Cube",ctx->Theme.TextDefault,ZUIFontSize::Header);
                    ZUILabel(ctx,"StaticMeshActor",ctx->Theme.TextDim);
                ZUIEndColumn(ctx);
                char fk[]="##ahfill";
                ZUIBox* f=ZUIPushBox(ctx,fk,8,ZUI_None);
                f->Size[0]=ZFill(); f->Size[1]=ZPx(36.f); ZUIPopBox(ctx);
                ZUISmallButton(ctx,"+ Add");
                ZUISpacer(ctx,6.f);
                ZUIEndRow(ctx);
            }
            // Breadcrumb
            {
                ZUIBox* cr=ZUIBeginRow(ctx,"##cr",ZFill(),ZPx(22.f));
                cr->Flags=cr->Flags|ZUI_DrawBackground;
                ZUIBoxSetColor(cr,0.f,0.47f,0.80f,0.18f);
                ZUISpacer(ctx,8.f);
                ZUILabel(ctx,"Cube (Instance)",ctx->Theme.TextDefault);
                ZUIEndRow(ctx);
            }
            {
                ZUIBox* cr2=ZUIBeginRow(ctx,"##cr2",ZFill(),ZPx(22.f));
                cr2->Flags=cr2->Flags|ZUI_DrawBackground;
                ZUIBoxSetColor(cr2,0.f,0.f,0.f,0.f);
                ZUISpacer(ctx,20.f);
                ZUILabel(ctx,"StaticMeshComponent0",ctx->Theme.TextDim);
                char fk2[]="##cr2f"; ZUIBox* f2=ZUIPushBox(ctx,fk2,6,ZUI_None);
                f2->Size[0]=ZFill(); f2->Size[1]=ZPx(22.f); ZUIPopBox(ctx);
                ZUILabel(ctx,"Edit in C++",ctx->Theme.TextAccent);
                ZUISpacer(ctx,8.f);
                ZUIEndRow(ctx);
            }
            // Search + category tabs
            ZUISpacer(ctx,2.f);
            {
                ZUIBox* sr=ZUIBeginRow(ctx,"##sr",ZFill(),ZPx(24.f));
                sr->Flags=sr->Flags|ZUI_DrawBackground;
                ZUIBoxSetColor(sr,0.10f,0.10f,0.10f,1.f);
                ZUISpacer(ctx,6.f);
                ZUILabel(ctx,"Q",ctx->Theme.TextDim);
                ZUISpacer(ctx,4.f);
                ZUITextField(ctx,"##inspsearch",search_buf,sizeof(search_buf),180.f);
                ZUIEndRow(ctx);
            }
            ZUISpacer(ctx,2.f);
            {
                ZUIBox* cats=ZUIBeginRow(ctx,"##cats",ZFill(),ZPx(24.f));
                cats->Flags=cats->Flags|ZUI_DrawBackground;
                ZUIBoxSetColor(cats,0.12f,0.12f,0.13f,1.f);
                ZUISpacer(ctx,4.f);
                static const char* kC[]={"General","Actor","Physics","Rendering","All"};
                for (int ci=0; ci<5; ++ci)
                {
                    char bk[32]; snprintf(bk,sizeof(bk),"%s##cat%d",kC[ci],ci);
                    bool sel=(cat_sel==ci);
                    ZUIBox* cb=ZUIPushBox(ctx,bk,(uint32_t)strlen(bk),
                        ZUI_DrawBackground|ZUI_DrawText|ZUI_DrawBorder|ZUI_Clickable);
                    cb->Size[0]=ZText(); cb->Size[1]=ZPx(20.f);
                    cb->Padding[0]=cb->Padding[2]=8.f; cb->TextAlign=ZUITextAlign::Center;
                    if (sel) ZUIBoxSetColor(cb,0.f,0.47f,0.80f,1.f);
                    else ZUIBoxSetColor(cb,0.18f,0.18f,0.20f,1.f);
                    cb->BorderColor[0]=0.28f; cb->BorderColor[1]=0.28f;
                    cb->BorderColor[2]=0.30f; cb->BorderColor[3]=0.60f;
                    cb->BorderThickness=1.f;
                    cb->TextColor[0]=cb->TextColor[1]=cb->TextColor[2]=1.f;
                    cb->TextColor[3]=sel?1.f:0.60f;
                    ZUIBoxSetCornerRadius(cb,3.f); cb->EdgeSoftness=0.f;
                    ZUISignal csig=ZUISignalFromBox(ctx,cb); ZUIPopBox(ctx);
                    if (csig.Flags&ZUI_SignalClicked) cat_sel=ci;
                    ZUISpacer(ctx,3.f);
                }
                ZUIEndRow(ctx);
            }
            ZUISeparator(ctx);

            ZUIBeginScrollRegion(ctx,"##insp",ZFill(),ZFill());

            // Transform
            SectionHdr(ctx,"Transform",&xfm_open);
            if (xfm_open)
            {
                XYZRow(ctx,"Location","loc",pos);
                XYZRow(ctx,"Rotation","rot",rot);
                XYZRow(ctx,"Scale",   "scl",scl);
                MobilityRow(ctx);
            }
            ZUISeparator(ctx);

            // Static Mesh
            SectionHdr(ctx,"Static Mesh",&mesh_open,0.75f,0.90f,1.0f);
            if (mesh_open)
            {
                PropRow(ctx,"Mesh","mesh_v");
                if (ZUIBeginCombo(ctx,"##mesh_combo","SM_Cube.zemesh",ZFill()))
                    ZUIEndCombo(ctx);
                PropRowEnd(ctx);

                PropRow(ctx,"Cast Shadow","cshadow");
                ZUIToggleButton(ctx,"On##cshadow",&cast_shadow,ZPx(36.f),ZPx(kRH-2.f));
                PropRowEnd(ctx);

                PropRow(ctx,"LOD","lod_v");
                ZUILabel(ctx,"Auto",ctx->Theme.TextAccent);
                PropRowEnd(ctx);
            }
            ZUISeparator(ctx);

            // Material
            SectionHdr(ctx,"Material",&mat_open,0.90f,0.75f,0.55f);
            if (mat_open)
            {
                PropRow(ctx,"Base Color","basecol");
                ZUIColorEdit4(ctx,"##basecol",mat_color);
                PropRowEnd(ctx);

                PropRow(ctx,"Emissive","emissive");
                ZUIColorEdit4(ctx,"##emissive",emit_color);
                PropRowEnd(ctx);

                PropRow(ctx,"Roughness","roughness");
                ZUISliderFloat(ctx,"##roughness",&roughness,0.f,1.f,ZFill(),ZPx(kRH-2.f));
                PropRowEnd(ctx);

                PropRow(ctx,"Metallic","metallic");
                ZUISliderFloat(ctx,"##metallic",&metallic,0.f,1.f,ZFill(),ZPx(kRH-2.f));
                PropRowEnd(ctx);
            }
            ZUISeparator(ctx);

            // Add Component
            ZUISpacer(ctx,6.f);
            ZUIBeginRow(ctx,"##addcmp",ZFill(),ZPx(28.f));
                ZUISpacer(ctx,8.f);
                ZUIButton(ctx,"+ Add Component",ZFill(),ZPx(26.f));
                ZUISpacer(ctx,8.f);
            ZUIEndRow(ctx);

            ZUIEndScrollRegion(ctx);
        }
    };

    // ================================================================
    // ViewportPanel — 3D scene view with toolbar
    // ================================================================
    struct ViewportPanel : ZUIPanelView
    {
        ViewportPanel() { Title = "Viewport"; }

        bool snap_enabled = false;
        int  view_mode    = 0; // 0=Perspective 1=Top 2=Front
        int  shading_mode = 0; // 0=Lit 1=Unlit 2=Wireframe

        static constexpr float k_fps_history[32] = {
            90,88,91,87,92,89,85,93,90,88,
            94,87,89,91,88,92,90,86,93,89,
            91,88,90,92,87,93,88,91,89,90,
            92,88
        };

        void BuildContent(ZUIContext* ctx, float[4]) override
        {
            // Top toolbar
            {
                ZUIBox* tb=ZUIBeginRow(ctx,"##vptb",ZFill(),ZPx(28.f));
                tb->Flags=tb->Flags|ZUI_DrawBackground;
                ZUIBoxSetColor(tb,0.130f,0.130f,0.138f,1.f);
                ZUISpacer(ctx,6.f);

                // View mode toggle
                static const char* kVm[]={"Perspective","Top","Front"};
                for (int i=0; i<3; ++i)
                {
                    char bk[32]; snprintf(bk,sizeof(bk),"%s##vm%d",kVm[i],i);
                    bool sel=(view_mode==i);
                    ZUIBox* btn=ZUIPushBox(ctx,bk,(uint32_t)strlen(bk),
                        ZUI_DrawBackground|ZUI_DrawText|ZUI_Clickable);
                    btn->Size[0]=ZText(); btn->Size[1]=ZPx(22.f);
                    btn->Padding[0]=btn->Padding[2]=8.f;
                    if (sel) ZUIBoxSetColor(btn,0.f,0.47f,0.80f,0.70f);
                    else ZUIBoxSetColor(btn,0.20f,0.20f,0.22f,1.f);
                    btn->TextColor[0]=btn->TextColor[1]=btn->TextColor[2]=1.f;
                    btn->TextColor[3]=sel?1.f:0.70f;
                    ZUIBoxSetCornerRadius(btn,2.f); btn->EdgeSoftness=0.f;
                    ZUISignal s=ZUISignalFromBox(ctx,btn); ZUIPopBox(ctx);
                    if (s.Flags&ZUI_SignalClicked) view_mode=i;
                    if (i<2) ZUISpacer(ctx,1.f);
                }
                ZUISpacer(ctx,10.f);
                ZUISeparatorText(ctx,"|");
                ZUISpacer(ctx,10.f);

                // Shading
                static const char* kSh[]={"Lit","Unlit","Wireframe"};
                for (int i=0; i<3; ++i)
                {
                    char bk[32]; snprintf(bk,sizeof(bk),"%s##sh%d",kSh[i],i);
                    bool sel=(shading_mode==i);
                    ZUIBox* btn=ZUIPushBox(ctx,bk,(uint32_t)strlen(bk),
                        ZUI_DrawBackground|ZUI_DrawText|ZUI_Clickable);
                    btn->Size[0]=ZText(); btn->Size[1]=ZPx(22.f);
                    btn->Padding[0]=btn->Padding[2]=7.f;
                    if (sel) ZUIBoxSetColor(btn,0.f,0.47f,0.80f,0.70f);
                    else ZUIBoxSetColor(btn,0.20f,0.20f,0.22f,1.f);
                    btn->TextColor[0]=btn->TextColor[1]=btn->TextColor[2]=1.f;
                    btn->TextColor[3]=sel?1.f:0.70f;
                    ZUIBoxSetCornerRadius(btn,2.f); btn->EdgeSoftness=0.f;
                    ZUISignal s=ZUISignalFromBox(ctx,btn); ZUIPopBox(ctx);
                    if (s.Flags&ZUI_SignalClicked) shading_mode=i;
                    if (i<2) ZUISpacer(ctx,1.f);
                }
                ZUISpacer(ctx,10.f);
                ZUIToggleButton(ctx,"Snap##vsnap",&snap_enabled,ZText(),ZPx(22.f));
                ZUIEndRow(ctx);
            }

            // Scene area
            ZUIBox* scene=ZUIBeginColumn(ctx,"##vpscene",ZFill(),ZFill());
            scene->Flags=scene->Flags|ZUI_DrawBackground;
            ZUIBoxSetColor(scene,0.090f,0.090f,0.095f,1.f);

                // Overlay info
                ZUISpacer(ctx,16.f);
                float hint[4]={0.30f,0.30f,0.32f,1.f};
                ZUILabel(ctx,"[ 3D Scene Viewport ]",ctx->Theme.TextDim);
                ZUISpacer(ctx,6.f);
                ZUILabel(ctx,"Scene renderer will attach here.",hint);
                ZUISpacer(ctx,4.f);
                ZUILabel(ctx,"Camera: X 0.0  Y 5.0  Z -8.0",hint);

                // FPS sparkline plot
                ZUISpacer(ctx,8.f);
                {
                    ZUIBox* row=ZUIBeginRow(ctx,"##fps_row",ZFill(),ZFit());
                    row->Flags=row->Flags;
                    ZUISpacer(ctx,8.f);
                    ZUILabel(ctx,"FPS: 90",hint);
                    ZUISpacer(ctx,8.f);
                    ZUIPlotLines(ctx,"##fpsspark",k_fps_history,32,
                                 60.f,120.f,nullptr,ZPx(80.f),ZPx(20.f));
                    ZUIEndRow(ctx);
                }

            ZUIEndColumn(ctx);
        }
    };
    constexpr float ViewportPanel::k_fps_history[32];

    // ================================================================
    // OutputPanel — console log with filters and timestamps
    // ================================================================
    struct OutputPanel : ZUIPanelView
    {
        OutputPanel() {
            Title = "Output";
            TabColor[0]=0.95f; TabColor[1]=0.74f; TabColor[2]=0.14f; TabColor[3]=0.70f;
        }

        int  filter = 0; // 0=All 1=Info 2=Warn 3=Error
        bool auto_scroll = true;

        struct LogEntry { const char* time; const char* level; const char* text; int kind; };
        // kind: 0=info  1=warn  2=error
        static constexpr LogEntry k_log[] = {
            {"15:20:01","[INFO] ","Engine initialized successfully",          0},
            {"15:20:01","[INFO] ","VFS mounted: /ZodiacEngine",               0},
            {"15:20:02","[INFO] ","GPU: Apple M2 Pro  —  MoltenVK 1.3",      0},
            {"15:20:02","[INFO] ","Vulkan swapchain: 3024x1834  (Retina)",    0},
            {"15:20:03","[INFO] ","Scene loaded: DefaultScene",               0},
            {"15:20:03","[WARN] ","No skybox texture set — using atmosphere",  1},
            {"15:20:03","[INFO] ","ZUI panel system ready  (UIScale 2.0)",    0},
            {"15:20:03","[INFO] ","Draw list renderer active",                0},
            {"15:20:04","[INFO] ","2 actors in scene",                        0},
            {"15:20:05","[WARN] ","Shader cache miss: recompiling 3 shaders", 1},
            {"15:20:06","[INFO] ","Ready.",                                   0},
        };
        static constexpr int kLogCount = 11;

        void BuildContent(ZUIContext* ctx, float[4]) override
        {
            // Filter bar
            {
                ZUIBox* fb=ZUIBeginRow(ctx,"##outfb",ZFill(),ZPx(28.f));
                fb->Flags=fb->Flags|ZUI_DrawBackground;
                ZUIBoxSetColor(fb,0.13f,0.13f,0.14f,1.f);
                ZUISpacer(ctx,6.f);

                static const char* kF[]={"All","Info","Warn","Error"};
                static const float kFCol[4][4]={
                    {0.70f,0.70f,0.70f,1.f},  // All — grey
                    {0.53f,0.80f,0.53f,1.f},  // Info — green
                    {0.95f,0.74f,0.14f,1.f},  // Warn — amber
                    {0.94f,0.33f,0.31f,1.f},  // Error — red
                };
                for (int i=0; i<4; ++i)
                {
                    char bk[24]; snprintf(bk,sizeof(bk),"%s##flt%d",kF[i],i);
                    bool sel=(filter==i);
                    ZUIBox* btn=ZUIPushBox(ctx,bk,(uint32_t)strlen(bk),
                        ZUI_DrawBackground|ZUI_DrawText|ZUI_DrawBorder|ZUI_Clickable);
                    btn->Size[0]=ZText(); btn->Size[1]=ZPx(22.f);
                    btn->Padding[0]=btn->Padding[2]=8.f;
                    if (sel)
                    {
                        float bg[4]={kFCol[i][0]*0.3f,kFCol[i][1]*0.3f,kFCol[i][2]*0.3f,1.f};
                        ZUIBoxSetColorArr(btn,bg);
                        btn->BorderColor[0]=kFCol[i][0]; btn->BorderColor[1]=kFCol[i][1];
                        btn->BorderColor[2]=kFCol[i][2]; btn->BorderColor[3]=0.9f;
                    }
                    else { ZUIBoxSetColor(btn,0.18f,0.18f,0.20f,1.f);
                           btn->BorderColor[0]=0.28f; btn->BorderColor[1]=0.28f;
                           btn->BorderColor[2]=0.30f; btn->BorderColor[3]=0.6f; }
                    btn->BorderThickness=1.f;
                    btn->TextColor[0]=kFCol[i][0]; btn->TextColor[1]=kFCol[i][1];
                    btn->TextColor[2]=kFCol[i][2]; btn->TextColor[3]=1.f;
                    ZUIBoxSetCornerRadius(btn,3.f); btn->EdgeSoftness=0.f;
                    ZUISignal s=ZUISignalFromBox(ctx,btn); ZUIPopBox(ctx);
                    if (s.Flags&ZUI_SignalClicked) filter=i;
                    ZUISpacer(ctx,4.f);
                }
                // Fill + Clear button
                char fk[]="##outfill"; ZUIBox* f=ZUIPushBox(ctx,fk,8,ZUI_None);
                f->Size[0]=ZFill(); f->Size[1]=ZPx(28.f); ZUIPopBox(ctx);
                ZUISmallButton(ctx,"Clear");
                ZUISpacer(ctx,6.f);
                ZUIEndRow(ctx);
            }

            ZUIBeginScrollRegion(ctx,"##outlog",ZFill(),ZFill());
            ZUISpacer(ctx,2.f);

            for (int i=0; i<kLogCount; ++i)
            {
                const LogEntry& e=k_log[i];
                // Apply filter
                if (filter==1 && e.kind!=0) continue;
                if (filter==2 && e.kind!=1) continue;
                if (filter==3 && e.kind!=2) continue;

                // Row background alternates
                char rk[32]; snprintf(rk,sizeof(rk),"##ol%d",i);
                ZUIBox* row=ZUIBeginRow(ctx,rk,ZFill(),ZPx(20.f));
                row->Flags=row->Flags|ZUI_DrawBackground;
                bool hov=(ctx->HotKey==row->Key);
                ZUIBoxSetColor(row,(i%2)?0.11f:0.f,(i%2)?0.11f:0.f,(i%2)?0.11f:0.f,
                               hov?1.f:(i%2)?1.f:0.f);

                ZUISpacer(ctx,6.f);
                // Timestamp
                float tc[4]={0.38f,0.38f,0.40f,1.f};
                ZUILabel(ctx,e.time,tc);
                ZUISpacer(ctx,6.f);
                // Level badge
                const float* lc=(e.kind==1)?ctx->Theme.TextWarn
                               :(e.kind==2)?ctx->Theme.TextError
                               :ctx->Theme.TextDim;
                ZUILabel(ctx,e.level,lc);
                ZUILabel(ctx,e.text,ctx->Theme.TextDim);
                ZUIEndRow(ctx);
            }

            ZUIEndScrollRegion(ctx);
        }
    };
    constexpr OutputPanel::LogEntry OutputPanel::k_log[11];

    // ================================================================
    // ContentBrowserPanel — asset grid with ZUIGridView
    // ================================================================
    struct ContentBrowserPanel : ZUIPanelView
    {
        ContentBrowserPanel() {
            Title = "Content";
            TabColor[0]=0.40f; TabColor[1]=0.78f; TabColor[2]=1.00f; TabColor[3]=0.70f;
        }

        int   selected    = -1;
        int   view_size   = 1; // 0=small 1=medium 2=large
        char  search_buf[64] = {};
        static constexpr float kCellSizes[3] = {70.f, 90.f, 110.f};

        struct Asset {
            const char* name;
            const char* ext;
            float       icon[4];
            bool        is_dir;
        };
        static constexpr Asset k_assets[] = {
            {"Meshes",        "",        {1.00f,0.90f,0.20f,1.f}, true },
            {"Textures",      "",        {1.00f,0.90f,0.20f,1.f}, true },
            {"Materials",     "",        {1.00f,0.90f,0.20f,1.f}, true },
            {"SM_Cube",       ".zemesh", {0.40f,0.78f,1.00f,1.f}, false},
            {"SM_Sphere",     ".zemesh", {0.40f,0.78f,1.00f,1.f}, false},
            {"SM_Cylinder",   ".zemesh", {0.40f,0.78f,1.00f,1.f}, false},
            {"T_Diffuse",     ".png",    {1.00f,0.55f,0.25f,1.f}, false},
            {"T_Normal",      ".png",    {0.50f,0.65f,1.00f,1.f}, false},
            {"T_Roughness",   ".png",    {0.70f,0.70f,0.70f,1.f}, false},
            {"M_Wood",        ".zemat",  {0.50f,0.85f,0.40f,1.f}, false},
            {"M_Metal",       ".zemat",  {0.50f,0.85f,0.40f,1.f}, false},
            {"DefaultScene",  ".zescene",{0.80f,0.50f,1.00f,1.f}, false},
            {"Env_Day",       ".hdr",    {0.30f,0.90f,0.90f,1.f}, false},
            {"SFX_Impact",    ".wav",    {0.98f,0.85f,0.20f,1.f}, false},
        };
        static constexpr int kAssetCount = 14;

        void BuildContent(ZUIContext* ctx, float[4]) override
        {
            // Toolbar
            {
                ZUIBox* tb=ZUIBeginRow(ctx,"##cbtb",ZFill(),ZPx(28.f));
                tb->Flags=tb->Flags|ZUI_DrawBackground;
                ZUIBoxSetColor(tb,0.13f,0.13f,0.14f,1.f);
                ZUISpacer(ctx,6.f);
                ZUISmallButton(ctx,"Import");
                ZUISpacer(ctx,4.f);
                ZUISmallButton(ctx,"New Folder");
                ZUISpacer(ctx,8.f);
                ZUITextField(ctx,"##cbsearch",search_buf,sizeof(search_buf),120.f);
                // Fill + view size buttons
                char fk[]="##cbfill"; ZUIBox* f=ZUIPushBox(ctx,fk,8,ZUI_None);
                f->Size[0]=ZFill(); f->Size[1]=ZPx(28.f); ZUIPopBox(ctx);
                static const char* kSz[]={"S","M","L"};
                for (int i=0; i<3; ++i)
                {
                    char bk[16]; snprintf(bk,sizeof(bk),"%s##vs%d",kSz[i],i);
                    bool sel=(view_size==i);
                    ZUIBox* btn=ZUIPushBox(ctx,bk,(uint32_t)strlen(bk),
                        ZUI_DrawBackground|ZUI_DrawText|ZUI_Clickable);
                    btn->Size[0]=ZPx(20.f); btn->Size[1]=ZPx(22.f);
                    btn->TextAlign=ZUITextAlign::Center;
                    if (sel) ZUIBoxSetColor(btn,0.f,0.47f,0.80f,0.7f);
                    else ZUIBoxSetColor(btn,0.20f,0.20f,0.22f,1.f);
                    btn->TextColor[0]=btn->TextColor[1]=btn->TextColor[2]=1.f;
                    btn->TextColor[3]=sel?1.f:0.65f;
                    ZUIBoxSetCornerRadius(btn,2.f);
                    ZUISignal s=ZUISignalFromBox(ctx,btn); ZUIPopBox(ctx);
                    if (s.Flags&ZUI_SignalClicked) view_size=i;
                    ZUISpacer(ctx,1.f);
                }
                ZUISpacer(ctx,6.f);
                ZUIEndRow(ctx);
            }
            // Breadcrumb
            {
                ZUIBox* br=ZUIBeginRow(ctx,"##cbbr",ZFill(),ZPx(22.f));
                br->Flags=br->Flags|ZUI_DrawBackground;
                ZUIBoxSetColor(br,0.10f,0.10f,0.11f,1.f);
                ZUISpacer(ctx,6.f);
                ZUILabel(ctx,"Assets",ctx->Theme.TextAccent);
                ZUILabel(ctx," / ",ctx->Theme.TextDim);
                ZUILabel(ctx,"All",ctx->Theme.TextDefault);
                ZUIEndRow(ctx);
            }

            float cell = kCellSizes[view_size];
            ZUIBeginGridView(ctx,"##cbgrid",cell,cell+20.f);

            for (int i=0; i<kAssetCount; ++i)
            {
                const Asset& a=k_assets[i];
                char ik[32]; snprintf(ik,sizeof(ik),"##cb%d",i);
                if (ZUIGridViewNextItem(ctx,ik,selected==i))
                    selected=i;

                // Icon
                ZUISpacer(ctx,6.f);
                char iconk[32]; snprintf(iconk,sizeof(iconk),"##cbi%d",i);
                ZUIBox* icon=ZUIPushBox(ctx,iconk,(uint32_t)strlen(iconk),ZUI_DrawBackground);
                icon->Size[0]=ZPx(cell-12.f); icon->Size[1]=ZPx(cell-12.f);
                ZUIBoxSetColorArr(icon,a.icon);
                ZUIBoxSetCornerRadius(icon,a.is_dir?4.f:6.f);
                icon->EdgeSoftness=0.5f;
                ZUIPopBox(ctx);
                ZUISpacer(ctx,3.f);
                // Name label (truncated to fit)
                ZUILabel(ctx,a.name,selected==i?ctx->Theme.TextDefault:ctx->Theme.TextDim);

                ZUIGridViewEndItem(ctx);
            }

            ZUIEndGridView(ctx);
        }
    };
    constexpr float ContentBrowserPanel::kCellSizes[3];

} // namespace Tetragrama::Panels
