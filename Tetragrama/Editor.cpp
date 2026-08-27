// New panel-manager-based UI (replaces old per-component system)
#include <GLFW/glfw3.h>
#include <Tetragrama/Controllers/EditorCameraController.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/MessageToken.h>
#include <Tetragrama/Messengers/Messenger.h>
#include <Tetragrama/Panels/ZUIPanelManagerComponent.h>
#include <ZEngine/Core/CoreEvent.h>
#include <ZEngine/Core/VFS/Registry/AssetRecord.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIFont.h>
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

        // Single panel-manager component replaces all old per-panel components.
        // It owns the dock tree, tab bars, and all panel views.
        auto* pm = ZPushStructCtor(&Memory->MainArena, Tetragrama::Panels::ZUIPanelManagerComponent);
        pm->Initialize(ZUIUILayer, "PanelManager");
        ZUIUILayer->AddComponent(pm);
        editor_cam_controller->Initialize(&Memory->MainArena, CurrentWindow, ZEngine::Engine::GetContext()->InputManager, this);
        editor_scene->Initialize(&Memory->MainArena, Configuration->ActiveSceneName.c_str());

        CameraController = editor_cam_controller;
        CurrentScene     = editor_scene;

        // Bake ZUI font atlases. Font sizes are chosen to be readable on high-resolution
        // displays where glfwGetWindowSize returns physical pixel counts (~3024px wide).
        // ImGui approach: fonts are baked at physical pixel density and all draw
        // coordinates are also in physical pixels — the NDC transform handles the rest.
        if (RenderPipeline && RenderPipeline->ZUICtx && RenderPipeline->ZUIRenderer)
        {
            constexpr const char* kFontPath       = "/ZodiacEngine/Settings/Fonts/OpenSans/OpenSans-Regular.ttf";
            constexpr const char* kHeaderFontPath = "/ZodiacEngine/Settings/Fonts/OpenSans/OpenSans-SemiBold.ttf";
            auto*                 ctx             = RenderPipeline->ZUICtx;

            // Font atlas is always baked at 2× the logical base size so that
            // every display gets a 2× oversampled atlas — the same sharpness
            // advantage Retina screens had before, now available everywhere.
            //
            // kBase  = logical display size (matches ZUIStyle.FontSize)
            // kBake  = atlas physical size  (kBase * kOversample)
            // FontScale = 1/kOversample     (maps atlas px → logical px)
            //
            // UIScale (fb/win ratio) is set each frame by BeginOverlayFrame and
            // handles the physical pixel density independently of the font atlas.
            constexpr float       kBase           = 13.f;                // logical body size in px
            constexpr float       kOversample     = 2.f;                 // always 2× — sharp on every display
            const float           kBake           = kBase * kOversample; // 26 px atlas
            const float           kSmall          = kBase * 0.80f * kOversample;
            const float           kHeader         = kBase * 1.30f * kOversample;
            const float           kFontScale      = 1.f / kOversample; // 0.5

            ZENGINE_CORE_INFO("[ZUI] FontBake body={:.0f} small={:.0f} header={:.0f}  FontScale={:.2f}", kBake, kSmall, kHeader, kFontScale);

            auto scratch = ZGetScratch(&Memory->MainArena);
            ctx->Atlas   = ZEngine::UI::ZUIFontAtlasBake(&ctx->PersistentArena, scratch.Arena, RenderPipeline->Device, kFontPath, kSmall, kBake, kHeader, 32, 96, kHeaderFontPath);
            ZReleaseScratch(scratch);

            if (ctx->Atlas)
            {
                if (ctx->Atlas->Small)
                    ctx->Atlas->Small->FontScale = kFontScale;
                if (ctx->Atlas->Body)
                    ctx->Atlas->Body->FontScale = kFontScale;
                if (ctx->Atlas->Header)
                    ctx->Atlas->Header->FontScale = kFontScale;

                // Style.FontSize = logical body size → FrameHeight = 13 + 3*2 = 19 px
                ctx->Style.FontSize = kBase;
                ZUIStyleUpdate(&ctx->Style);
                ZENGINE_CORE_INFO("[ZUI] Style.FontSize={:.0f}  FrameHeight={:.0f}", ctx->Style.FontSize, ctx->Style.FrameHeight);
            }
        }

        // Scene instance creation is handled directly in SceneViewportUIComponent::OnDrop
        // via ImportCoordinator::Enqueue's returned UUID — no callback needed here.
    }

    void Editor::ProcessEvent(ZEngine::Core::CoreEvent& e)
    {
        // Always route events to the window and ZUI layer
        if (CurrentWindow)
        {
            CurrentWindow->OnEvent(e);
        }
        if (ZUIUILayer)
        {
            ZUIUILayer->OnEvent(e);
        }

        // Gate camera-controller mouse routing on viewport focus (Gap 3)
        bool is_mouse_event  = (e.GetType() == ZEngine::Core::EventType::MouseButtonPressed || e.GetType() == ZEngine::Core::EventType::MouseButtonReleased || e.GetType() == ZEngine::Core::EventType::MouseMoved || e.GetType() == ZEngine::Core::EventType::MouseWheel);

        bool viewport_active = RenderPipeline && RenderPipeline->ZUICtx && RenderPipeline->ZUICtx->ViewportHovered;

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
        if (ZUIUILayer)
        {
            ZUIUILayer->Render(nullptr, nullptr);
        }
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
