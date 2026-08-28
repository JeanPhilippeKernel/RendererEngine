#include <GLFW/glfw3.h>
#include <Tetragrama/Components/ZUI/ZUIDockspaceComponent.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorScene.h>
#include <Tetragrama/Serializers/EditorSceneSerializer.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/UI/ZUIDockspace.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>

using namespace ZEngine::UI;

namespace Tetragrama::Components
{
    static constexpr float k_dim[4] = {0.55f, 0.55f, 0.60f, 1.f};
    static constexpr float kMenuH   = 28.f;
    static constexpr float kStatusH = 28.f;
    static constexpr float kLeftW   = 0.18f;
    static constexpr float kRightW  = 0.22f;
    static constexpr float kBottomH = 0.25f;

    static void            AssignRegion(ZUIComponent* cmp, float x, float y, float w, float h)
    {
        if (!cmp || cmp->Detached)
        {
            return;
        }
        cmp->RegionX = x;
        cmp->RegionY = y;
        cmp->RegionW = w;
        cmp->RegionH = h;
    }

    void ZUIDockspaceComponent::Initialize(Tetragrama::Layers::ZUILayer* parent, cstring name, bool visibility)
    {
        ParentLayer = parent;
        Name        = name;
        Visible     = visibility;

        // Build the dockspace split tree once at startup
        auto* arena = ParentLayer ? &ParentLayer->LocalArena : nullptr;
        if (arena)
        {
            m_dock_tree = ZUIDockTreeCreate(arena);

            // Root: H split → left 18% (Hierarchy) | right 82%
            ZUIDockSplitH(m_dock_tree, m_dock_tree->Root, kLeftW, ZUIDockHashName("Hierarchy"), 0);

            // Right child of root: H split → left 78% (center) | right 22% (Inspector)
            ZUIDockNode* right_node = m_dock_tree->Root->Last;
            ZUIDockSplitH(m_dock_tree, right_node, 1.f - kRightW, 0, ZUIDockHashName("Inspector"));

            // Center child: V split → top 75% (Viewport) | bottom 25%
            ZUIDockNode* center_node = right_node->First;
            ZUIDockSplitV(m_dock_tree, center_node, 1.f - kBottomH, ZUIDockHashName("Viewport"), 0);

            // Bottom child: H split → left 40% (Log) | right 60% (Project)
            ZUIDockNode* bottom_node = center_node->Last;
            ZUIDockSplitH(m_dock_tree, bottom_node, 0.40f, ZUIDockHashName("Log"), ZUIDockHashName("Project"));
        }
    }

    void ZUIDockspaceComponent::BuildUI(ZUIContext* ctx)
    {
        if (!Visible)
        {
            return;
        }

        float sw       = (float) ctx->ScreenW;
        float sh       = (float) ctx->ScreenH;
        float menu_h   = kMenuH * ctx->UIScale;
        float status_h = kStatusH * ctx->UIScale;

        // Recompute dock rects from the current window size
        if (m_dock_tree)
        {
            const float root_rect[4] = {0.f, menu_h, sw, sh - status_h};
            ZUIDockLayout(m_dock_tree, root_rect);

            auto AssignFromDock = [&](ZUIComponent* cmp, const char* panel_name) {
                if (!cmp || cmp->Detached)
                {
                    return;
                }
                float r[4];
                if (ZUIDockRectForKey(m_dock_tree, ZUIDockHashName(panel_name), r))
                    AssignRegion(cmp, r[0], r[1], r[2] - r[0], r[3] - r[1]);
            };

            AssignFromDock(Hierarchy, "Hierarchy");
            AssignFromDock(Inspector, "Inspector");
            AssignFromDock(Viewport, "Viewport");
            AssignFromDock(Log, "Log");
            AssignFromDock(Project, "Project");
        }
        AssignRegion(StatusBar, 0.f, sh - status_h, sw, status_h);

        // --- Full-screen background ---
        ZUIBox* bg      = ZUIBeginColumn(ctx, "##dockspace_bg", ZPx(sw), ZPx(sh));
        bg->Flags       = bg->Flags | ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY;
        bg->FloatPos[0] = 0.f;
        bg->FloatPos[1] = 0.f;
        ZUIBoxSetColorArr(bg, ctx->Theme.WindowBg);
        ZUIBoxSetCornerRadius(bg, 0.f);
        bg->EdgeSoftness = 0.f;

        // Platform-aware shortcut display strings
#if defined(__APPLE__)
        static constexpr const char* kMod          = "Cmd+";
        static constexpr const char* kModShift     = "Cmd+Shift+";
        static constexpr const char* kQuitShortcut = "Cmd+Q";
#else
        static constexpr const char* kMod          = "Ctrl+";
        static constexpr const char* kModShift     = "Ctrl+Shift+";
        static constexpr const char* kQuitShortcut = "Alt+F4";
#endif
        char sc_new[24], sc_open[24], sc_save[24], sc_save_as[24], sc_undo[24], sc_redo[24], sc_all[24];
        snprintf(sc_new, sizeof(sc_new), "%sN", kMod);
        snprintf(sc_open, sizeof(sc_open), "%sO", kMod);
        snprintf(sc_save, sizeof(sc_save), "%sS", kMod);
        snprintf(sc_save_as, sizeof(sc_save_as), "%sS", kModShift);
        snprintf(sc_undo, sizeof(sc_undo), "%sZ", kMod);
        snprintf(sc_redo, sizeof(sc_redo), "%sY", kMod);
        snprintf(sc_all, sizeof(sc_all), "%sA", kMod);

        // --- Menu bar ---
        if (ZUIBeginMenuBar(ctx))
        {
            ZUISpacer(ctx, 6.f);

            // "File" menu
            if (ZUIBeginMenu(ctx, "File"))
            {
                auto* file_app = (ParentLayer && ParentLayer->CurrentApp) ? reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp) : nullptr;

                if (ZUIMenuItemEx(ctx, "New Scene", sc_new))
                {
                    if (file_app && file_app->CurrentScene)
                    {
                        auto* scene                = reinterpret_cast<EditorScenePtr>(file_app->CurrentScene);
                        scene->SelectedActorHandle = {};
                    }
                }
                if (ZUIMenuItemEx(ctx, "Open Scene...", sc_open))
                {
                    ZENGINE_CORE_INFO("[Editor] Use the Project panel to locate and drop a .zescene into the viewport")
                }
                ZUISeparator(ctx);
                if (ZUIMenuItemEx(ctx, "Save Scene", sc_save))
                {
                    if (file_app && file_app->CurrentScene && file_app->Configuration)
                    {
                        auto*                              scene = reinterpret_cast<EditorScenePtr>(file_app->CurrentScene);
                        Serializers::EditorSceneSerializer serializer;
                        serializer.Serialize(scene);
                        ZENGINE_CORE_INFO("[Editor] Scene saved")
                    }
                }
                if (ZUIMenuItemEx(ctx, "Save Scene As...", sc_save_as))
                {
                    if (file_app && file_app->CurrentScene)
                    {
                        auto*                              scene = reinterpret_cast<EditorScenePtr>(file_app->CurrentScene);
                        Serializers::EditorSceneSerializer serializer;
                        serializer.Serialize(scene);
                    }
                }
                ZUISeparator(ctx);
                if (ZUIMenuItemEx(ctx, "Quit", kQuitShortcut))
                {
                    if (file_app && file_app->CurrentWindow)
                    {
                        auto* glfw_win = static_cast<GLFWwindow*>(file_app->CurrentWindow->GetNativeWindow());
                        if (glfw_win)
                            glfwSetWindowShouldClose(glfw_win, GLFW_TRUE);
                    }
                }
                ZUIEndMenu(ctx);
            }
            ZUISpacer(ctx, 4.f);

            // "Edit" menu
            if (ZUIBeginMenu(ctx, "Edit"))
            {
                if (ZUIMenuItemEx(ctx, "Undo", sc_undo, false, false))
                {
                }
                if (ZUIMenuItemEx(ctx, "Redo", sc_redo, false, false))
                {
                }
                ZUISeparator(ctx);
                if (ZUIMenuItemEx(ctx, "Select All", sc_all))
                {
                    if (ParentLayer && ParentLayer->CurrentApp)
                    {
                        auto* edit_app   = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
                        auto* edit_scene = reinterpret_cast<EditorScenePtr>(edit_app->CurrentScene);
                        auto* eng        = ZEngine::Engine::GetContext();
                        if (edit_scene && eng && eng->ActorManager && eng->ActorManager->Count() > 0)
                        {
                            bool found = false;
                            eng->ActorManager->ForEach([&](ZEngine::ECS::ActorHandle h, ZEngine::ECS::Actor*) {
                                if (!found)
                                {
                                    edit_scene->SelectedActorHandle = h;
                                    found                           = true;
                                }
                            });
                        }
                    }
                }
                ZUIEndMenu(ctx);
            }
            ZUISpacer(ctx, 4.f);

            // "View" menu — checkable toggles using ZUIMenuItemEx selected state
            if (ZUIBeginMenu(ctx, "View"))
            {
                auto vis_item = [&](const char* label, ZUIComponent* cmp) {
                    if (!cmp)
                        return;
                    if (ZUIMenuItemEx(ctx, label, nullptr, cmp->Visible))
                        cmp->Visible = !cmp->Visible;
                };
                vis_item("Scene", Viewport);
                vis_item("Hierarchy", Hierarchy);
                vis_item("Inspector", Inspector);
                vis_item("Console", Log);
                vis_item("Project", Project);
                ZUIEndMenu(ctx);
            }

            ZUILabel(ctx, " | ", k_dim);

            // Scene name
            if (ParentLayer && ParentLayer->CurrentApp)
            {
                auto* app = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
                if (app->Configuration)
                {
                    const char* sname = app->Configuration->ActiveSceneName.empty() ? "-" : app->Configuration->ActiveSceneName.c_str();
                    char        scene_buf[128];
                    snprintf(scene_buf, sizeof(scene_buf), "Scene: %s", sname);
                    ZUILabel(ctx, scene_buf, k_dim);
                }
            }

            ZUIEndMenuBar(ctx);
        }

        // --- Workspace resize dividers (RAD Debugger style) ---
        // Direct mouse-bound tracking — bypasses z-ordered hit-test so dividers
        // always have priority over panel content, regardless of render order.
        if (m_dock_tree)
        {
            static constexpr float kDivW = 6.f; // grab width in logical px

            for (int di = 0; di < 4; ++di)
            {
                Divider& div   = m_dividers[di];
                float    lr[4] = {};
                if (!ZUIDockRectForKey(m_dock_tree, ZUIDockHashName(div.leaf_name), lr))
                    continue;

                // Divider rect: at the right/bottom (or left/top if use_near) edge of the leaf
                float dx0, dy0, dx1, dy1;
                if (!div.horizontal)
                { // vertical divider (left|right split)
                    float edge_x = div.use_near ? lr[0] : lr[2];
                    dx0          = edge_x - kDivW * 0.5f;
                    dy0          = lr[1];
                    dx1          = edge_x + kDivW * 0.5f;
                    dy1          = lr[3];
                }
                else
                { // horizontal divider (top|bottom split)
                    float edge_y = div.use_near ? lr[1] : lr[3];
                    dx0          = lr[0];
                    dy0          = edge_y - kDivW * 0.5f;
                    dx1          = lr[2];
                    dy1          = edge_y + kDivW * 0.5f;
                }

                float mx = ctx->MousePos[0], my = ctx->MousePos[1];
                bool  in_rect = (mx >= dx0 && mx <= dx1 && my >= dy0 && my <= dy1);

                // Start drag when mouse pressed in divider area
                if (ctx->MousePressed[0] && in_rect)
                    div.dragging = true;
                if (ctx->MouseReleased[0])
                    div.dragging = false;

                // Apply resize while dragging
                if (div.dragging && ctx->MouseDown[0])
                {
                    float delta = div.horizontal ? (ctx->MousePos[1] - ctx->PrevMousePos[1]) : (ctx->MousePos[0] - ctx->PrevMousePos[0]);
                    if (delta != 0.f)
                    {
                        uint64_t     key  = ZUIDockHashName(div.leaf_name);
                        ZUIDockNode* leaf = ZUIDockFindLeaf(m_dock_tree, key);
                        if (leaf)
                            ZUIDockResize(m_dock_tree, leaf, delta);
                    }
                }

                // Visual indicator: thin colored line at the divider, brighter on hover/drag
                bool  highlight  = in_rect || div.dragging;
                float vis_col[4] = {highlight ? 0.45f : 0.22f, highlight ? 0.55f : 0.28f, highlight ? 0.70f : 0.35f, 1.f};
                char  vis_key[32];
                snprintf(vis_key, sizeof(vis_key), "##divvis_%d", di);
                ZUIBox* vis = ZUIPushBox(ctx, vis_key, (uint32_t) strlen(vis_key), ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
                if (!div.horizontal)
                {
                    vis->Size[0]     = ZPx(1.f);
                    vis->Size[1]     = ZPx(dy1 - dy0);
                    vis->FloatPos[0] = (dx0 + dx1) * 0.5f;
                    vis->FloatPos[1] = dy0;
                }
                else
                {
                    vis->Size[0]     = ZPx(dx1 - dx0);
                    vis->Size[1]     = ZPx(1.f);
                    vis->FloatPos[0] = dx0;
                    vis->FloatPos[1] = (dy0 + dy1) * 0.5f;
                }
                vis->EdgeSoftness = 0.f;
                ZUIBoxSetColorArr(vis, vis_col);
                ZUIPopBox(ctx);
            }
        }

        ZUIEndColumn(ctx);
    }
} // namespace Tetragrama::Components
