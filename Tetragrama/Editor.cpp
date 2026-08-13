#include <Tetragrama/Controllers/EditorCameraController.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/MessageToken.h>
#include <Tetragrama/Messengers/Messenger.h>
#include <ZEngine/Core/VFS/Registry/AssetRecord.h>
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
        if (WorkingSpacePath && WorkingSpacePath[0] != '\0')
        {
            if (ZEngine::Engine::GetContext()->VFS->Mount(&WorkingSpaceBackend, ZEngine::Core::VFS::VFSPath::Root(), 0).Failed())
            {
                ZENGINE_CORE_ERROR("Failed to mount working space '{}' into the VFS", WorkingSpacePath)
            }
        }

        auto editor_scene          = ZPushStructCtor(&Memory->MainArena, EditorScene);
        auto editor_cam_controller = ZPushStructCtor(&Memory->MainArena, Controllers::EditorCameraController);
        UILayer                    = ZPushStructCtor(&Memory->MainArena, ImguiLayer);

        UILayer->Initialize(&Memory->MainArena, this);
        editor_cam_controller->Initialize(&Memory->MainArena, CurrentWindow, ZEngine::Engine::GetContext()->InputManager);
        editor_scene->Initialize(&Memory->MainArena, Configuration->ActiveSceneName.c_str());

        CameraController = editor_cam_controller;
        CurrentScene     = editor_scene;

        // Scene instance creation is handled directly in SceneViewportUIComponent::OnDrop
        // via ImportCoordinator::Enqueue's returned UUID — no callback needed here.
    }

    void Editor::OnUpdate(float dt)
    {
        CHECK_AND_ESCAPE_NULL(UILayer)

        UILayer->Update(dt);
    }

    void Editor::OnEvent(Core::CoreEvent& e)
    {
        CHECK_AND_ESCAPE_NULL(UILayer)

        UILayer->OnEvent(e);
    }

    void Editor::OnPreRender() {}

    void Editor::OnPostRender() {}

    void Editor::OnRenderUI()
    {
        UILayer->Render(nullptr, nullptr);
    }

    void Editor::OnClosing() {}

    void Editor::OnClosed() {}

    void EditorConfiguration::ReadConfig(ZEngine::Core::Memory::ArenaAllocator* arena, const char* file)
    {
        std::ifstream  f(file);
        nlohmann::json config           = nlohmann::json::parse(f);
        std::string    root_project_dir = std::filesystem::path(file).parent_path().string();

        // Helper: expand $(workingSpace) token in a json string field
        auto           expand           = [&](nlohmann::json& node, std::string_view key) {
            std::string_view lookup("$(workingSpace)");
            auto             s = node[key].get<std::string>();
            auto             p = s.find(lookup);
            if (p != std::string::npos)
                node[key] = s.replace(p, lookup.size(), "");
        };
        auto expand_nested = [&](nlohmann::json& node, std::string_view section, std::string_view key) {
            std::string_view lookup("$(workingSpace)");
            auto             s = node[section][key].get<std::string>();
            auto             p = s.find(lookup);
            if (p != std::string::npos)
                node[section][key] = s.replace(p, lookup.size(), "");
        };

        std::string working_space_path = config["workingSpace"];
        if (working_space_path == ".")
        {
            expand(config, "sceneDir");
            expand(config, "sceneDataDir");
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
        SceneDataPath.init(arena, config["sceneDataDir"].get<std::string>().c_str());

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
