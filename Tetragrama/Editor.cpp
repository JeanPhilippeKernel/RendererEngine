#include <Tetragrama/Components/ZUI/ZUIDockspaceComponent.h>
#include <Tetragrama/Components/ZUI/ZUIHierarchyViewComponent.h>
#include <Tetragrama/Components/ZUI/ZUIInspectorViewComponent.h>
#include <Tetragrama/Components/ZUI/ZUILogComponent.h>
#include <Tetragrama/Components/ZUI/ZUIProjectViewComponent.h>
#include <Tetragrama/Components/ZUI/ZUISceneViewportComponent.h>
#include <Tetragrama/Components/ZUI/ZUIStatusBarComponent.h>
#include <Tetragrama/Controllers/EditorCameraController.h>
#include <Tetragrama/Editor.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIFont.h>
#include <Tetragrama/MessageToken.h>
#include <Tetragrama/Messengers/Messenger.h>
#include <ZEngine/Core/VFS/Registry/AssetRecord.h>
#include <ZEngine/Core/CoreEvent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Managers/AssetManager.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <fstream>

using namespace ZEngine;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Helpers;
using namespace Tetragrama::Layers;

namespace Tetragrama
{
    void Editor::OnInitializing()
    {
        Configuration = ZPushStructCtor(&Memory->MainArena, EditorConfiguration);

        if (ZEngine::Helpers::secure_strlen(ConfigFile))
        {
            Configuration->ReadConfig(&Memory->MainArena, ConfigFile);
        }

        if (Configuration->ActiveSceneName.empty())
        {
            ZENGINE_CORE_WARN("Editor Scene name is empty")

            cstring active_scene = "<empty scene>";
            Configuration->ActiveSceneName.init(&Memory->MainArena, active_scene);
        }
        WorkingSpacePath = Configuration->WorkingSpacePath.c_str();
        if (WorkingSpacePath && WorkingSpacePath[0] != '\0')
        {
            WorkingSpaceBackend.Initialize(WorkingSpacePath, ZEngine::Core::VFS::VFSBackendCaps::Read | ZEngine::Core::VFS::VFSBackendCaps::Write | ZEngine::Core::VFS::VFSBackendCaps::List, &Memory->MainArena);
            VFSBackend = &WorkingSpaceBackend;
        }
    }

    void Editor::OverrideWindowConfiguration()
    {
        const char* project_name = Configuration->ProjectName.empty() ? "ZEngine Editor" : Configuration->ProjectName.c_str();
        const char* scene_name   = Configuration->ActiveSceneName.empty() ? "<empty scene>" : Configuration->ActiveSceneName.c_str();
        std::string title        = fmt::format("{0} - Active Scene : {1}", project_name, scene_name);
        WindowCfg.EnableVsync    = true;
        WindowCfg.Title.init(&Memory->MainArena, title.c_str());
    }

    void Editor::OnInitialized()
    {
        auto editor_scene          = ZPushStructCtor(&Memory->MainArena, EditorScene);
        auto editor_cam_controller = ZPushStructCtor(&Memory->MainArena, Controllers::EditorCameraController);
        ZUIUILayer                 = ZPushStructCtor(&Memory->MainArena, ZUILayer);

        ZUIUILayer->Initialize(&Memory->MainArena, this);

        // Create all ZUI panels
        auto* zui_log  = ZPushStructCtor(&Memory->MainArena, Components::ZUILogComponent);
        auto* zui_hier = ZPushStructCtor(&Memory->MainArena, Components::ZUIHierarchyViewComponent);
        auto* zui_insp = ZPushStructCtor(&Memory->MainArena, Components::ZUIInspectorViewComponent);
        auto* zui_vp   = ZPushStructCtor(&Memory->MainArena, Components::ZUISceneViewportComponent);
        auto* zui_proj = ZPushStructCtor(&Memory->MainArena, Components::ZUIProjectViewComponent);
        auto* zui_stat = ZPushStructCtor(&Memory->MainArena, Components::ZUIStatusBarComponent);
        auto* zui_dock = ZPushStructCtor(&Memory->MainArena, Components::ZUIDockspaceComponent);

        zui_log->Initialize(ZUIUILayer,  "Console");
        zui_hier->Initialize(ZUIUILayer, "Hierarchy");
        zui_insp->Initialize(ZUIUILayer, "Inspector");
        zui_vp->Initialize(ZUIUILayer,   "Scene");
        zui_proj->Initialize(ZUIUILayer, "Project");
        zui_stat->Initialize(ZUIUILayer, "StatusBar");
        zui_dock->Initialize(ZUIUILayer, "Dockspace");

        // Wire panel pointers into dockspace for region assignment
        zui_dock->Hierarchy  = zui_hier;
        zui_dock->Inspector  = zui_insp;
        zui_dock->Viewport   = zui_vp;
        zui_dock->Log        = zui_log;
        zui_dock->Project    = zui_proj;
        zui_dock->StatusBar  = zui_stat;

        // Register: dockspace FIRST (sets regions), then panels
        ZUIUILayer->AddComponent(zui_dock);
        ZUIUILayer->AddComponent(zui_vp);
        ZUIUILayer->AddComponent(zui_hier);
        ZUIUILayer->AddComponent(zui_insp);
        ZUIUILayer->AddComponent(zui_log);
        ZUIUILayer->AddComponent(zui_proj);
        ZUIUILayer->AddComponent(zui_stat);
        editor_cam_controller->Initialize(&Memory->MainArena, CurrentWindow, ZEngine::Engine::GetContext()->InputManager, this);
        editor_scene->Initialize(&Memory->MainArena, Configuration->ActiveSceneName.c_str());

        CameraController = editor_cam_controller;
        CurrentScene     = editor_scene;

        // Bake ZUI font atlases — Small (18 px), Body (28 px), Header (36 px)
        if (RenderPipeline && RenderPipeline->ZUICtx && RenderPipeline->ZUIRenderer)
        {
            constexpr const char* kFontPath =
                "/ZodiacEngine/Settings/Fonts/OpenSans/OpenSans-Regular.ttf";
            auto* ctx = RenderPipeline->ZUICtx;

            auto scratch = ZGetScratch(&Memory->MainArena);

            ctx->FontSmall = ZEngine::UI::ZUIFontBake(
                &ctx->PersistentArena, scratch.Arena, RenderPipeline->Device,
                kFontPath, 18.f, 512, 512, 32, 96);

            ctx->Font = ZEngine::UI::ZUIFontBake(
                &ctx->PersistentArena, scratch.Arena, RenderPipeline->Device,
                kFontPath, 28.f, 1024, 1024, 32, 96);

            ctx->FontHeader = ZEngine::UI::ZUIFontBake(
                &ctx->PersistentArena, scratch.Arena, RenderPipeline->Device,
                kFontPath, 36.f, 1024, 1024, 32, 96);

            ZReleaseScratch(scratch);
        }

        // Scene instance creation is handled directly in SceneViewportUIComponent::OnDrop
        // via ImportCoordinator::Enqueue's returned UUID — no callback needed here.
    }

    void Editor::ProcessEvent(ZEngine::Core::CoreEvent& e)
    {
        // Always route events to the window and ZUI layer
        if (CurrentWindow) { CurrentWindow->OnEvent(e); }
        if (ZUIUILayer)    { ZUIUILayer->OnEvent(e); }

        // Gate camera-controller mouse routing on viewport focus (Gap 3)
        bool is_mouse_event = (e.GetType() == ZEngine::Core::EventType::MouseButtonPressed  ||
                               e.GetType() == ZEngine::Core::EventType::MouseButtonReleased ||
                               e.GetType() == ZEngine::Core::EventType::MouseMoved          ||
                               e.GetType() == ZEngine::Core::EventType::MouseWheel);

        bool viewport_active = RenderPipeline && RenderPipeline->ZUICtx &&
                               RenderPipeline->ZUICtx->ViewportHovered;

        if (CameraController && (!is_mouse_event || viewport_active))
        {
            CameraController->OnEvent(e);
        }

        OnEvent(e);
    }

    void Editor::OnUpdate(float dt)
    {
        CHECK_AND_ESCAPE_NULL(ZUIUILayer)
        ZUIUILayer->Update(dt);
    }

    void Editor::OnEvent(Core::CoreEvent& /*e*/)
    {
        // Event routing is handled in ProcessEvent (which also gates camera input
        // on viewport focus). Nothing extra needed here.
    }

    void Editor::OnPreRender() {}

    void Editor::OnPostRender() {}

    void Editor::OnRenderUI()
    {
        if (ZUIUILayer) { ZUIUILayer->Render(nullptr, nullptr); }
    }

    void Editor::OnClosing() {}

    void Editor::OnClosed() {}

    void EditorConfiguration::ReadConfig(ZEngine::Core::Memory::ArenaAllocator* arena, const char* file)
    {
        std::ifstream  f(file);
        nlohmann::json config              = nlohmann::json::parse(f);
        std::string    root_project_dir    = std::filesystem::path(file).parent_path().string();

        // Helper: expand $(workingSpace) token in a json string field.
        // Result is a workspace-relative sub-path, e.g. "Assets/Meshes" (no leading slash).
        auto           strip_leading_slash = [](std::string& s) {
            if (!s.empty() && s[0] == '/')
                s.erase(0, 1);
        };
        auto expand = [&](nlohmann::json& node, std::string_view key) {
            std::string_view lookup("$(workingSpace)");
            auto             s = node[key].get<std::string>();
            auto             p = s.find(lookup);
            if (p != std::string::npos)
            {
                s = s.replace(p, lookup.size(), "");
                strip_leading_slash(s);
                node[key] = s;
            }
        };
        auto expand_nested = [&](nlohmann::json& node, std::string_view section, std::string_view key) {
            std::string_view lookup("$(workingSpace)");
            auto             s = node[section][key].get<std::string>();
            auto             p = s.find(lookup);
            if (p != std::string::npos)
            {
                s = s.replace(p, lookup.size(), "");
                strip_leading_slash(s);
                node[section][key] = s;
            }
        };

        std::string working_space_path = config["workingSpace"];
        if (working_space_path == ".")
        {
            expand(config, "sceneDir");
            // Expand all fields inside assetDirs (new format)
            if (config.contains("assetDirs"))
            {
                for (auto& [key, val] : config["assetDirs"].items())
                    expand_nested(config, "assetDirs", key);
            }
            // Backward compat: old defaultImportDir format
            if (config.contains("defaultImportDir"))
            {
                expand_nested(config, "defaultImportDir", "textureDir");
                expand_nested(config, "defaultImportDir", "soundDir");
            }
            config["workingSpace"] = root_project_dir;
        }

        auto ws = config["workingSpace"].get<std::string>();
        ProjectName.init(arena, config["projectName"].get<std::string>().c_str());
        WorkingSpacePath.init(arena, ws.c_str());
        ScenePath.init(arena, config["sceneDir"].get<std::string>().c_str());

        // Asset directories — new assetDirs format, fall back to defaultImportDir
        auto asset_path = [&](const char* asset_key, const char* legacy_section, const char* legacy_key, const char* default_suffix) -> std::string {
            if (config.contains("assetDirs") && config["assetDirs"].contains(asset_key))
                return config["assetDirs"][asset_key].get<std::string>();
            if (legacy_section && config.contains(legacy_section) && config[legacy_section].contains(legacy_key))
                return config[legacy_section][legacy_key].get<std::string>();
            return fmt::format("{}{}", ws, default_suffix);
        };

        TexturePath.init(arena, asset_path("textureDir", "defaultImportDir", "textureDir", "/Assets/Textures").c_str());
        SoundPath.init(arena, asset_path("soundDir", "defaultImportDir", "soundDir", "/Assets/Sounds").c_str());
        MeshPath.init(arena, asset_path("meshDir", nullptr, nullptr, "/Assets/Meshes").c_str());
        MaterialPath.init(arena, asset_path("materialDir", nullptr, nullptr, "/Assets/Materials").c_str());
        SpritePath.init(arena, asset_path("spriteDir", nullptr, nullptr, "/Assets/Sprites").c_str());
        EnvironmentMapImportPath.init(arena, asset_path("environmentMapDir", nullptr, nullptr, "/Assets/EnvironmentMaps").c_str());

        /*
         * Retreiving the Active Scene
         */
        for (const auto& scene : config["sceneList"])
        {
            bool is_default = scene["isDefault"].get<bool>();
            if (!is_default)
            {
                continue;
            }
            ActiveSceneName.init(arena, scene["name"].get<std::string>().c_str());
            break;
        }
    }
} // namespace Tetragrama
