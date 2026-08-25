#include <Tetragrama/Components/ZUI/ZUIDockspaceComponent.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorScene.h>
#include <Tetragrama/Serializers/EditorSceneSerializer.h>
#include <ZEngine/Engine.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/UI/ZUIDockspace.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <GLFW/glfw3.h>
#include <cstdio>

using namespace ZEngine::UI;

namespace Tetragrama::Components
{
    static constexpr float k_dim[4]   = {0.55f, 0.55f, 0.60f, 1.f};
    static constexpr float kMenuH     = 26.f;
    static constexpr float kStatusH   = 28.f;
    static constexpr float kLeftW     = 0.18f;
    static constexpr float kRightW    = 0.22f;
    static constexpr float kBottomH   = 0.25f;

    static void AssignRegion(ZUIComponent* cmp, float x, float y, float w, float h)
    {
        if (!cmp || cmp->Detached) { return; }
        cmp->RegionX = x;
        cmp->RegionY = y;
        cmp->RegionW = w;
        cmp->RegionH = h;
    }

    void ZUIDockspaceComponent::Initialize(Tetragrama::Layers::ZUILayer* parent,
                                           cstring name, bool visibility)
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
            ZUIDockSplitH(m_dock_tree, m_dock_tree->Root,
                          kLeftW,
                          ZUIDockHashName("Hierarchy"),
                          0);

            // Right child of root: H split → left 78% (center) | right 22% (Inspector)
            ZUIDockNode* right_node = m_dock_tree->Root->Last;
            ZUIDockSplitH(m_dock_tree, right_node,
                          1.f - kRightW,
                          0,
                          ZUIDockHashName("Inspector"));

            // Center child: V split → top 75% (Viewport) | bottom 25%
            ZUIDockNode* center_node = right_node->First;
            ZUIDockSplitV(m_dock_tree, center_node,
                          1.f - kBottomH,
                          ZUIDockHashName("Viewport"),
                          0);

            // Bottom child: H split → left 40% (Log) | right 60% (Project)
            ZUIDockNode* bottom_node = center_node->Last;
            ZUIDockSplitH(m_dock_tree, bottom_node,
                          0.40f,
                          ZUIDockHashName("Log"),
                          ZUIDockHashName("Project"));
        }
    }

    void ZUIDockspaceComponent::BuildUI(ZUIContext* ctx)
    {
        if (!Visible) { return; }

        float sw       = (float)ctx->ScreenW;
        float sh       = (float)ctx->ScreenH;
        float menu_h   = kMenuH   * ctx->UIScale;
        float status_h = kStatusH * ctx->UIScale;

        // Recompute dock rects from the current window size
        if (m_dock_tree)
        {
            const float root_rect[4] = {0.f, menu_h, sw, sh - status_h};
            ZUIDockLayout(m_dock_tree, root_rect);

            auto AssignFromDock = [&](ZUIComponent* cmp, const char* panel_name)
            {
                if (!cmp || cmp->Detached) { return; }
                float r[4];
                if (ZUIDockRectForKey(m_dock_tree, ZUIDockHashName(panel_name), r))
                    AssignRegion(cmp, r[0], r[1], r[2] - r[0], r[3] - r[1]);
            };

            AssignFromDock(Hierarchy, "Hierarchy");
            AssignFromDock(Inspector, "Inspector");
            AssignFromDock(Viewport,  "Viewport");
            AssignFromDock(Log,       "Log");
            AssignFromDock(Project,   "Project");
        }
        AssignRegion(StatusBar, 0.f, sh - status_h, sw, status_h);

        // --- Full-screen background ---
        ZUIBox* bg   = ZUIBeginColumn(ctx, "##dockspace_bg", ZPx(sw), ZPx(sh));
        bg->Flags    = bg->Flags | ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY;
        bg->FloatPos[0] = 0.f;
        bg->FloatPos[1] = 0.f;
        ZUIBoxSetColorArr(bg, ctx->Theme.WindowBg);
        ZUIBoxSetCornerRadius(bg, 0.f);
        bg->EdgeSoftness = 0.f;

        // --- Menu bar ---
        if (ZUIBeginMenuBar(ctx))
        {
            ZUISpacer(ctx, 6.f);

            // "File" menu
            if (ZUIBeginMenu(ctx, "File"))
            {
                auto* file_app = (ParentLayer && ParentLayer->CurrentApp)
                               ? reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp) : nullptr;

                if (ZUIMenuItem(ctx, "New Scene"))
                {
                    // Reset scene: deselect actor, clear scene actors
                    if (file_app && file_app->CurrentScene)
                    {
                        auto* scene = reinterpret_cast<EditorScenePtr>(file_app->CurrentScene);
                        scene->SelectedActorHandle = {};
                    }
                }
                if (ZUIMenuItem(ctx, "Open Scene..."))
                {
                    // Navigate the Project Browser to select a .zescene
                    // (no system file dialog — user uses the Project panel)
                    ZENGINE_CORE_INFO("[Editor] Use the Project panel to locate and drop a .zescene into the viewport")
                }
                ZUISeparator(ctx);
                if (ZUIMenuItem(ctx, "Save Scene"))
                {
                    if (file_app && file_app->CurrentScene && file_app->Configuration)
                    {
                        auto* scene = reinterpret_cast<EditorScenePtr>(file_app->CurrentScene);
                        Serializers::EditorSceneSerializer serializer;
                        serializer.Serialize(scene);
                        ZENGINE_CORE_INFO("[Editor] Scene saved")
                    }
                }
                if (ZUIMenuItem(ctx, "Save Scene As..."))
                {
                    // Same as Save for now (path is determined by scene config)
                    if (file_app && file_app->CurrentScene)
                    {
                        auto* scene = reinterpret_cast<EditorScenePtr>(file_app->CurrentScene);
                        Serializers::EditorSceneSerializer serializer;
                        serializer.Serialize(scene);
                    }
                }
                ZUISeparator(ctx);
                if (ZUIMenuItem(ctx, "Quit"))
                {
                    if (file_app && file_app->CurrentWindow)
                    {
                        auto* glfw_win = static_cast<GLFWwindow*>(
                            file_app->CurrentWindow->GetNativeWindow());
                        if (glfw_win) { glfwSetWindowShouldClose(glfw_win, GLFW_TRUE); }
                    }
                }
                ZUIEndMenu(ctx);
            }
            ZUISpacer(ctx, 4.f);

            // "Edit" menu
            if (ZUIBeginMenu(ctx, "Edit"))
            {
                if (ZUIMenuItem(ctx, "Undo", false)) {}
                if (ZUIMenuItem(ctx, "Redo", false)) {}
                ZUISeparator(ctx);
                if (ZUIMenuItem(ctx, "Select All"))
                {
                    // Select first actor (full multi-select not yet supported)
                    if (ParentLayer && ParentLayer->CurrentApp)
                    {
                        auto* edit_app   = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
                        auto* edit_scene = reinterpret_cast<EditorScenePtr>(edit_app->CurrentScene);
                        auto* eng        = ZEngine::Engine::GetContext();
                        if (edit_scene && eng && eng->ActorManager && eng->ActorManager->Count() > 0)
                        {
                            // Select the first valid actor via ForEach
                            bool found = false;
                            eng->ActorManager->ForEach([&](ZEngine::ECS::ActorHandle h, ZEngine::ECS::Actor*)
                            {
                                if (!found) { edit_scene->SelectedActorHandle = h; found = true; }
                            });
                        }
                    }
                }
                ZUIEndMenu(ctx);
            }
            ZUISpacer(ctx, 4.f);

            // "View" menu — checkable panel toggles (checkmark = visible)
            if (ZUIBeginMenu(ctx, "View"))
            {
                auto vis_item = [&](const char* label, ZUIComponent* cmp) {
                    if (!cmp) { return; }
                    char buf[80];
                    snprintf(buf, sizeof(buf), "%s  %s##vm", cmp->Visible ? "[x]" : "[ ]", label);
                    if (ZUIMenuItem(ctx, buf)) { cmp->Visible = !cmp->Visible; }
                };
                vis_item("Scene",     Viewport);
                vis_item("Hierarchy", Hierarchy);
                vis_item("Inspector", Inspector);
                vis_item("Console",   Log);
                vis_item("Project",   Project);
                ZUIEndMenu(ctx);
            }

            ZUILabel(ctx, " | ", k_dim);

            // Scene name
            if (ParentLayer && ParentLayer->CurrentApp)
            {
                auto* app = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
                if (app->Configuration)
                {
                    const char* sname = app->Configuration->ActiveSceneName.empty()
                                      ? "-" : app->Configuration->ActiveSceneName.c_str();
                    char scene_buf[128];
                    snprintf(scene_buf, sizeof(scene_buf), "Scene: %s", sname);
                    ZUILabel(ctx, scene_buf, k_dim);
                }
            }

            ZUIEndMenuBar(ctx);
        }

        // --- Resize handle: vertical strip between Hierarchy and Viewport ---
        if (m_dock_tree)
        {
            float hr[4] = {}, vr[4] = {};
            bool got_h = ZUIDockRectForKey(m_dock_tree, ZUIDockHashName("Hierarchy"), hr);
            bool got_v = ZUIDockRectForKey(m_dock_tree, ZUIDockHashName("Viewport"),  vr);
            if (got_h && got_v)
            {
                // Place a 4-px wide invisible drag strip at the boundary
                float strip_x = hr[2] - 2.f;
                float strip_y = hr[1];
                float strip_h = hr[3] - hr[1];

                ZUIBox* strip = ZUIPushBox(ctx, "##dock_resize_hv", 8,
                                            ZUI_FloatX | ZUI_FloatY | ZUI_Clickable);
                strip->Size[0]     = ZPx(4.f);
                strip->Size[1]     = ZPx(strip_h);
                strip->FloatPos[0] = strip_x;
                strip->FloatPos[1] = strip_y;
                ZUIBoxSetColor(strip, 0.f, 0.f, 0.f, 0.f);
                ZUISignal rs = ZUISignalFromBox(ctx, strip);
                ZUIPopBox(ctx);

                if ((rs.Flags & ZUI_SignalHeld) && rs.DragDelta[0] != 0.f)
                {
                    ZUIDockNode* hier_leaf = m_dock_tree->Root ? m_dock_tree->Root->First : nullptr;
                    if (hier_leaf)
                        ZUIDockResize(m_dock_tree, hier_leaf, rs.DragDelta[0]);
                }
            }
        }

        ZUIEndColumn(ctx);
    }
} // namespace Tetragrama::Components
