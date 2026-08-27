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
    // HierarchyPanel — scene actor tree
    // ================================================================
    struct HierarchyPanel : ZUIPanelView
    {
        HierarchyPanel() { Title = "Hierarchy"; }

        int  selected      = -1;
        bool states_seeded = false;
        char search_buf[64] = {};

        struct Actor { const char* name; int parent; float icon[4]; };
        static constexpr Actor k_actors[] = {
            { "World",           -1, {0.50f,0.50f,0.90f,1.f} },
            { "DefaultScene",     0, {0.30f,0.78f,0.30f,1.f} },
            { "DirectionalLight", 1, {0.98f,0.85f,0.25f,1.f} },
            { "MainCamera",       1, {0.25f,0.78f,0.98f,1.f} },
            { "Cube",             1, {0.72f,0.50f,0.98f,1.f} },
            { "Sphere",           1, {0.72f,0.50f,0.98f,1.f} },
            { "Ground",           1, {0.72f,0.50f,0.98f,1.f} },
            { "SkyAtmosphere",    1, {0.30f,0.90f,0.90f,1.f} },
        };
        static constexpr int kCount = 8;

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            (void)rect;
            float h = ZUIGetFrameHeight(ctx);

            { ZUIBox* hdr = ZUIBeginRow(ctx, "##hhdr", ZFill(), ZPx(h));
              hdr->Flags = hdr->Flags | ZUI_DrawBackground;
              ZUIBoxSetColorArr(hdr, ctx->Theme.PanelBgAlt); hdr->EdgeSoftness=0.f;
              ZUISpacer(ctx, ZUIGetFramePadX(ctx));
              ZUILabel(ctx, "SCENE HIERARCHY", ctx->Theme.TextDim, ZUIFontSize::Small);
              { char fk[]="##hfill"; ZUIBox* f=ZUIPushBox(ctx,fk,7,ZUI_None);
                f->Size[0]=ZFill();f->Size[1]=ZPx(h);ZUIPopBox(ctx); }
              ZUISmallButton(ctx, "+");
              ZUISpacer(ctx, ZUIGetFramePadX(ctx));
              ZUIEndRow(ctx); }

            ZUISearchBox(ctx, "##hsearch", search_buf, sizeof(search_buf),
                         "Search actors...", ZFill());
            ZUISeparator(ctx);

            if (!states_seeded)
            {
                auto seed = [&](const char* lbl, int depth) {
                    uint64_t hh = ZUIHashStr(lbl,(uint32_t)strlen(lbl))^(uint64_t)depth;
                    ZUIPersistentState* s = ZUIStateGetOrInsert(&ctx->StateStore, hh);
                    if (s) s->UserData = 1.f;
                };
                seed("World",0); seed("DefaultScene",1);
                states_seeded = true;
            }

            ZUIBeginTreeView(ctx, "##hier");
            for (int i = 0; i < kCount; ++i)
            {
                if (k_actors[i].parent != -1) continue;
                bool is_sel = (selected == i);
                if (ZUITreeViewBeginNode(ctx, k_actors[i].name, is_sel, k_actors[i].icon, true))
                {
                    if (is_sel) selected = i;
                    for (int j = 0; j < kCount; ++j)
                    {
                        if (k_actors[j].parent != i) continue;
                        bool csel = (selected == j);
                        bool j_has_ch = false;
                        for (int k = 0; k < kCount; ++k)
                            if (k_actors[k].parent == j) { j_has_ch=true; break; }
                        if (j_has_ch)
                        {
                            if (ZUITreeViewBeginNode(ctx,k_actors[j].name,csel,k_actors[j].icon,true))
                            {
                                if (csel) selected = j;
                                for (int k = 0; k < kCount; ++k)
                                {
                                    if (k_actors[k].parent != j) continue;
                                    bool ksel = (selected == k);
                                    if (ZUITreeViewLeaf(ctx,k_actors[k].name,ksel,k_actors[k].icon))
                                        selected = k;
                                }
                                ZUITreeViewEndNode(ctx);
                            }
                            else if (csel) selected = j;
                        }
                        else
                        {
                            if (ZUITreeViewLeaf(ctx,k_actors[j].name,csel,k_actors[j].icon))
                                selected = j;
                        }
                    }
                    ZUITreeViewEndNode(ctx);
                }
                else if (is_sel) selected = i;
            }
            ZUIEndTreeView(ctx);
        }
    };

    // ================================================================
    // ViewportPanel — 3D scene view with overlay toolbar
    // ================================================================
    struct ViewportPanel : ZUIPanelView
    {
        ViewportPanel() { Title = "Viewport"; }

        int  view_mode    = 0;
        int  shading_mode = 0;
        bool snap_enabled = false;

        static constexpr float k_fps[32] = {
            90,88,91,87,92,89,85,93,90,88,94,87,89,91,88,92,
            90,86,93,89,91,88,90,92,87,93,88,91,89,90,92,88
        };

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            float h = ZUIGetFrameHeight(ctx);

            ZUIBox* bg = ZUIBeginColumn(ctx, "##vpbg", ZFill(), ZFill());
            bg->Flags = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColor(bg, 0.09f, 0.09f, 0.095f, 1.f); bg->EdgeSoftness=0.f;
            ZUISpacer(ctx, 24.f);
            ZUILabel(ctx, "[ 3D Viewport ]", ctx->Theme.TextDim);
            ZUISpacer(ctx, 4.f);
            ZUILabel(ctx, "Scene renderer attaches here", ctx->Theme.TextDim);
            ZUISpacer(ctx, 4.f);
            ZUILabel(ctx, "Camera:  X 0.0   Y 5.0   Z -8.0", ctx->Theme.TextDim);
            ZUISpacer(ctx, 8.f);
            { ZUIBox* r = ZUIBeginRow(ctx, "##fpsrow", ZFill(), ZFit());
              ZUISpacer(ctx, 8.f);
              ZUILabel(ctx, "FPS: 90", ctx->Theme.TextDim);
              ZUISpacer(ctx, 8.f);
              ZUIPlotLines(ctx, "##fps", k_fps, 32, 60.f, 120.f, nullptr, ZPx(80.f), ZPx(20.f));
              ZUIEndRow(ctx); }
            ZUIEndColumn(ctx);

            // Floating overlay toolbar
            float tw = rect[2] - rect[0];
            ZUIBox* tb = ZUIPushBox(ctx, "##vptb", 6,
                ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
            tb->Size[0]=ZPx(tw); tb->Size[1]=ZPx(h+6.f);
            tb->FloatPos[0]=rect[0]; tb->FloatPos[1]=rect[1];
            tb->LayoutAxis=ZUIAxis::X;
            ZUIBoxSetColor(tb, 0.10f,0.10f,0.106f,0.92f); tb->EdgeSoftness=0.f;

            auto seg_btn = [&](const char* lbl, bool sel, int* var, int idx) {
                ZUIBox* btn = ZUIPushBox(ctx, lbl, (uint32_t)strlen(lbl),
                    ZUI_DrawBackground|ZUI_DrawText|ZUI_Clickable);
                btn->Size[0]=ZText(); btn->Size[1]=ZPx(h);
                btn->Padding[0]=btn->Padding[2]=ZUIGetFramePadX(ctx);
                ZUIBoxSetCornerRadius(btn, ctx->Style.FrameRounding); btn->EdgeSoftness=0.f;
                if (sel) ZUIBoxSetColorArr(btn, ctx->Theme.ButtonActiveBg);
                else     ZUIBoxSetColorArr(btn, ctx->Theme.InputBg);
                btn->TextColor[0]=sel?ctx->Theme.TextDefault[0]:ctx->Theme.TextDim[0];
                btn->TextColor[1]=sel?ctx->Theme.TextDefault[1]:ctx->Theme.TextDim[1];
                btn->TextColor[2]=sel?ctx->Theme.TextDefault[2]:ctx->Theme.TextDim[2];
                btn->TextColor[3]=1.f;
                ZUISignal s = ZUISignalFromBox(ctx, btn); ZUIPopBox(ctx);
                if (s.Flags & ZUI_SignalClicked) *var = idx;
            };

            ZUISpacer(ctx, 6.f);
            static const char* kVm[]={"Persp","Top","Front","Right"};
            for (int i=0;i<4;++i){seg_btn(kVm[i],view_mode==i,&view_mode,i);if(i<3)ZUISpacer(ctx,1.f);}
            ZUISpacer(ctx, 8.f);
            { ZUIBox* sp=ZUIPushBox(ctx,"##vpsep",7,ZUI_DrawBackground);
              sp->Size[0]=ZPx(1.f);sp->Size[1]=ZPx(14.f);
              ZUIBoxSetColorArr(sp,ctx->Theme.Separator);ZUIPopBox(ctx); }
            ZUISpacer(ctx, 8.f);
            static const char* kSh[]={"Lit","Unlit","Wire"};
            for (int i=0;i<3;++i){seg_btn(kSh[i],shading_mode==i,&shading_mode,i);if(i<2)ZUISpacer(ctx,1.f);}
            ZUISpacer(ctx, 8.f);
            ZUIToggleButton(ctx, "Snap##vsnap", &snap_enabled, ZText(), ZPx(h));
            ZUIPopBox(ctx);
        }
    };
    constexpr float ViewportPanel::k_fps[32];

    // ================================================================
    // InspectorPanel — component property editor
    // ================================================================
    struct InspectorPanel : ZUIPanelView
    {
        InspectorPanel() { Title = "Inspector"; }

        bool  xfm_open  = true;
        bool  mesh_open = true;
        bool  mat_open  = true;
        bool  phys_open = false;
        float pos[3] = {0.f,0.f,0.f}, rot[3] = {0.f,0.f,0.f}, scl[3] = {1.f,1.f,1.f};
        float roughness=0.45f, metallic=0.f;
        float mat_col[4] = {0.8f,0.4f,0.2f,1.f};
        bool  cast_shadow=true, simulate_phys=false;
        int   mobility=2;
        char  search_buf[64]={};
        static constexpr float kLW = 110.f;

        void XYZRow(ZUIContext* ctx, const char* label, const char* key, float v[3])
        {
            static const float kA[3][3]={{0.85f,0.22f,0.22f},{0.22f,0.72f,0.22f},{0.22f,0.45f,0.85f}};
            float h=ZUIGetFrameHeight(ctx);
            char rk[64]; snprintf(rk,sizeof(rk),"##xr_%s",key);
            ZUIBeginRow(ctx,rk,ZFill(),ZPx(h));
            char lk[64]; snprintf(lk,sizeof(lk),"%s##xl_%s",label,key);
            ZUIBox* lb=ZUIPushBox(ctx,lk,(uint32_t)strlen(lk),ZUI_DrawText);
            lb->Size[0]=ZPx(kLW);lb->Size[1]=ZFill();lb->Padding[0]=ZUIGetFramePadX(ctx);
            lb->TextColor[0]=ctx->Theme.TextDim[0];lb->TextColor[1]=ctx->Theme.TextDim[1];
            lb->TextColor[2]=ctx->Theme.TextDim[2];lb->TextColor[3]=1.f;ZUIPopBox(ctx);
            for (int i=0;i<3;++i)
            {
                if(i>0)ZUISpacer(ctx,2.f);
                char ak[64]; snprintf(ak,sizeof(ak),"##xacc%d_%s",i,key);
                ZUIBox* acc=ZUIPushBox(ctx,ak,(uint32_t)strlen(ak),ZUI_DrawBackground);
                acc->Size[0]=ZPx(2.f);acc->Size[1]=ZPx(h-4.f);
                ZUIBoxSetColor(acc,kA[i][0],kA[i][1],kA[i][2],1.f);
                acc->EdgeSoftness=0.f;ZUIPopBox(ctx);
                char fk[64]; snprintf(fk,sizeof(fk),"##xf%d_%s",i,key);
                ZUIDragFloat(ctx,fk,&v[i],0.1f,58.f);
            }
            ZUIEndRow(ctx);
        }

        void PropStart(ZUIContext* ctx, const char* label, const char* rkey)
        {
            float h=ZUIGetFrameHeight(ctx);
            char rk[80]; snprintf(rk,sizeof(rk),"##pr_%s",rkey);
            ZUIBeginRow(ctx,rk,ZFill(),ZPx(h));
            char lk[80]; snprintf(lk,sizeof(lk),"%s##lbl_%s",label,rkey);
            ZUIBox* lb=ZUIPushBox(ctx,lk,(uint32_t)strlen(lk),ZUI_DrawText);
            lb->Size[0]=ZPx(kLW);lb->Size[1]=ZFill();lb->Padding[0]=ZUIGetFramePadX(ctx);
            lb->TextColor[0]=ctx->Theme.TextDim[0];lb->TextColor[1]=ctx->Theme.TextDim[1];
            lb->TextColor[2]=ctx->Theme.TextDim[2];lb->TextColor[3]=1.f;ZUIPopBox(ctx);
        }
        void PropEnd(ZUIContext* ctx){ZUIEndRow(ctx);}

        void MobilityRow(ZUIContext* ctx)
        {
            float h=ZUIGetFrameHeight(ctx);
            PropStart(ctx,"Mobility","mob");
            static const char* kL[]={"Static","Stationary","Movable"};
            static const float kW[]={48.f,66.f,55.f};
            for (int i=0;i<3;++i)
            {
                if(i>0)ZUISpacer(ctx,1.f);
                char bk[32];snprintf(bk,sizeof(bk),"%s##mob%d",kL[i],i);
                bool sel=(mobility==i);
                ZUIBox* btn=ZUIPushBox(ctx,bk,(uint32_t)strlen(bk),
                    ZUI_DrawBackground|ZUI_DrawBorder|ZUI_DrawText|ZUI_Clickable);
                btn->Size[0]=ZPx(kW[i]);btn->Size[1]=ZPx(h-2.f);
                btn->TextAlign=ZUITextAlign::Center;btn->BorderThickness=1.f;
                ZUIBoxSetCornerRadius(btn,ctx->Style.FrameRounding);btn->EdgeSoftness=0.f;
                if(sel){ZUIBoxSetColorArr(btn,ctx->Theme.ButtonActiveBg);
                    btn->BorderColor[0]=ctx->Theme.TabActiveBorder[0];
                    btn->BorderColor[1]=ctx->Theme.TabActiveBorder[1];
                    btn->BorderColor[2]=ctx->Theme.TabActiveBorder[2];btn->BorderColor[3]=1.f;}
                else{ZUIBoxSetColorArr(btn,ctx->Theme.InputBg);
                    btn->BorderColor[0]=ctx->Theme.InputBorder[0];
                    btn->BorderColor[1]=ctx->Theme.InputBorder[1];
                    btn->BorderColor[2]=ctx->Theme.InputBorder[2];btn->BorderColor[3]=0.8f;}
                btn->TextColor[0]=sel?ctx->Theme.TextDefault[0]:ctx->Theme.TextDim[0];
                btn->TextColor[1]=sel?ctx->Theme.TextDefault[1]:ctx->Theme.TextDim[1];
                btn->TextColor[2]=sel?ctx->Theme.TextDefault[2]:ctx->Theme.TextDim[2];
                btn->TextColor[3]=1.f;
                ZUISignal s=ZUISignalFromBox(ctx,btn);ZUIPopBox(ctx);
                if(s.Flags&ZUI_SignalClicked)mobility=i;
            }
            PropEnd(ctx);
        }

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            (void)rect;
            float h=ZUIGetFrameHeight(ctx);

            { ZUIBox* hdr=ZUIBeginRow(ctx,"##ah",ZFill(),ZPx(h*2.f));
              hdr->Flags=hdr->Flags|ZUI_DrawBackground;
              ZUIBoxSetColorArr(hdr,ctx->Theme.PanelBgAlt);hdr->EdgeSoftness=0.f;
              ZUISpacer(ctx,ZUIGetFramePadX(ctx)*2.f);
              ZUIBox* ic=ZUIPushBox(ctx,"##ahic",6,ZUI_DrawBackground);
              float isz=ctx->Style.TabIconSize*2.f;
              ic->Size[0]=ZPx(isz);ic->Size[1]=ZPx(isz);
              ZUIBoxSetColorArr(ic,ctx->Theme.TabActiveBorder);
              ZUIBoxSetCornerRadius(ic,isz*0.5f);ic->EdgeSoftness=0.5f;ZUIPopBox(ctx);
              ZUISpacer(ctx,ZUIGetInnerSpac(ctx));
              ZUIBeginColumn(ctx,"##ahtxt",ZFit(),ZFit());
                ZUILabel(ctx,"Cube",ctx->Theme.TextDefault,ZUIFontSize::Header);
                ZUILabel(ctx,"StaticMeshActor",ctx->Theme.TextDim);
              ZUIEndColumn(ctx);
              {char fk[]="##ahfill";ZUIBox* f=ZUIPushBox(ctx,fk,8,ZUI_None);
               f->Size[0]=ZFill();f->Size[1]=ZPx(h*2.f);ZUIPopBox(ctx);}
              ZUISmallButton(ctx,"+ Add");
              ZUISpacer(ctx,ZUIGetFramePadX(ctx));
              ZUIEndRow(ctx); }

            ZUISearchBox(ctx,"##inspsearch",search_buf,sizeof(search_buf),"Search properties...",ZFill());
            ZUISeparator(ctx);
            ZUIBeginScrollRegion(ctx,"##insp",ZFill(),ZFill());

            if(ZUICollapsingHeader(ctx,"Transform",&xfm_open))
            {
                XYZRow(ctx,"Location","loc",pos);
                XYZRow(ctx,"Rotation","rot",rot);
                XYZRow(ctx,"Scale","scl",scl);
                MobilityRow(ctx);
            }
            ZUISeparator(ctx);

            if(ZUICollapsingHeader(ctx,"Static Mesh",&mesh_open))
            {
                PropStart(ctx,"Mesh","mesh_v");
                if(ZUIBeginCombo(ctx,"##mesh_combo","SM_Cube.zemesh",ZFill()))
                {
                    ZUIComboItem(ctx,"SM_Cube.zemesh",true);
                    ZUIComboItem(ctx,"SM_Sphere.zemesh",false);
                    ZUIComboItem(ctx,"SM_Plane.zemesh",false);
                    ZUIEndCombo(ctx);
                }
                PropEnd(ctx);
                PropStart(ctx,"Cast Shadow","cshadow");
                ZUICheckbox(ctx,"##cshadow_cb",&cast_shadow);
                PropEnd(ctx);
                PropStart(ctx,"LOD","lod_v");
                ZUILabel(ctx,"Auto",ctx->Theme.TextAccent);
                PropEnd(ctx);
            }
            ZUISeparator(ctx);

            if(ZUICollapsingHeader(ctx,"Material",&mat_open))
            {
                PropStart(ctx,"Base Color","basecol");
                ZUIColorEdit4(ctx,"##basecol",mat_col);
                PropEnd(ctx);
                PropStart(ctx,"Roughness","roughness");
                ZUISliderFloat(ctx,"##roughness",&roughness,0.f,1.f,ZFill(),ZPx(h-2.f));
                PropEnd(ctx);
                PropStart(ctx,"Metallic","metallic");
                ZUISliderFloat(ctx,"##metallic",&metallic,0.f,1.f,ZFill(),ZPx(h-2.f));
                PropEnd(ctx);
            }
            ZUISeparator(ctx);

            if(ZUICollapsingHeader(ctx,"Physics",&phys_open))
            {
                PropStart(ctx,"Simulate","phys_sim");
                ZUICheckbox(ctx,"##phys_sim_cb",&simulate_phys);
                PropEnd(ctx);
            }
            ZUISpacer(ctx,8.f);
            {ZUIBeginRow(ctx,"##addcmp",ZFill(),ZPx(h+4.f));
             ZUISpacer(ctx,ZUIGetFramePadX(ctx));
             ZUIButton(ctx,"+ Add Component",ZFill(),ZPx(h));
             ZUISpacer(ctx,ZUIGetFramePadX(ctx));
             ZUIEndRow(ctx);}

            ZUIEndScrollRegion(ctx);
        }
    };

    // ================================================================
    // ConsolePanel — log output with filtering
    // ================================================================
    struct ConsolePanel : ZUIPanelView
    {
        ConsolePanel() { Title = "Console"; }

        int  filter      = 0;
        bool auto_scroll = true;

        struct LogEntry { const char* time; const char* level; const char* msg; int kind; };
        // 30 entries so the scrollbar is clearly visible
        static constexpr LogEntry k_log[] = {
            {"15:20:01","INFO ","Engine starting up — build Debug arm64",                       0},
            {"15:20:01","INFO ","ArenaAllocator: 512 MB reserved",                              0},
            {"15:20:01","INFO ","VFS mounted: /ZodiacEngine",                                   0},
            {"15:20:01","INFO ","VFS mounted: /Project",                                        0},
            {"15:20:02","INFO ","VulkanDevice: Apple M2 Pro — MoltenVK 1.3.275",               0},
            {"15:20:02","INFO ","Swapchain: 3024x1834 (Retina, VK_FORMAT_B8G8R8A8_UNORM)",     0},
            {"15:20:02","INFO ","RenderGraph compiled: 4 passes",                               0},
            {"15:20:02","INFO ","ShaderCache: 12 shaders loaded from cache",                    0},
            {"15:20:03","WARN ","ShaderCache miss: recompiling ui.vert",                        1},
            {"15:20:03","WARN ","ShaderCache miss: recompiling ui.frag",                        1},
            {"15:20:03","INFO ","ZUIFontAtlas baked: 3 fonts (13/11/17 px)",                   0},
            {"15:20:03","INFO ","ZUI style: FrameHeight=19  IndentSpacing=21",                 0},
            {"15:20:03","INFO ","AssetRegistry: scanning /Project/Assets...",                   0},
            {"15:20:03","INFO ","AssetRegistry: 142 assets indexed",                            0},
            {"15:20:03","INFO ","SceneLoader: loading DefaultScene.zescene",                    0},
            {"15:20:04","INFO ","Scene: spawned 8 actors",                                      0},
            {"15:20:04","INFO ","Scene: ECS initialized (8 entities)",                          0},
            {"15:20:04","WARN ","Actor 'Cube': no physics body — simulate disabled",            1},
            {"15:20:04","INFO ","RenderResourceManager: 3 meshes uploaded",                     0},
            {"15:20:04","INFO ","RenderResourceManager: 5 textures uploaded",                   0},
            {"15:20:04","INFO ","SkyAtmosphere: atmospheric scattering enabled",                0},
            {"15:20:05","WARN ","No skybox texture assigned — using atmosphere fallback",       1},
            {"15:20:05","INFO ","InputFrame registered: keyboard + mouse",                      0},
            {"15:20:05","INFO ","ZUIContextInit: FrameArena=32MB PersistentArena=1MB",         0},
            {"15:20:05","INFO ","UIContext budget: 60/128 MB committed",                        0},
            {"15:20:05","INFO ","Panel system ready: 4 panels docked",                          0},
            {"15:20:05","INFO ","ZUIDockSerial v4: no saved layout — using default",            0},
            {"15:20:06","INFO ","ZUILayer: BuildUI running at 60 fps",                          0},
            {"15:20:06","INFO ","Ready.",                                                        0},
            {"15:20:06","INFO ","----------------------------------------------",               0},
        };
        static constexpr int kLogCount = 30;

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            (void)rect;
            float h = ZUIGetFrameHeight(ctx);

            // Filter bar
            { ZUIBox* fb=ZUIBeginRow(ctx,"##confb",ZFill(),ZPx(h+4.f));
              fb->Flags=fb->Flags|ZUI_DrawBackground;
              ZUIBoxSetColorArr(fb,ctx->Theme.PanelBgAlt);fb->EdgeSoftness=0.f;
              ZUISpacer(ctx,ZUIGetFramePadX(ctx));

              static const char* kF[]={"All","Info","Warn","Error"};
              static const float kFC[4][4]={
                  {0.70f,0.70f,0.72f,1.f},{0.53f,0.80f,0.53f,1.f},
                  {0.95f,0.74f,0.14f,1.f},{0.94f,0.33f,0.31f,1.f}};
              for (int i=0;i<4;++i)
              {
                  char bk[24];snprintf(bk,sizeof(bk),"%s##flt%d",kF[i],i);
                  bool sel=(filter==i);
                  ZUIBox* btn=ZUIPushBox(ctx,bk,(uint32_t)strlen(bk),
                      ZUI_DrawBackground|ZUI_DrawBorder|ZUI_DrawText|ZUI_Clickable);
                  btn->Size[0]=ZText();btn->Size[1]=ZPx(h);
                  btn->Padding[0]=btn->Padding[2]=ZUIGetFramePadX(ctx)*2.f;
                  btn->BorderThickness=1.f;
                  ZUIBoxSetCornerRadius(btn,ctx->Style.FrameRounding);btn->EdgeSoftness=0.f;
                  if(sel){float bg[4]={kFC[i][0]*0.22f,kFC[i][1]*0.22f,kFC[i][2]*0.22f,1.f};
                      ZUIBoxSetColorArr(btn,bg);
                      btn->BorderColor[0]=kFC[i][0];btn->BorderColor[1]=kFC[i][1];
                      btn->BorderColor[2]=kFC[i][2];btn->BorderColor[3]=0.9f;}
                  else{ZUIBoxSetColorArr(btn,ctx->Theme.InputBg);
                      btn->BorderColor[0]=ctx->Theme.InputBorder[0];
                      btn->BorderColor[1]=ctx->Theme.InputBorder[1];
                      btn->BorderColor[2]=ctx->Theme.InputBorder[2];btn->BorderColor[3]=0.6f;}
                  btn->TextColor[0]=kFC[i][0];btn->TextColor[1]=kFC[i][1];
                  btn->TextColor[2]=kFC[i][2];btn->TextColor[3]=sel?1.f:0.75f;
                  ZUISignal s=ZUISignalFromBox(ctx,btn);ZUIPopBox(ctx);
                  if(s.Flags&ZUI_SignalClicked)filter=i;
                  ZUISpacer(ctx,4.f);
              }
              {char fk[]="##confill";ZUIBox* f=ZUIPushBox(ctx,fk,8,ZUI_None);
               f->Size[0]=ZFill();f->Size[1]=ZPx(h+4.f);ZUIPopBox(ctx);}
              ZUICheckbox(ctx,"Scroll##autoscroll",&auto_scroll);
              ZUISpacer(ctx,4.f);
              ZUISmallButton(ctx,"Clear");
              ZUISpacer(ctx,ZUIGetFramePadX(ctx));
              ZUIEndRow(ctx); }

            ZUISeparator(ctx);

            ZUIBeginScrollRegion(ctx,"##conlog",ZFill(),ZFill());
            for (int i=0;i<kLogCount;++i)
            {
                const LogEntry& e=k_log[i];
                if(filter==1&&e.kind!=0)continue;
                if(filter==2&&e.kind!=1)continue;
                if(filter==3&&e.kind!=2)continue;

                const float* lc=(e.kind==1)?ctx->Theme.TextWarn
                               :(e.kind==2)?ctx->Theme.TextError
                               :ctx->Theme.TextDim;

                char rk[32];snprintf(rk,sizeof(rk),"##cl%d",i);
                ZUIBox* row=ZUIBeginRow(ctx,rk,ZFill(),ZPx(h));
                row->Flags=row->Flags|ZUI_DrawBackground;
                bool hov=(ctx->HotKey==row->Key);
                ZUIBoxSetColor(row,0.f,0.f,0.f,hov?0.08f:0.f);

                ZUISpacer(ctx,ZUIGetFramePadX(ctx)*2.f);
                float tc[4]={0.35f,0.35f,0.37f,1.f};
                ZUILabel(ctx,e.time,tc);
                ZUISpacer(ctx,ZUIGetInnerSpac(ctx));
                ZUILabel(ctx,e.level,lc);
                ZUISpacer(ctx,2.f);
                ZUILabel(ctx,e.msg,ctx->Theme.TextDefault);
                ZUIEndRow(ctx);
            }
            ZUIEndScrollRegion(ctx);
        }
    };
    constexpr ConsolePanel::LogEntry ConsolePanel::k_log[30];

} // namespace Tetragrama::Panels
