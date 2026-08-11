#include <Tetragrama/Controllers/EditorCameraController.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/MessageToken.h>
#include <Tetragrama/Messengers/Messenger.h>
#include <ZEngine/Engine.h>
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
            WorkingSpaceBackend.Initialize(WorkingSpacePath, ZEngine::Core::VFS::VFSBackendCaps::Read | ZEngine::Core::VFS::VFSBackendCaps::List, &Memory->MainArena);
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

    const char* Editor::GetActiveEnvironmentMapPath()
    {
        if (!Configuration || Configuration->ActiveEnvironmentMapPath.empty())
            return nullptr;
        return Configuration->ActiveEnvironmentMapPath.c_str();
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
        editor_cam_controller->Initialize(&Memory->MainArena, CurrentWindow);
        editor_scene->Initialize(&Memory->MainArena, Configuration->ActiveSceneName.c_str());

        CameraController = editor_cam_controller;
        CurrentScene     = editor_scene;
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
            expand_nested(config, "defaultImportDir", "textureDir");
            expand_nested(config, "defaultImportDir", "soundDir");
            expand(config, "sceneDir");
            expand(config, "sceneDataDir");
            if (config.contains("environmentMapDir"))
                expand(config, "environmentMapDir");
            config["workingSpace"] = root_project_dir;
        }

        ProjectName.init(arena, config["projectName"].get<std::string>().c_str());
        WorkingSpacePath.init(arena, config["workingSpace"].get<std::string>().c_str());
        DefaultImportTexturePath.init(arena, config["defaultImportDir"]["textureDir"].get<std::string>().c_str());
        DefaultImportSoundPath.init(arena, config["defaultImportDir"]["soundDir"].get<std::string>().c_str());
        ScenePath.init(arena, config["sceneDir"].get<std::string>().c_str());
        SceneDataPath.init(arena, config["sceneDataDir"].get<std::string>().c_str());

        // Environment map import directory (where .zenvmap files are written)
        if (config.contains("environmentMapDir"))
        {
            EnvironmentMapImportPath.init(arena, config["environmentMapDir"].get<std::string>().c_str());
        }
        else
        {
            // Default: <project>/Assets/EnvironmentMaps
            auto default_env_dir = fmt::format("{}/Assets/EnvironmentMaps", config["workingSpace"].get<std::string>());
            EnvironmentMapImportPath.init(arena, default_env_dir.c_str());
        }

        // Active environment map: optional — read from "sky.environmentMap" (filename only)
        // Resolved against EnvironmentMapImportPath at runtime.
        std::string env_map_file;
        if (config.contains("sky") && config["sky"].contains("environmentMap"))
            env_map_file = config["sky"]["environmentMap"].get<std::string>();
        else if (config.contains("environmentMap"))
            env_map_file = config["environmentMap"].get<std::string>(); // legacy fallback

        if (!env_map_file.empty())
        {
            auto abs_path = fmt::format("{}/{}", EnvironmentMapImportPath.c_str(), env_map_file);
            ActiveEnvironmentMapPath.init(arena, abs_path.c_str());
        }

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
