#include <Tetragrama/Components/ZUI/ZUIChrome.h>
#include <Tetragrama/Components/ZUI/ZUIDockspaceComponent.h>
#include <Tetragrama/Components/ZUI/ZUIRecentLevels.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorScene.h>
#include <ZEngine/Core/Coroutine.h>
#include <ZEngine/Core/MainThreadScheduler.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/UI/ZUIDockspace.h>
#include <ZEngine/UI/ZUIFont.h>
#include <ZEngine/UI/ZUITokens.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using namespace ZEngine::UI;

namespace Tetragrama::Components
{
    // Must run via MainThreadScheduler::Post, not inline in BuildUI — a native
    // dialog called directly from the UI-build call stack previously deadlocked (#645).
    static std::future<void> OpenSceneDialogAsync(EditorPtr app)
    {
        if (!app || !app->CurrentWindow || !app->Configuration)
            co_return;

        std::filesystem::path         ws(app->Configuration->WorkingSpacePath.c_str());
        std::filesystem::path         scene_dir = ws / app->Configuration->ScenePath.c_str();

        std::vector<std::string_view> filters   = {".zescene"};
        std::string                   picked    = co_await app->CurrentWindow->OpenFileDialogAsync(filters, scene_dir.string(), "Select a scene file");
        if (!picked.empty())
        {
            Recent().Push(picked.c_str());
            app->OpenScene(picked.c_str());
        }
    }

    static constexpr float k_dim[4] = {0.55f, 0.55f, 0.60f, 1.f};

    void                   ZUIDockspaceComponent::Initialize(Tetragrama::Layers::ZUILayer* parent, cstring name, bool visibility)
    {
        ParentLayer = parent;
        Name        = name;
        Visible     = visibility;
    }

    void ZUIDockspaceComponent::BuildUI(ZUIContext* ctx)
    {
        if (!Visible)
        {
            return;
        }

        float   sw      = (float) ctx->ScreenW;
        float   sh      = (float) ctx->ScreenH;

        // Full-screen overlay — alpha=0 background so ZFill() children resolve
        // correctly, but visually transparent. Renders on top of PanelManagerComponent.
        ZUIBox* bg      = ZUIBeginColumn(ctx, "##dockspace_bg", ZPx(sw), ZPx(sh));
        bg->Flags       = bg->Flags | ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY;
        bg->FloatPos[0] = 0.f;
        bg->FloatPos[1] = 0.f;
        ZUIBoxSetColor(bg, 0.f, 0.f, 0.f, 0.f);
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
        char sc_new[24] = {}, sc_open[24] = {}, sc_save[24] = {}, sc_save_as[24] = {}, sc_undo[24] = {}, sc_redo[24] = {}, sc_all[24] = {};
        snprintf(sc_new, sizeof(sc_new), "%sN", kMod);
        snprintf(sc_open, sizeof(sc_open), "%sO", kMod);
        snprintf(sc_save, sizeof(sc_save), "%sS", kMod);
        snprintf(sc_save_as, sizeof(sc_save_as), "%sS", kModShift);
        snprintf(sc_undo, sizeof(sc_undo), "%sZ", kMod);
        snprintf(sc_redo, sizeof(sc_redo), "%sY", kMod);
        snprintf(sc_all, sizeof(sc_all), "%sA", kMod);

        // Menu bar
        if (ZUIBeginMenuBar(ctx))
        {
            // Engine mark: the app logo, a 26 px box whose artwork fills ~19 px so the mark sits at x=12
            // as in Figma; menus start at x=43 (labels at 54, 22 px apart).
            if (!m_logo_requested)
            {
                m_logo_requested = true;
                const auto* eng  = ZEngine::Engine::GetContext();
                if (eng && eng->EngineAssetsNativeRoot && eng->RenderResourceManager)
                {
                    const auto logo_path = (std::filesystem::path(eng->EngineAssetsNativeRoot) / "Settings/Icons/AppIconWhite.png").string();
                    m_logo_texture       = eng->RenderResourceManager->SubmitTextureFile(logo_path.c_str());
                }
            }
            ZUISpacer(ctx, 9.f);
            {
                ZUIBox* mark  = ZUIPushBox(ctx, "##engine_mark", 13, m_logo_texture.Valid() ? ZUI_DrawBackground : ZUI_None);
                mark->Size[0] = ZPx(26.f);
                mark->Size[1] = ZPx(26.f);
                if (m_logo_texture.Valid())
                {
                    mark->TextureIndex = (uint32_t) m_logo_texture.Index;
                    mark->TextureFlipV = true;
                    ZUIBoxSetColor(mark, 1.f, 1.f, 1.f, 1.f); // quad must have alpha > 0 to be emitted
                    mark->EdgeSoftness = 0.f;
                }
                ZUIPopBox(ctx);
            }
            ZUISpacer(ctx, 8.f);

            // "File" menu
            ctx->PopupDesiredW = 280.f; // Figma "File menu" width; consumed only if the popup is open
            if (ZUIBeginMenu(ctx, "File"))
            {
                auto* file_app = (ParentLayer && ParentLayer->CurrentApp) ? reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp) : nullptr;

                if (ZUIMenuItemEx(ctx, "New Level" ZUI_GLYPH_ELLIPSIS, sc_new, false, true, (int) ZUIIcon::Plus))
                {
                    ZEngine::Core::MainThreadScheduler::Post(file_app, [](void* ptr) {
                        auto* app     = reinterpret_cast<EditorPtr>(ptr);
                        auto* eng_ctx = ZEngine::Engine::GetContext();
                        if (app && app->CurrentScene && eng_ctx && eng_ctx->ActorManager)
                        {
                            eng_ctx->ActorManager->DestroyAll();
                            reinterpret_cast<EditorScenePtr>(app->CurrentScene)->Reset(app->Configuration->DefaultSky);
                        }
                    });
                }
                if (ZUIMenuItemEx(ctx, "Open Level" ZUI_GLYPH_ELLIPSIS, sc_open, false, true, (int) ZUIIcon::Folder))
                {
                    ZEngine::Core::MainThreadScheduler::Post(file_app, [](void* ptr) { OpenSceneDialogAsync(reinterpret_cast<EditorPtr>(ptr)); });
                }
                if (Recent().File.empty() && file_app && file_app->Configuration)
                    Recent().Load(std::string(file_app->Configuration->WorkingSpacePath.c_str()) + "/.zengine/recent_levels.txt");
                ctx->PopupDesiredW = 270.f; // Figma "Open Recent submenu" width
                if (ZUIBeginSubMenu(ctx, "Open Recent", true, (int) ZUIIcon::Layers))
                {
                    if (Recent().Count == 0)
                        ZUIMenuItemEx(ctx, "No recent levels", nullptr, false, false);
                    for (int ri = 0; ri < Recent().Count; ++ri)
                    {
                        char name[128] = {}, age[32] = {}, label[160] = {};
                        RecentLevels::Stem(Recent().Items[ri].Path, name, sizeof(name));
                        RecentLevels::Age(Recent().Items[ri].Time, age, sizeof(age));
                        snprintf(label, sizeof(label), "%s##recent%d", name, ri);
                        if (ZUIMenuItemEx(ctx, label, age, false, true, (int) ZUIIcon::Grid3x3))
                        {
                            const std::string path = Recent().Items[ri].Path;
                            Recent().Push(path.c_str());
                            if (file_app)
                                file_app->OpenScene(path.c_str());
                        }
                    }
                    ZUISeparator(ctx);
                    if (ZUIMenuItemEx(ctx, "Clear Recent Levels", nullptr, false, Recent().Count > 0))
                        Recent().Clear();
                    ZUIEndSubMenu(ctx);
                }
                ctx->PopupDesiredW = 280.f;
                ZUISeparator(ctx);
                if (ZUIMenuItemEx(ctx, "Save", sc_save, false, true, (int) ZUIIcon::Save))
                {
                    ZEngine::Core::MainThreadScheduler::Post(file_app, [](void* ptr) {
                        if (auto* app = reinterpret_cast<EditorPtr>(ptr))
                            app->SaveScene();
                    });
                }
                if (ZUIMenuItemEx(ctx, "Save As" ZUI_GLYPH_ELLIPSIS, sc_save_as))
                {
                    ZEngine::Core::MainThreadScheduler::Post(file_app, [](void* ptr) {
                        if (auto* app = reinterpret_cast<EditorPtr>(ptr))
                            app->SaveSceneAs();
                    });
                }
                ZUIMenuItemEx(ctx, "Save All", nullptr); // saves the open level only for now
                ZUISeparator(ctx);
                if (ZUIMenuItemEx(ctx, "Import" ZUI_GLYPH_ELLIPSIS, nullptr, false, true, (int) ZUIIcon::Image))
                {
                    if (file_app && file_app->Configuration)
                    {
                        file_app->Configuration->ShowImporter  = true;
                        file_app->Configuration->FocusImporter = true;
                    }
                }
                ZUIMenuItemEx(ctx, "Export Selected" ZUI_GLYPH_ELLIPSIS, nullptr, false, false);
                ZUISeparator(ctx);
                ZUIMenuItemEx(ctx, "Project Settings" ZUI_GLYPH_ELLIPSIS, nullptr, false, true, (int) ZUIIcon::SlidersHorizontal);
                ZUISeparator(ctx);
                if (ZUIMenuItemEx(ctx, "Exit", kQuitShortcut, false, true, (int) ZUIIcon::X))
                {
                    ZEngine::Core::MainThreadScheduler::Post(file_app, [](void*) { ZEngine::Engine::RequestClose(); });
                }
                ZUIEndMenu(ctx);
            }

            ctx->PopupDesiredW = 0.f;

            // "Edit" menu
            if (ZUIBeginMenu(ctx, "Edit"))
            {
                if (ZUIMenuItemEx(ctx, "Undo", sc_undo, false, false))
                {
                }
                if (ZUIMenuItemEx(ctx, "Redo", sc_redo, false, false))
                {
                }
                ZUIEndMenu(ctx);
            }

            // "Window" menu
            if (ZUIBeginMenu(ctx, "Window"))
            {
                ZUIMenuItem(ctx, "Main Panels", false);
                ZUISeparator(ctx);

                static const char* kPanels[] = {"Outliner", "Console", "Details", "Content Browser"};
                for (int pi = 0; pi < 4; ++pi)
                {
                    bool vis = ShellPanelManager && ShellPanelManager->IsPanelVisible(kPanels[pi]);
                    if (ZUIMenuItemEx(ctx, kPanels[pi], nullptr, vis) && ShellPanelManager)
                        ShellPanelManager->SetPanelVisible(kPanels[pi], !vis);
                }
                ZUISeparator(ctx);
                bool prof_vis = ShellPanelManager && ShellPanelManager->IsPanelVisible("Profiler");
                if (ZUIMenuItemEx(ctx, "Memory Profiler", nullptr, prof_vis) && ShellPanelManager)
                    ShellPanelManager->SetPanelVisible("Profiler", !prof_vis);
                ZUIEndMenu(ctx);
            }

            // "Tools" menu
            if (ZUIBeginMenu(ctx, "Tools"))
            {
                if (ZUIMenuItemEx(ctx, "Scene Settings", nullptr, m_settings_open))
                {
                    m_settings_open = !m_settings_open;
                    if (m_settings_open)
                        m_settings_just_opened = true;
                }
                ZUIEndMenu(ctx);
            }

            // "Build" menu (no build pipeline in the editor yet)
            if (ZUIBeginMenu(ctx, "Build"))
            {
                ZUIMenuItemEx(ctx, "Build Lighting", nullptr, false, false);
                ZUIEndMenu(ctx);
            }

            // "Select" menu
            if (ZUIBeginMenu(ctx, "Select"))
            {
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
                ZUIMenuItemEx(ctx, "Select None", nullptr, false, false);
                ZUIEndMenu(ctx);
            }

            // "Actor" menu (actor creation lives in the Outliner for now)
            if (ZUIBeginMenu(ctx, "Actor"))
            {
                ZUIMenuItemEx(ctx, "Add Actor", nullptr, false, false);
                ZUIEndMenu(ctx);
            }

            // "Help" menu
            if (ZUIBeginMenu(ctx, "Help"))
            {
                ZUIMenuItemEx(ctx, "About ZEngine", nullptr, false, false);
                ZUIEndMenu(ctx);
            }

            // Project title, right-aligned (the native window supplies the window controls).
            {
                ZUIBox* fill  = ZUIPushBox(ctx, "##menu_fill", 11, ZUI_None);
                fill->Size[0] = ZFill();
                fill->Size[1] = ZPx(1.f);
                ZUIPopBox(ctx);

                auto*       title_app  = (ParentLayer && ParentLayer->CurrentApp) ? reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp) : nullptr;
                const char* project    = (title_app && title_app->Configuration && !title_app->Configuration->ProjectName.empty()) ? title_app->Configuration->ProjectName.c_str() : "ZEngine";
                char        title[160] = {};
                snprintf(title, sizeof(title), "%s - Level Editor", project);
                ZUILabel(ctx, title, ctx->Theme.TextDim);
                ZUISpacer(ctx, 12.f);
            }

            ZUIEndMenuBar(ctx);
        }

        // Level tab row and main toolbar (Figma chrome, 100 px in total with the menu bar).
        {
            auto*       chrome_app   = (ParentLayer && ParentLayer->CurrentApp) ? reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp) : nullptr;
            auto*       chrome_scene = chrome_app ? reinterpret_cast<EditorScenePtr>(chrome_app->CurrentScene) : nullptr;
            const char* level        = (chrome_app && chrome_app->Configuration && !chrome_app->Configuration->ActiveSceneName.empty()) ? chrome_app->Configuration->ActiveSceneName.c_str() : "Untitled";
            Chrome::BuildLevelTabs(ctx, ShellPanelManager, level, chrome_scene && chrome_scene->IsDirty());

            const Chrome::ToolbarResult tb = Chrome::BuildMainToolbar(ctx);
            if (tb.SaveScene && chrome_app)
            {
                ZEngine::Core::MainThreadScheduler::Post(chrome_app, [](void* ptr) {
                    if (auto* app = reinterpret_cast<EditorPtr>(ptr))
                        app->SaveScene();
                });
            }
            if ((tb.NewActor || tb.NewCollection) && chrome_scene)
            {
                auto* eng = ZEngine::Engine::GetContext();
                if (eng && eng->ActorManager)
                {
                    chrome_scene->CreateActor(*eng->ActorManager, tb.NewCollection ? "Collection" : "Actor");
                }
            }
            if (tb.OpenSettings)
            {
                m_settings_open = !m_settings_open;
                if (m_settings_open)
                    m_settings_just_opened = true;
            }
        }

        // Scene Settings window
        if (m_settings_open)
        {
            static constexpr float kSideW          = 160.f;
            static constexpr float kMinW           = 400.f;
            static constexpr float kMinH           = 300.f;
            float&                 kW              = m_modal_w; // alias for readability
            float&                 kH              = m_modal_h;
            float                  fh              = ZUIGetFrameHeight(ctx);

            auto*                  stg_app         = (ParentLayer && ParentLayer->CurrentApp) ? reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp) : nullptr;
            auto*                  stg_scene       = stg_app ? reinterpret_cast<EditorScenePtr>(stg_app->CurrentScene) : nullptr;

            auto                   mark_grid_dirty = [stg_scene]() {
                if (!stg_scene)
                    return;
                stg_scene->GridDirty[0].value.store(true, std::memory_order_release);
                stg_scene->GridDirty[1].value.store(true, std::memory_order_release);
                stg_scene->GridDirty[2].value.store(true, std::memory_order_release);
            };
            auto mark_sky_dirty = [stg_scene]() {
                if (!stg_scene)
                    return;
                stg_scene->MarkSkyDirty();
                stg_scene->MarkDirty(true);
            };

            // Lazy center on first open
            if (m_modal_x < 0.f)
            {
                m_modal_x = (sw - kW) * 0.5f;
                m_modal_y = (sh - kH) * 0.5f;
            }
            // Moving the native window between displays can shrink its logical
            // bounds. Retained modal coordinates must not strand its controls.
            m_modal_x = std::clamp(m_modal_x, 0.f, std::max(0.f, sw - kW));
            m_modal_y = std::clamp(m_modal_y, 0.f, std::max(0.f, sh - kH));

            // Dim backdrop — visual only; NOT Clickable (a clickable full-screen sibling
            // visited after the window in LIFO traversal would overwrite HotKey for all
            // window children). Click-outside is detected via bounds check below instead.
            {
                ZUIBox* dim      = ZUIPushBox(ctx, "##stg_dim", 9, ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
                dim->Size[0]     = ZPx(sw);
                dim->Size[1]     = ZPx(sh);
                dim->FloatPos[0] = 0.f;
                dim->FloatPos[1] = 0.f;
                ZUIBoxSetColor(dim, 0.f, 0.f, 0.f, 0.55f);
                ZUIPopBox(ctx);
            }

            // The shared focus trap transfers focus into the modal and restores
            // the owner on close; keep its key intact until that pass runs.
            if (m_settings_just_opened)
            {
                m_settings_just_opened = false;
            }

            // Modal window — centered.
            // Register as ModalBox so the interaction pass restricts hover to this subtree.
            ZUIBox* win      = ZUIBeginColumn(ctx, "##stg_win", ZPx(kW), ZPx(kH));
            win->Flags       = win->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DropShadow | ZUI_FloatX | ZUI_FloatY | ZUI_ClipChildren;
            ctx->ModalBox    = win;
            win->ShadowKind  = ZUIShadowKind::Modal;
            win->FloatPos[0] = m_modal_x;
            win->FloatPos[1] = m_modal_y;
            {
                const float win_bg[4] = ZUI_TOKEN(ZEngine::UI::Tokens::BgPanel);
                ZUIBoxSetColorArr(win, win_bg);
            }
            ZUIBoxSetCornerRadius(win, Metrics::Radius6);
            win->BorderThickness = 1.f;
            win->BorderColor[0]  = ZEngine::UI::Tokens::BorderStrong.R;
            win->BorderColor[1]  = ZEngine::UI::Tokens::BorderStrong.G;
            win->BorderColor[2]  = ZEngine::UI::Tokens::BorderStrong.B;
            win->BorderColor[3]  = 1.f;
            win->EdgeSoftness    = 0.f;
            // Child surfaces must stay inside the modal's stroke. A rectangular
            // scissor cannot protect its rounded corners or a parent border.
            win->Padding[0] = win->Padding[1] = win->Padding[2] = win->Padding[3] = 1.f;

            // Title bar (Figma "Engine settings": 36 px, bg/surface, gear + semibold title + close)
            {
                static constexpr float kTbarH = 36.f;
                ZUIBox*                tbar   = ZUIBeginRow(ctx, "##stg_tbar", ZFill(), ZPx(kTbarH));
                tbar->Flags                   = tbar->Flags | ZUI_DrawBackground | ZUI_Clickable | ZUI_NoKeyboardFocus;
                const float tbar_bg[4]        = ZUI_TOKEN(ZEngine::UI::Tokens::BgSurface);
                ZUIBoxSetColorArr(tbar, tbar_bg);
                ZUIBoxSetTopRadius(tbar, Metrics::Radius6 - 1.f);
                tbar->EdgeSoftness = 0.f;
                tbar->CrossAlign   = ZUIAlign::Center;
                tbar->Padding[0]   = 14.f;
                tbar->Padding[2]   = 10.f;
                tbar->Gap          = 8.f;

                ZUIIconBox(ctx, ZUIIcon::Settings, 14.f, ctx->Theme.TextDim);
                ZUIBox* ttl    = ZUIPushBox(ctx, "##stg_ttl", 9, ZUI_DrawText);
                ttl->Size[0]   = ZFill();
                ttl->Size[1]   = ZFill();
                ttl->Label     = ZUIPushStr(&ctx->FrameArena, "Engine Settings", 15);
                ttl->FontSize  = ZUIFontSize::Header;
                ttl->TextAlign = ZUITextAlign::Left;
                ZEngine::Helpers::secure_memcpy(ttl->TextColor, sizeof(ttl->TextColor), ctx->Theme.TextDefault, sizeof(ttl->TextColor));
                ZUIPopBox(ctx);

                ZUIBox* xb     = ZUIBeginRow(ctx, "##stg_close", ZPx(22.f), ZPx(22.f));
                xb->Flags      = xb->Flags | ZUI_DrawBackground | ZUI_Clickable | ZUI_KeyboardActivate;
                xb->MainAlign  = ZUIAlign::Center;
                xb->CrossAlign = ZUIAlign::Center;
                ZUIBoxSetCornerRadius(xb, 4.f);
                ZUIIconBox(ctx, ZUIIcon::X, 12.f, ctx->Theme.TextDim);
                ZUISignal xsig = ZUISignalFromBox(ctx, xb);
                if (ctx->ActiveKey == xb->Key)
                    ZUIBoxSetColorArr(xb, ctx->Theme.ButtonActiveBg);
                else if (ctx->HotKey == xb->Key)
                    ZUIBoxSetColorArr(xb, ctx->Theme.ButtonHoveredBg);
                else
                    ZUIBoxSetColor(xb, 0.f, 0.f, 0.f, 0.f);
                ZUIEndRow(ctx);
                if (xsig.Flags & ZUI_SignalClicked)
                    m_settings_open = false;

                ZUISignalFromBox(ctx, tbar); // consume hot/active state for drag
                // Drag title bar to move modal
                if (ctx->ActiveKey == tbar->Key && ctx->MouseDown[0])
                {
                    float dx  = ctx->MousePos[0] - ctx->PrevMousePos[0];
                    float dy  = ctx->MousePos[1] - ctx->PrevMousePos[1];
                    m_modal_x = fmaxf(0.f, fminf(m_modal_x + dx, sw - kW));
                    m_modal_y = fmaxf(0.f, fminf(m_modal_y + dy, sh - kH));
                }
                ZUIEndRow(ctx);
            }

            // Body row: sidebar | separator | content
            ZUIBeginRow(ctx, "##stg_body", ZFill(), ZFill());

            // Sidebar (Figma: 160 px, bg/window, 8 px padding, 28 px page rows)
            ZUIBox* side           = ZUIBeginColumn(ctx, "##stg_side", ZPx(kSideW), ZFill());
            side->Flags            = side->Flags | ZUI_DrawBackground;
            const float side_bg[4] = ZUI_TOKEN(ZEngine::UI::Tokens::BgWindow);
            ZUIBoxSetColorArr(side, side_bg);
            side->EdgeSoftness = 0.f;
            side->Padding[0] = side->Padding[2] = 8.f;
            side->Padding[1] = side->Padding[3] = 8.f;
            side->Gap                           = 2.f;
            {
                static const char*   kPages[5] = {"Grid", "Sky", "Renderer", "Theme", "Layout"};
                static const ZUIIcon kIcons[5] = {ZUIIcon::Grid3x3, ZUIIcon::Sun, ZUIIcon::Image, ZUIIcon::Layers, ZUIIcon::PanelBottom};
                const float          sel[4]    = ZUI_TOKEN(ZEngine::UI::Tokens::AccentSelection);
                for (int pi = 0; pi < 5; ++pi)
                {
                    const bool act    = (m_settings_page == pi);
                    char       nk[24] = {};
                    snprintf(nk, sizeof(nk), "##nav_%d", pi);
                    ZUIBox* nb       = ZUIBeginRow(ctx, nk, ZFill(), ZPx(28.f));
                    nb->Flags        = nb->Flags | ZUI_DrawBackground | ZUI_Clickable | ZUI_KeyboardActivate;
                    nb->CrossAlign   = ZUIAlign::Center;
                    nb->Padding[0]   = 10.f;
                    nb->Gap          = 8.f;
                    nb->EdgeSoftness = 0.5f;
                    ZUIBoxSetCornerRadius(nb, 4.f);
                    const bool   hov = (ctx->HotKey == nb->Key);
                    const float* fg  = (act || hov) ? ctx->Theme.TextDefault : ctx->Theme.TextDim;
                    ZUIIconBox(ctx, kIcons[pi], 13.f, fg);
                    ZUILabelTight(ctx, kPages[pi], fg, ZUIFontSize::Body);
                    ZUISignal sig = ZUISignalFromBox(ctx, nb);
                    if (act)
                        ZUIBoxSetColorArr(nb, sel);
                    else if (hov)
                        ZUIBoxSetColorArr(nb, ctx->Theme.ButtonHoveredBg);
                    else
                        ZUIBoxSetColor(nb, 0.f, 0.f, 0.f, 0.f);
                    ZUIEndRow(ctx);
                    if (sig.Flags & ZUI_SignalClicked)
                        m_settings_page = pi;
                }
            }
            ZUIEndColumn(ctx); // sidebar

            // Vertical separator
            {
                ZUIBox* vs         = ZUIPushBox(ctx, "##stg_vsep", 9, ZUI_DrawBackground);
                vs->Size[0]        = ZPx(1.f);
                vs->Size[1]        = ZFill();
                const float sub[4] = ZUI_TOKEN(ZEngine::UI::Tokens::BorderSubtle);
                ZUIBoxSetColorArr(vs, sub);
                vs->EdgeSoftness = 0.f;
                ZUIPopBox(ctx);
            }

            // Content column
            ZUIBox* cnt       = ZUIBeginColumn(ctx, "##stg_cnt", ZFill(), ZFill());
            cnt->Flags        = cnt->Flags | ZUI_ClipChildren;
            cnt->EdgeSoftness = 0.f;
            cnt->Padding[0]   = 20.f;
            cnt->Padding[2]   = 20.f;
            ZUISpacer(ctx, 16.f);
            {
                static const char* kTitles[5] = {"Grid", "Sky", "Renderer", "Theme", "Layout"};
                ZUILabel(ctx, kTitles[m_settings_page < 5 ? m_settings_page : 0], ctx->Theme.TextDefault, ZUIFontSize::Header);
                ZUISpacer(ctx, 8.f);
                ZUIBox* hr         = ZUIPushBox(ctx, "##stg_hrule", 10, ZUI_DrawBackground);
                hr->Size[0]        = ZFill();
                hr->Size[1]        = ZPx(1.f);
                const float sub[4] = ZUI_TOKEN(ZEngine::UI::Tokens::BorderSubtle);
                ZUIBoxSetColorArr(hr, sub);
                hr->EdgeSoftness = 0.f;
                ZUIPopBox(ctx);
                ZUISpacer(ctx, 10.f);
            }
            ZUIBeginScrollRegion(ctx, "##stg_scroll", ZFill(), ZFill());
            const auto property_label = [&](const char* key) {
                auto* box = ZUIBeginColumn(ctx, key, ZPx(152.f), ZFill());
                return box;
            };
            const auto begin_section = [&](const char* title, bool& open) {
                constexpr float background[4] = ZUI_TOKEN(Tokens::BgSurface);
                ZUICollapsingHeader(ctx, title, &open, background, true);
                auto* header       = ctx->Current->LastChild;
                header->Size[1]    = ZPx(26.f);
                header->Padding[0] = 8.f;
                // Settings headers retain their surface fill when hovered or
                // pressed; the generic dock-header active fill is darker.
                ZUIBoxSetColorArr(header, background);
                auto* chevron    = header->FirstChild;
                chevron->Size[0] = chevron->Size[1] = ZPx(16.f);
                chevron->NextSib->Size[0]           = ZPx(6.f);
                header->CustomDraw                  = [](ZUIDrawList* draw, const ZUIBox* box, const void*) {
                    constexpr float border[4] = ZUI_TOKEN(Tokens::BorderSubtle);
                    ZUIDrawListAddRectFilledNoAA(draw, box->ScreenMin[0], box->ScreenMax[1] - 1.f, box->ScreenMax[0], box->ScreenMax[1], ZUIPackColor(border));
                };
                if (!open)
                {
                    ZUISpacer(ctx, 4.f);
                    return false;
                }
                char key[80] = {};
                snprintf(key, sizeof(key), "##settings_rows_%s", title);
                auto* rows       = ZUIBeginColumn(ctx, key, ZFill(), ZFit());
                rows->Padding[0] = 8.f;
                rows->Padding[1] = 10.f;
                rows->Padding[3] = 6.f;
                rows->Gap        = 10.f;
                return true;
            };
            const auto end_section = [&]() {
                ZUIEndColumn(ctx);
                ZUISpacer(ctx, 4.f);
            };

            if (m_settings_page == 0 && stg_scene) // Grid
            {
                auto& cfg = stg_scene->Grid;

                // Show Grid
                {
                    ZUIBeginRow(ctx, "##sg_en_r", ZFill(), ZPx(fh + 4.f));
                    bool prev = cfg.Enabled;
                    ZUICheckbox(ctx, "Show Grid##sg_en_cb", &cfg.Enabled);
                    ZUIEndRow(ctx);
                    ZUISpacer(ctx, 5.f);
                    if (cfg.Enabled != prev)
                        mark_grid_dirty();
                }
                // Cell Size
                {
                    float prev = cfg.CellSize;
                    ZUIBeginRow(ctx, "##sg_cs_r", ZFill(), ZPx(fh + 6.f));
                    property_label("##sg_cs_l");
                    ZUILabel(ctx, "Cell Size", ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    ZUIDragFloat(ctx, "##sg_cs_s", &cfg.CellSize, 0.01f, 120.f);
                    cfg.CellSize = fmaxf(0.001f, fminf(cfg.CellSize, 1.f));
                    ZUIEndRow(ctx);
                    ZUISpacer(ctx, 5.f);
                    if (cfg.CellSize != prev)
                        mark_grid_dirty();
                }
                // Fade Radius
                {
                    float prev = cfg.FadeRadius;
                    ZUIBeginRow(ctx, "##sg_fr_r", ZFill(), ZPx(fh + 6.f));
                    property_label("##sg_fr_l");
                    ZUILabel(ctx, "Fade Radius", ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    ZUIDragFloat(ctx, "##sg_fr_s", &cfg.FadeRadius, 1.0f, 120.f);
                    cfg.FadeRadius = fmaxf(10.f, fminf(cfg.FadeRadius, 2000.f));
                    ZUIEndRow(ctx);
                    ZUISpacer(ctx, 5.f);
                    if (cfg.FadeRadius != prev)
                        mark_grid_dirty();
                }
                // Fade Strength
                {
                    float prev = cfg.FadeStrength;
                    ZUIBeginRow(ctx, "##sg_fs_r", ZFill(), ZPx(fh + 6.f));
                    property_label("##sg_fs_l");
                    ZUILabel(ctx, "Fade Strength", ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    ZUIDragFloat(ctx, "##sg_fs_s", &cfg.FadeStrength, 0.01f, 120.f);
                    cfg.FadeStrength = fmaxf(0.1f, fminf(cfg.FadeStrength, 2.f));
                    ZUIEndRow(ctx);
                    ZUISpacer(ctx, 5.f);
                    if (cfg.FadeStrength != prev)
                        mark_grid_dirty();
                }
                // Line Width
                {
                    float prev = cfg.LineWidth;
                    ZUIBeginRow(ctx, "##sg_lw_r", ZFill(), ZPx(fh + 6.f));
                    property_label("##sg_lw_l");
                    ZUILabel(ctx, "Line Width", ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    ZUIDragFloat(ctx, "##sg_lw_s", &cfg.LineWidth, 0.01f, 120.f);
                    cfg.LineWidth = fmaxf(0.5f, fminf(cfg.LineWidth, 4.f));
                    ZUIEndRow(ctx);
                    ZUISpacer(ctx, 5.f);
                    if (cfg.LineWidth != prev)
                        mark_grid_dirty();
                }
                // Ground Y
                {
                    float prev = cfg.GroundY;
                    ZUIBeginRow(ctx, "##sg_gy_r", ZFill(), ZPx(fh + 6.f));
                    property_label("##sg_gy_l");
                    ZUILabel(ctx, "Ground Y", ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    ZUIDragFloat(ctx, "##sg_gy_s", &cfg.GroundY, 0.1f, 120.f);
                    cfg.GroundY = fmaxf(-100.f, fminf(cfg.GroundY, 100.f));
                    ZUIEndRow(ctx);
                    ZUISpacer(ctx, 5.f);
                    if (cfg.GroundY != prev)
                        mark_grid_dirty();
                }
                // Max LOD
                {
                    int prev = cfg.MaxLOD;
                    ZUIBeginRow(ctx, "##sg_ml_r", ZFill(), ZPx(fh + 6.f));
                    property_label("##sg_ml_l");
                    ZUILabel(ctx, "Max LOD", ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    ZUIBeginColumn(ctx, "##sg_ml_w", ZPx(120.f), ZFill());
                    ZUIDragInt(ctx, "##sg_ml_d", &cfg.MaxLOD, 1.f, 60.f);
                    ZUIEndColumn(ctx);
                    ZUIEndRow(ctx);
                    ZUISpacer(ctx, 5.f);
                    if (cfg.MaxLOD != prev)
                        mark_grid_dirty();
                }

                ZUISpacer(ctx, 14.f);

                // Color rows
                auto color_row = [&](const char* rk, const char* lk, const char* ck, const char* label, float col[4]) {
                    float p[4] = {col[0], col[1], col[2], col[3]};
                    ZUIBeginRow(ctx, rk, ZFill(), ZPx(fh + 6.f));
                    property_label(lk);
                    ZUILabel(ctx, label, ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    // GridPass blends into FrameColor after tone mapping: these
                    // are already display-encoded values, unlike sky colors.
                    ZUIColorEdit4(ctx, ck, col, ZUIColorSpace::Srgb);
                    ZUIEndRow(ctx);
                    ZUISpacer(ctx, 5.f);
                    if (col[0] != p[0] || col[1] != p[1] || col[2] != p[2] || col[3] != p[3])
                        mark_grid_dirty();
                };
                color_row("##sg_ct_r", "##sg_ct_l", "##sg_ct_c", "Thin Lines", cfg.ColorThin);
                color_row("##sg_ck_r", "##sg_ck_l", "##sg_ck_c", "Thick Lines", cfg.ColorThick);
                color_row("##sg_cx_r", "##sg_cx_l", "##sg_cx_c", "X Axis", cfg.ColorXAxis);
                color_row("##sg_cz_r", "##sg_cz_l", "##sg_cz_c", "Z Axis", cfg.ColorZAxis);
                ZUISpacer(ctx, 14.f); // bottom padding
            }
            else if (m_settings_page == 1 && stg_scene) // Sky
            {
                auto& cfg        = stg_scene->Sky;
                auto  scalar_row = [&](const char* row_key, const char* label, const char* control_key, float* value, float minimum, float maximum, int decimals = 3) {
                    auto* row = ZUIBeginRow(ctx, row_key, ZFill(), ZPx(25.f));
                    row->Gap  = 12.f;
                    property_label("##sky_scalar_label");
                    ZUILabel(ctx, label, ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    auto* controls     = ZUIBeginRow(ctx, control_key, ZPx(272.f), ZPx(22.f));
                    controls->Gap      = 8.f;
                    char track_key[96] = {};
                    snprintf(track_key, sizeof(track_key), "%s_track", control_key);
                    const bool changed       = ZUISliderFloat(ctx, track_key, value, minimum, maximum, ZPx(216.f), ZPx(22.f));
                    char       value_key[96] = {};
                    snprintf(value_key, sizeof(value_key), "%s_value", control_key);
                    auto* cell       = ZUIBeginColumn(ctx, value_key, ZPx(48.f), ZPx(22.f));
                    cell->CrossAlign = ZUIAlign::Center;
                    char text[32]    = {};
                    snprintf(text, sizeof(text), "%.*f", decimals, *value);
                    ZUILabelTight(ctx, text, ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    ZUIEndRow(ctx);
                    ZUIEndRow(ctx);
                    return changed;
                };
                auto drag_scalar_row = [&](const char* row_key, const char* label, const char* control_key, float* value, float speed, float minimum, float maximum, int decimals = 3) {
                    const float previous = *value;
                    auto*       row      = ZUIBeginRow(ctx, row_key, ZFill(), ZPx(25.f));
                    row->Gap             = 12.f;
                    property_label("##sky_drag_scalar_label");
                    ZUILabel(ctx, label, ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    const int saved_decimals = ctx->DragFloatDecimals;
                    ctx->DragFloatDecimals   = decimals;
                    ZUIDragFloat(ctx, control_key, value, speed, 120.f);
                    ctx->DragFloatDecimals = saved_decimals;
                    ZUIEndRow(ctx);
                    if (*value < minimum)
                        *value = minimum;
                    else if (*value > maximum)
                        *value = maximum;
                    return previous != *value;
                };
                auto color_row = [&](const char* row_key, const char* label, const char* control_key, float value[4]) {
                    auto* row = ZUIBeginRow(ctx, row_key, ZFill(), ZPx(25.f));
                    row->Gap  = 12.f;
                    property_label("##sky_color_label");
                    ZUILabel(ctx, label, ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    // Sky shaders consume scene-linear radiance/multipliers.
                    const bool changed = ZUIColorEdit4(ctx, control_key, value, ZUIColorSpace::Linear);
                    ZUIEndRow(ctx);
                    return changed;
                };
                auto color3_row = [&](const char* row_key, const char* label, const char* control_key, float value[3]) {
                    auto* row = ZUIBeginRow(ctx, row_key, ZFill(), ZPx(25.f));
                    row->Gap  = 12.f;
                    property_label("##sky_color3_label");
                    ZUILabel(ctx, label, ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    const bool changed = ZUIColorEdit3(ctx, control_key, value, ZUIColorSpace::Linear);
                    ZUIEndRow(ctx);
                    return changed;
                };
                auto vector3_row = [&](const char* row_key, const char* label, const char* control_key, float value[3], float speed, bool non_negative) {
                    float previous[3] = {value[0], value[1], value[2]};
                    auto* row         = ZUIBeginRow(ctx, row_key, ZFill(), ZPx(25.f));
                    row->Gap          = 12.f;
                    property_label("##sky_vector3_label");
                    ZUILabel(ctx, label, ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    // Reuse scalar-field interaction, but use the design's compact
                    // three-field layout instead of the legacy axis-label chips.
                    auto* controls                        = ZUIBeginRow(ctx, control_key, ZPx(304.f), ZPx(22.f));
                    controls->Gap                         = 8.f;
                    static constexpr float axes[3][4]     = {ZUI_TOKEN(Tokens::AccentRed), ZUI_TOKEN(Tokens::AccentGreen), ZUI_TOKEN(Tokens::AccentBlue)};
                    const int              saved_decimals = ctx->DragFloatDecimals;
                    ctx->DragFloatDecimals                = non_negative ? 6 : 0;
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        char key[96] = {};
                        snprintf(key, sizeof(key), "%s_%d", control_key, axis);
                        ZUIDragFloat(ctx, key, &value[axis], speed, 96.f);
                        auto* field           = controls->LastChild;
                        field->Size[1]        = ZPx(22.f);
                        field->Padding[0]     = 9.f;
                        field->CustomDrawData = axes[axis];
                        field->CustomDraw     = [](ZUIDrawList* draw, const ZUIBox* box, const void* data) { ZUIDrawListAddRectFilledNoAA(draw, box->ScreenMin[0] + 1.f, box->ScreenMin[1] + 1.f, box->ScreenMin[0] + 4.f, box->ScreenMax[1] - 1.f, ZUIPackColor(static_cast<const float*>(data))); };
                    }
                    ctx->DragFloatDecimals = saved_decimals;
                    ZUIEndRow(ctx);
                    ZUIEndRow(ctx);
                    if (non_negative)
                    {
                        for (uint32_t index = 0; index < 3; ++index)
                            if (value[index] < 0.0f)
                                value[index] = 0.0f;
                    }
                    return previous[0] != value[0] || previous[1] != value[1] || previous[2] != value[2];
                };

                const auto checkbox_row = [&](const char* key, const char* label, bool* value, const char* tooltip = nullptr) {
                    auto* row = ZUIBeginRow(ctx, key, ZFill(), ZPx(25.f));
                    row->Gap  = 12.f;
                    property_label("##sky_checkbox_label");
                    ZUILabel(ctx, label, ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    const bool previous   = *value;
                    char       cb_key[96] = {};
                    snprintf(cb_key, sizeof(cb_key), "%s_cb", key);
                    ZUICheckbox(ctx, cb_key, value);
                    ZUIEndRow(ctx);
                    if (tooltip && ctx->HotKey == ZUIHashStr(cb_key, static_cast<uint32_t>(ZEngine::Helpers::secure_strlen(cb_key))))
                        ZUISetTooltip(ctx, ZUISignal{.Flags = ZUI_SignalHovered}, tooltip);
                    return previous != *value;
                };
                if (begin_section("Source", m_sky_sections[0]))
                {
                    static constexpr const char* kModeNames[] = {"Atmosphere", "HDRI", "Sky Sphere"};
                    const uint32_t               mode_index   = std::min(2u, static_cast<uint32_t>(cfg.Mode));
                    auto*                        mode_row     = ZUIBeginRow(ctx, "##sky_mode_r", ZFill(), ZPx(25.f));
                    mode_row->Gap                             = 12.f;
                    property_label("##sky_mode_l");
                    ZUILabel(ctx, "Mode", ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    if (ZUIBeginCombo(ctx, "##sky_mode", kModeNames[mode_index], ZPx(280.f)))
                    {
                        for (uint32_t i = 0; i < 3; ++i)
                        {
                            if (ZUIComboItem(ctx, kModeNames[i], i == mode_index))
                            {
                                cfg.Mode = static_cast<ZEngine::Rendering::Scenes::SkyMode>(i);
                                mark_sky_dirty();
                            }
                        }
                        ZUIEndCombo(ctx);
                    }
                    ZUIEndRow(ctx);

                    if (cfg.IsHDRI())
                    {
                        auto* asset_manager = ZEngine::Managers::AssetManager::Instance();
                        auto* registry      = asset_manager ? asset_manager->Registry : nullptr;
                        char  preview[160]  = "No HDRI asset (fallback environment)";
                        if (registry && !cfg.EnvironmentMap.is_nil())
                        {
                            if (const auto* selected = registry->FindByUUID(cfg.EnvironmentMap))
                                std::snprintf(preview, sizeof(preview), "%s", selected->Name);
                            else
                                std::snprintf(preview, sizeof(preview), "Missing HDRI asset (fallback environment)");
                        }

                        auto* hdri_row = ZUIBeginRow(ctx, "##sky_hdri_r", ZFill(), ZPx(25.f));
                        hdri_row->Gap  = 12.f;
                        property_label("##sky_hdri_l");
                        ZUILabel(ctx, "HDRI Asset", ctx->Theme.TextDefault);
                        ZUIEndColumn(ctx);
                        if (ZUIBeginCombo(ctx, "##sky_hdri", preview, ZPx(280.f)))
                        {
                            if (ZUIComboItem(ctx, "None (fallback environment)", cfg.EnvironmentMap.is_nil()))
                            {
                                cfg.EnvironmentMap = {};
                                mark_sky_dirty();
                            }
                            if (registry)
                            {
                                ZEngine::Core::VFS::AssetRegistry::QueryFilter filter = {};
                                filter.Ext                                            = ".hdr";
                                auto candidates                                       = registry->Query(filter, &ctx->FrameArena);
                                for (uint32_t i = 0; i < candidates.Count; ++i)
                                {
                                    const auto* candidate = registry->Access(candidates.Handles[i]);
                                    if (candidate && ZUIComboItem(ctx, candidate->Name, candidate->UUID == cfg.EnvironmentMap))
                                    {
                                        cfg.EnvironmentMap = candidate->UUID;
                                        mark_sky_dirty();
                                    }
                                }
                            }
                            ZUIEndCombo(ctx);
                        }
                        ZUIEndRow(ctx);
                    }

                    auto* light_row = ZUIBeginRow(ctx, "##sky_light_r", ZFill(), ZPx(25.f));
                    light_row->Gap  = 12.f;
                    property_label("##sky_light_l");
                    ZUILabel(ctx, "Primary Celestial Light", ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    auto* engine_context     = ZEngine::Engine::GetContext();
                    auto* actor_manager      = engine_context ? engine_context->ActorManager : nullptr;
                    char  light_preview[160] = "Select a directional light";
                    if (actor_manager && !cfg.PrimaryCelestialLight.is_nil())
                    {
                        bool primary_light_resolved = false;
                        actor_manager->ForEach([&](ZEngine::ECS::ActorHandle, ZEngine::ECS::Actor* actor) {
                            const auto* light = actor->GetComponent<ZEngine::ECS::Components::LightComponent>();
                            const auto* uuid  = actor->GetComponent<ZEngine::ECS::Components::UUIDComponent>();
                            if (!light || light->LightType != ZEngine::ECS::Components::LightComponent::Type::Directional || !uuid || uuid->Value != cfg.PrimaryCelestialLight)
                                return;

                            const auto* name = actor->GetComponent<ZEngine::ECS::Components::NameComponent>();
                            std::snprintf(light_preview, sizeof(light_preview), "%s", name && name->Value[0] ? name->Value : "Directional Light");
                            primary_light_resolved = true;
                        });
                        if (!primary_light_resolved)
                            std::snprintf(light_preview, sizeof(light_preview), "Missing directional light");
                    }
                    if (actor_manager && ZUIBeginCombo(ctx, "##sky_light", light_preview, ZPx(280.f)))
                    {
                        if (ZUIComboItem(ctx, "None##sky_light_none", cfg.PrimaryCelestialLight.is_nil()))
                            stg_scene->ClearPrimaryCelestialLight();

                        actor_manager->ForEach([&](ZEngine::ECS::ActorHandle handle, ZEngine::ECS::Actor* actor) {
                            const auto* light = actor->GetComponent<ZEngine::ECS::Components::LightComponent>();
                            if (!light || light->LightType != ZEngine::ECS::Components::LightComponent::Type::Directional)
                                return;

                            const auto* name            = actor->GetComponent<ZEngine::ECS::Components::NameComponent>();
                            const auto* uuid            = actor->GetComponent<ZEngine::ECS::Components::UUIDComponent>();
                            char        item_label[196] = {};
                            std::snprintf(item_label, sizeof(item_label), "%s##sky_light_%llu_%llu", name && name->Value[0] ? name->Value : "Directional Light", (unsigned long long) handle.Index, (unsigned long long) handle.Generation);
                            if (ZUIComboItem(ctx, item_label, uuid && uuid->Value == cfg.PrimaryCelestialLight))
                                stg_scene->SetPrimaryCelestialLight(handle);
                        });
                        ZUIEndCombo(ctx);
                    }
                    else if (!actor_manager)
                    {
                        ZUILabel(ctx, "No actor manager", ctx->Theme.TextDim);
                    }
                    ZUIEndRow(ctx);
                    end_section();
                }

                if (begin_section("Environment light", m_sky_sections[1]))
                {
                    if (scalar_row("##sky_intensity_r", "Environment Intensity", "##sky_intensity", &cfg.EnvironmentIntensity, 0.0f, 32.0f))
                        mark_sky_dirty();
                    if (color_row("##sky_tint_r", "Environment Tint", "##sky_tint", cfg.EnvironmentTint))
                        mark_sky_dirty();
                    if (scalar_row("##sky_yaw_r", "Environment Yaw (rad)", "##sky_yaw", &cfg.EnvironmentYawRadians, -3.14159265f, 3.14159265f))
                        mark_sky_dirty();
                    end_section();
                }

                if (cfg.IsSkySphere())
                {
                    if (begin_section("Gradient", m_sky_sections[8]))
                    {
                        if (color_row("##sky_sphere_horizon_r", "Horizon Color", "##sky_sphere_horizon", cfg.Sphere.HorizonColor))
                            mark_sky_dirty();
                        if (color_row("##sky_sphere_zenith_r", "Zenith Color", "##sky_sphere_zenith", cfg.Sphere.ZenithColor))
                            mark_sky_dirty();
                        if (color_row("##sky_sphere_ground_r", "Ground Color", "##sky_sphere_ground", cfg.Sphere.GroundColor))
                            mark_sky_dirty();
                        if (scalar_row("##sky_sphere_sharpness_r", "Horizon Sharpness", "##sky_sphere_sharpness", &cfg.Sphere.HorizonSharpness, 0.05f, 16.0f))
                            mark_sky_dirty();
                        end_section();
                    }
                    if (begin_section("Sun disc", m_sky_sections[9]))
                    {
                        if (checkbox_row("##sky_sphere_sun", "Show Sun Disc", &cfg.Sphere.ShowSunDisc))
                            mark_sky_dirty();
                        if (scalar_row("##sky_sphere_radius_r", "Sun Disc Radius (rad)", "##sky_sphere_radius", &cfg.Sphere.SunDiscAngularRadiusRadians, 0.0f, 0.1f, 5))
                            mark_sky_dirty();
                        if (scalar_row("##sky_sphere_intensity_r", "Sun Disc Intensity", "##sky_sphere_intensity", &cfg.Sphere.SunDiscIntensity, 0.0f, 64.0f))
                            mark_sky_dirty();
                        end_section();
                    }
                }
                else if (cfg.IsAtmosphere())
                {
                    if (begin_section("Planet", m_sky_sections[2]))
                    {
                        if (vector3_row("##sky_atm_planet_center_r", "Planet Centre (world)", "##sky_atm_planet_center", cfg.Atmosphere.PlanetCenterWorld, 1000.0f, false))
                            mark_sky_dirty();
                        if (drag_scalar_row("##sky_atm_world_scale_r", "World Units per Metre", "##sky_atm_world_scale", &cfg.Atmosphere.WorldUnitsPerMeter, 0.01f, 0.001f, 1000.0f))
                            mark_sky_dirty();
                        if (drag_scalar_row("##sky_atm_planet_radius_r", "Planet Radius (km)", "##sky_atm_planet_radius", &cfg.Atmosphere.PlanetRadiusKilometers, 1.0f, 1.0f, 100000.0f))
                            mark_sky_dirty();
                        const float minimum_atmosphere_radius = cfg.Atmosphere.PlanetRadiusKilometers + 0.1f;
                        if (drag_scalar_row("##sky_atm_radius_r", "Atmosphere Radius (km)", "##sky_atm_radius", &cfg.Atmosphere.AtmosphereRadiusKilometers, 1.0f, minimum_atmosphere_radius, 100100.0f))
                            mark_sky_dirty();

                        if (stg_app)
                        {
                            if (checkbox_row("##sky_atm_camera_ground", "Lock Camera to Surface", &stg_app->ConstrainCameraToAtmosphereGround, "Constrain editor camera to planet surface"))
                                ZENGINE_CORE_INFO("[EditorCamera] Atmosphere ground constraint {}", stg_app->ConstrainCameraToAtmosphereGround ? "enabled" : "disabled")
                        }
                        end_section();
                    }
                    if (begin_section("Molecular", m_sky_sections[3]))
                    {
                        if (vector3_row("##sky_atm_rayleigh_scatter_r", "Rayleigh Scatter (1/km)", "##sky_atm_rayleigh_scatter", cfg.Atmosphere.RayleighScatteringPerKilometer, 0.0001f, true))
                            mark_sky_dirty();
                        if (drag_scalar_row("##sky_atm_rayleigh_height_r", "Rayleigh Height (km)", "##sky_atm_rayleigh_height", &cfg.Atmosphere.RayleighScaleHeightKilometers, 0.01f, 0.01f, 100.0f))
                            mark_sky_dirty();

                        end_section();
                    }
                    if (begin_section("Aerosols", m_sky_sections[4]))
                    {
                        if (drag_scalar_row("##sky_atm_mie_scatter_r", "Mie Scatter (1/km)", "##sky_atm_mie_scatter", &cfg.Atmosphere.MieScatteringPerKilometer, 0.0001f, 0.0f, 1.0f, 6))
                            mark_sky_dirty();
                        if (drag_scalar_row("##sky_atm_mie_absorb_r", "Mie Absorb (1/km)", "##sky_atm_mie_absorb", &cfg.Atmosphere.MieAbsorptionPerKilometer, 0.0001f, 0.0f, 1.0f, 6))
                            mark_sky_dirty();
                        if (drag_scalar_row("##sky_atm_mie_height_r", "Mie Height (km)", "##sky_atm_mie_height", &cfg.Atmosphere.MieScaleHeightKilometers, 0.01f, 0.01f, 100.0f))
                            mark_sky_dirty();
                        if (drag_scalar_row("##sky_atm_mie_g_r", "Mie Anisotropy", "##sky_atm_mie_g", &cfg.Atmosphere.MieAnisotropy, 0.01f, -0.998f, 0.998f))
                            mark_sky_dirty();

                        end_section();
                    }
                    if (begin_section("Ozone", m_sky_sections[5]))
                    {
                        if (vector3_row("##sky_atm_ozone_absorb_r", "Ozone Absorb (1/km)", "##sky_atm_ozone_absorb", cfg.Atmosphere.OzoneAbsorptionPerKilometer, 0.00001f, true))
                            mark_sky_dirty();
                        if (drag_scalar_row("##sky_atm_ozone_center_r", "Ozone Centre (km)", "##sky_atm_ozone_center", &cfg.Atmosphere.OzoneCenterKilometers, 0.1f, 0.0f, 100.0f))
                            mark_sky_dirty();
                        if (drag_scalar_row("##sky_atm_ozone_thickness_r", "Ozone Thickness (km)", "##sky_atm_ozone_thickness", &cfg.Atmosphere.OzoneThicknessKilometers, 0.1f, 0.01f, 100.0f))
                            mark_sky_dirty();

                        end_section();
                    }
                    if (begin_section("Sun", m_sky_sections[6]))
                    {
                        if (drag_scalar_row("##sky_atm_sun_radius_r", "Sun Angular Radius (rad)", "##sky_atm_sun_radius", &cfg.Atmosphere.SunAngularRadiusRadians, 0.0001f, 0.0f, 0.1f, 5))
                            mark_sky_dirty();
                        if (drag_scalar_row("##sky_atm_sun_lux_r", "Sun Illuminance (lux)", "##sky_atm_sun_lux", &cfg.Atmosphere.SunIlluminanceLux, 100.0f, 0.0f, 200000.0f))
                            mark_sky_dirty();

                        end_section();
                    }
                    if (begin_section("Ground", m_sky_sections[7]))
                    {
                        if (color3_row("##sky_atm_ground_albedo_r", "Ground Albedo", "##sky_atm_ground_albedo", cfg.Atmosphere.GroundAlbedo))
                            mark_sky_dirty();
                        if (drag_scalar_row("##sky_atm_ground_ambient_r", "Ground Ambient", "##sky_atm_ground_ambient", &cfg.Atmosphere.GroundAmbientIrradiance, 0.01f, 0.0f, 16.0f))
                            mark_sky_dirty();
                        end_section();
                    }
                }

                if (begin_section("Diagnostics", m_sky_sections[10]))
                {
                    constexpr float diagnostics_color[4] = ZUI_TOKEN(Tokens::TextDisabled);
                    if (cfg.IsHDRI() && cfg.EnvironmentMap.is_nil())
                        ZUILabel(ctx, "No HDRI asset is assigned. The runtime will select its safe environment state.", diagnostics_color);
                    else if (cfg.IsHDRI())
                        ZUILabel(ctx, "HDRI is referenced by stable asset UUID; generated cache paths are not scene data.", diagnostics_color);
                    else
                        ZUILabel(ctx, "Runtime bake state and per-viewport previews are intentionally not scene data.", diagnostics_color);
                    if (cfg.PrimaryCelestialLight.is_nil())
                        ZUILabel(ctx, "No primary celestial light is assigned; the sky has no direct sun source.", diagnostics_color);
                    end_section();
                }
            }
            else if (m_settings_page == 2) // Renderer
            {
                ZUISpacer(ctx, 12.f);
                ZUISpacer(ctx, 14.f);
                ZUILabel(ctx, "No renderer settings yet.", ctx->Theme.TextDim);
            }
            else if (m_settings_page == 3) // Theme
            {
                if (begin_section("Appearance", m_theme_sections[0]))
                {
                    auto* row = ZUIBeginRow(ctx, "##stg_thm_row", ZFill(), ZPx(80.f));
                    row->Gap  = 12.f;
                    property_label("##stg_theme_label");
                    ZUILabel(ctx, "Theme", ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    ZUIBox* tc = ZUIBeginColumn(ctx, "##thm_0", ZPx(110.f), ZPx(80.f));
                    tc->Flags  = tc->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable | ZUI_KeyboardActivate;
                    ZUIBoxSetColorArr(tc, ctx->Theme.WindowBg);
                    ZUIBoxSetCornerRadius(tc, 4.f);
                    tc->BorderThickness = 2.f;
                    ZEngine::Helpers::secure_memcpy(tc->BorderColor, sizeof(tc->BorderColor), ctx->Theme.InputFocusBorder, sizeof(tc->BorderColor));
                    tc->Padding[0] = 10.f;
                    tc->Padding[1] = 10.f;
                    ZUILabelTight(ctx, "Dark", ctx->Theme.TextDefault);
                    if ((ZUISignalFromBox(ctx, tc).Flags & ZUI_SignalClicked) && stg_app && stg_app->Configuration)
                        stg_app->Configuration->DarkTheme = true;
                    ZUIEndColumn(ctx);
                    ZUIEndRow(ctx);
                    end_section();
                }
                if (begin_section("Motion", m_theme_sections[1]))
                {
                    auto* row = ZUIBeginRow(ctx, "##stg_motion_row", ZFill(), ZPx(25.f));
                    row->Gap  = 12.f;
                    property_label("##stg_motion_label");
                    ZUILabel(ctx, "Reduce Motion", ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    ZUICheckbox(ctx, "##ui_reduced_motion", &ctx->Style.ReducedMotion);
                    constexpr float hint[4] = ZUI_TOKEN(Tokens::TextDisabled);
                    ZUILabelTight(ctx, "Disable UI animation for this session.", hint);
                    ZUIEndRow(ctx);
                    end_section();
                }
            }
            else if (m_settings_page == 4 && ShellPanelManager) // Layout
            {
                static const char* kPanelNames[5] = {"Outliner", "Console", "Details", "Viewport", "Profiler"};
                static const char* kRowKeys[5]    = {"##lp_r0", "##lp_r1", "##lp_r2", "##lp_r3", "##lp_r4"};
                static const char* kCbKeys[5]     = {"##lp_c0", "##lp_c1", "##lp_c2", "##lp_c3", "##lp_c4"};

                for (int ni = 0; ni < 5; ++ni)
                {
                    bool  vis  = ShellPanelManager->IsPanelVisible(kPanelNames[ni]);
                    bool  prev = vis;
                    auto* row  = ZUIBeginRow(ctx, kRowKeys[ni], ZFill(), ZPx(25.f));
                    row->Gap   = 12.f;
                    property_label("##layout_panel_label");
                    ZUILabel(ctx, kPanelNames[ni], ctx->Theme.TextDefault);
                    ZUIEndColumn(ctx);
                    ZUICheckbox(ctx, kCbKeys[ni], &vis);
                    ZUIEndRow(ctx);
                    ZUISpacer(ctx, 10.f);
                    if (vis != prev)
                        ShellPanelManager->SetPanelVisible(kPanelNames[ni], vis);
                }

                ZUISpacer(ctx, 18.f);
                ZUIBeginRow(ctx, "##lp_rst_r", ZFill(), ZPx(fh + 6.f));
                ZUISpacer(ctx, 164.f);
                ZUISignal rst_sig = ZUIButton(ctx, "Reset Layout##lp_rst");
                if (rst_sig.Flags & ZUI_SignalClicked)
                    ShellPanelManager->ResetLayout();
                ZUIEndRow(ctx);
            }

            ZUIEndScrollRegion(ctx);
            ZUIEndColumn(ctx); // content
            ZUIEndRow(ctx);    // body

            // Footer (Figma: 1 px rule, 44 px bg/surface, Reset + primary Close right-aligned)
            {
                ZUIBox* fr         = ZUIPushBox(ctx, "##stg_fsep", 10, ZUI_DrawBackground);
                fr->Size[0]        = ZFill();
                fr->Size[1]        = ZPx(1.f);
                const float sub[4] = ZUI_TOKEN(ZEngine::UI::Tokens::BorderSubtle);
                ZUIBoxSetColorArr(fr, sub);
                fr->EdgeSoftness = 0.f;
                ZUIPopBox(ctx);

                ZUIBox* ft           = ZUIBeginRow(ctx, "##stg_footer", ZFill(), ZPx(44.f));
                ft->Flags            = ft->Flags | ZUI_DrawBackground;
                const float ft_bg[4] = ZUI_TOKEN(ZEngine::UI::Tokens::BgSurface);
                ZUIBoxSetColorArr(ft, ft_bg);
                ZUIBoxSetBottomRadius(ft, Metrics::Radius6 - 1.f);
                ft->EdgeSoftness   = 0.f;
                ft->MainAlign      = ZUIAlign::End;
                ft->CrossAlign     = ZUIAlign::Center;
                ft->Padding[2]     = 14.f;
                ft->Gap            = 8.f;
                auto footer_button = [&](const char* key, bool primary) {
                    const auto sig = ZUIButton(ctx, key, ZPx(72.f), ZPx(26.f), primary ? ZUIButtonKind::Primary : ZUIButtonKind::Secondary);
                    return (sig.Flags & ZUI_SignalClicked) != 0;
                };
                const bool reset_disabled = m_settings_page == 2 || ((m_settings_page == 0 || m_settings_page == 1) && !stg_scene) || (m_settings_page == 4 && !ShellPanelManager);
                if (reset_disabled)
                    ZUIBeginDisabled(ctx);
                const bool reset = footer_button("Reset##stg_reset", false);
                if (reset_disabled)
                    ZUIEndDisabled(ctx);
                if (reset)
                {
                    if (m_settings_page == 0 && stg_scene)
                    {
                        stg_scene->Grid = decltype(stg_scene->Grid){};
                        mark_grid_dirty();
                    }
                    else if (m_settings_page == 1 && stg_scene)
                    {
                        stg_scene->Sky = {};
                        mark_sky_dirty();
                    }
                    else if (m_settings_page == 3)
                    {
                        ctx->Style.ReducedMotion = false;
                        if (stg_app && stg_app->Configuration)
                            stg_app->Configuration->DarkTheme = true;
                    }
                    else if (m_settings_page == 4 && ShellPanelManager)
                        ShellPanelManager->ResetLayout();
                }
                if (footer_button("Close##stg_closebtn", true))
                    m_settings_open = false;
                ZUIEndRow(ctx);
            }

            // Bottom-right resize handle — floated inside modal, 12×12
            {
                static constexpr float kGrip = 14.f;
                ZUIBox*                rh    = ZUIPushBox(ctx, "##stg_grip", 10, ZUI_DrawBackground | ZUI_Clickable | ZUI_FloatX | ZUI_FloatY | ZUI_NoKeyboardFocus);
                rh->Size[0]                  = ZPx(kGrip);
                rh->Size[1]                  = ZPx(kGrip);
                rh->FloatPos[0]              = kW - kGrip;
                rh->FloatPos[1]              = kH - kGrip;
                bool ghov                    = (ctx->HotKey == rh->Key);
                bool gact                    = (ctx->ActiveKey == rh->Key);
                ZUIBoxSetColor(rh, 1.f, 1.f, 1.f, gact ? 0.30f : ghov ? 0.18f : 0.07f);
                ZUIBoxSetCornerRadius(rh, 3.f);
                ZUISignalFromBox(ctx, rh); // consume hot/active state for drag
                ZUIPopBox(ctx);

                if (ctx->ActiveKey == rh->Key && ctx->MouseDown[0])
                {
                    float dx = ctx->MousePos[0] - ctx->PrevMousePos[0];
                    float dy = ctx->MousePos[1] - ctx->PrevMousePos[1];
                    kW       = fmaxf(kMinW, fminf(kW + dx, sw - m_modal_x));
                    kH       = fmaxf(kMinH, fminf(kH + dy, sh - m_modal_y));
                }
            }

            ZUIEndColumn(ctx); // window
        }

        // Drag ghost: small box following the cursor during a drag, so the
        // user gets visual feedback regardless of which panel started it.
        if (ctx->DragSourceKey != 0)
        {
            ZUIDragCursor(ctx);
        }

        ZUIEndColumn(ctx);
    }
} // namespace Tetragrama::Components
