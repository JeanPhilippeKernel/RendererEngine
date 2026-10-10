// New panel-manager-based UI (replaces old per-component system)
#include <GLFW/glfw3.h>
#include <Tetragrama/Components/ZUI/ZUIDockspaceComponent.h>
#include <Tetragrama/Components/ZUI/ZUIStatusBarComponent.h>
#include <Tetragrama/Controllers/EditorCameraController.h>
#include <Tetragrama/Editor.h>
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
        // Reserve the editor owner before creating configuration and workspace
        // state. EditorScene will later carve its 200 MiB local arena from here.
        Memory->CreateBudgetedArena(Memory->Budget.EditorContext, &EditorArena);
        Configuration = ZPushStructCtor(&EditorArena, EditorConfiguration);

        if (ZEngine::Helpers::secure_strlen(ConfigFile))
        {
            Configuration->ReadConfig(&EditorArena, ConfigFile);
        }

        if (Configuration->ActiveSceneName.empty())
        {
            ZENGINE_CORE_WARN("Editor Scene name is empty")

            cstring active_scene = "<empty scene>";
            Configuration->ActiveSceneName.init(&EditorArena, active_scene);
        }
        WorkingSpacePath = Configuration->WorkingSpacePath.c_str();
        if (WorkingSpacePath && WorkingSpacePath[0] != '\0')
        {
            WorkingSpaceBackend.Initialize(WorkingSpacePath, ZEngine::Core::VFS::VFSBackendCaps::Read | ZEngine::Core::VFS::VFSBackendCaps::Write | ZEngine::Core::VFS::VFSBackendCaps::List, &EditorArena);
            VFSBackend = &WorkingSpaceBackend;
        }
    }

    void Editor::OverrideWindowConfiguration()
    {
        const char* project_name = Configuration->ProjectName.empty() ? "ZEngine Editor" : Configuration->ProjectName.c_str();
        const char* scene_name   = Configuration->ActiveSceneName.empty() ? "<empty scene>" : Configuration->ActiveSceneName.c_str();
        std::string title        = fmt::format("{0} - Active Scene : {1}", project_name, scene_name);
        WindowCfg.EnableVsync    = true;
        WindowCfg.Title.init(&EditorArena, title.c_str());
    }

    void Editor::OnInitialized()
    {
        auto editor_scene          = ZPushStructCtor(&EditorArena, EditorScene);
        auto editor_cam_controller = ZPushStructCtor(&EditorArena, Controllers::EditorCameraController);
        ZUIUILayer                 = ZPushStructCtor(&EditorArena, ZUILayer);

        ZUIUILayer->Initialize(&EditorArena, this);

        // Single panel-manager component replaces all old per-panel components.
        // It owns the dock tree, tab bars, and all panel views.
        auto* pm = ZPushStructCtor(&EditorArena, Tetragrama::Panels::ZUIPanelManagerComponent);
        pm->Initialize(ZUIUILayer, "PanelManager");
        ZUIUILayer->AddComponent(pm);

        // Editor shell: menu bar + floating overlays (settings, etc.)
        // Registered after PanelManager so it renders on top.
        auto* shell = ZPushStructCtor(&EditorArena, Tetragrama::Components::ZUIDockspaceComponent);
        shell->Initialize(ZUIUILayer, "EditorShell");
        shell->ShellPanelManager = &pm->Manager;
        ZUIUILayer->AddComponent(shell);

        auto* sbar              = ZPushStructCtor(&EditorArena, Tetragrama::Components::ZUIStatusBarComponent);
        sbar->ShellPanelManager = &pm->Manager;
        sbar->Initialize(ZUIUILayer, "StatusBar");
        ZUIUILayer->AddComponent(sbar);
        editor_cam_controller->Initialize(&EditorArena, CurrentWindow, ZEngine::Engine::GetContext()->InputManager, this);
        editor_scene->Initialize(&EditorArena, Configuration->ActiveSceneName.c_str(), Configuration->DefaultSky);

        CameraController = editor_cam_controller;
        CurrentScene     = editor_scene;

        // Bake ZUI font atlases. Font sizes are chosen to be readable on high-resolution
        // displays where glfwGetWindowSize returns physical pixel counts (~3024px wide).
        // ImGui approach: fonts are baked at physical pixel density and all draw
        // coordinates are also in physical pixels — the NDC transform handles the rest.
        if (RenderPipeline && RenderPipeline->ZUICtx && RenderPipeline->ZUIRenderPass)
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

            auto scratch = ZGetScratch(&EditorArena);
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

        OnEvent(e);
    }

    void Editor::OnUpdate(float dt)
    {
        if (CameraController)
        {
            ZEngine::UI::ZUIInputCapture capture = {};
            if (RenderPipeline && RenderPipeline->ZUICtx)
                capture = ZEngine::UI::ZUIGetInputCapture(RenderPipeline->ZUICtx);
            CameraController->SetInputCapture(capture.Pointer, capture.Keyboard);
        }

        if (ZUIUILayer)
            ZUIUILayer->Update(dt);
    }

    void Editor::OnEvent(Core::CoreEvent& /*e*/)
    {
        // Window and UI events are handled in ProcessEvent. Camera state is sampled
        // from InputManager during the application update.
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

    void Editor::SaveScene()
    {
        ZENGINE_CORE_INFO("[Editor] Scene save not yet implemented — pending scene serializer rebuild, see #713-#719")
    }

    void Editor::SaveSceneAs()
    {
        ZENGINE_CORE_INFO("[Editor] Scene save not yet implemented — pending scene serializer rebuild, see #713-#719")
    }

    void Editor::OpenScene(const char* /*path*/)
    {
        ZENGINE_CORE_INFO("[Editor] Scene load not yet implemented — pending scene serializer rebuild, see #713-#719")
    }

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

        // Project sky data is a template for newly created scenes. It never
        // replaces the SkyConfig serialized by an existing scene.
        const nlohmann::json* sky_defaults = nullptr;
        if (config.contains("skyDefaults") && config["skyDefaults"].is_object())
            sky_defaults = &config["skyDefaults"];
        else if (config.contains("sky") && config["sky"].is_object())
            sky_defaults = &config["sky"]; // compatibility with the initial project template

        if (sky_defaults)
        {
            const auto& sky = *sky_defaults;
            if (sky.contains("mode") && sky["mode"].is_string())
            {
                const std::string mode = sky["mode"].get<std::string>();
                if (mode == "hdri")
                    DefaultSky.Mode = ZEngine::Rendering::Scenes::SkyMode::HDRI;
                else if (mode == "skySphere")
                    DefaultSky.Mode = ZEngine::Rendering::Scenes::SkyMode::SkySphere;
            }
            if (sky.contains("environmentMap") && sky["environmentMap"].is_string())
            {
                auto environment_uuid = uuids::uuid::from_string(sky["environmentMap"].get<std::string>());
                if (environment_uuid.has_value())
                    DefaultSky.EnvironmentMap = environment_uuid.value();
            }
            if (sky.contains("environmentIntensity") && sky["environmentIntensity"].is_number())
                DefaultSky.EnvironmentIntensity = sky["environmentIntensity"].get<float>();
            if (sky.contains("environmentYawRadians") && sky["environmentYawRadians"].is_number())
                DefaultSky.EnvironmentYawRadians = sky["environmentYawRadians"].get<float>();
            if (sky.contains("environmentTint") && sky["environmentTint"].is_array() && sky["environmentTint"].size() == 4)
            {
                bool valid_tint = true;
                for (uint32_t i = 0; i < 4; ++i)
                {
                    if (!sky["environmentTint"][i].is_number())
                    {
                        valid_tint = false;
                        break;
                    }
                }
                if (valid_tint)
                    for (uint32_t i = 0; i < 4; ++i)
                        DefaultSky.EnvironmentTint[i] = sky["environmentTint"][i].get<float>();
            }
        }
        DefaultSky.Sanitize();

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
