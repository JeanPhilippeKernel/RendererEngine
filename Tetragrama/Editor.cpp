#include <Tetragrama/Controllers/EditorCameraController.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/MessageToken.h>
#include <Tetragrama/Messengers/Messenger.h>
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
            Configuration->ActiveSceneName.clear();
            Configuration->ActiveSceneName.append(active_scene);
        }
        WorkingSpacePath = Configuration->WorkingSpacePath.c_str();
    }

    void Editor::OverrideWindowConfiguration()
    {
        std::string title     = fmt::format("{0} - Active Scene : {1}", Configuration->ProjectName.c_str(), Configuration->ActiveSceneName.c_str());
        WindowCfg.EnableVsync = true;
        WindowCfg.Title.init(&Memory->MainArena, title.c_str());
    }

    void Editor::OnInitialized()
    {
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
        nlohmann::json config             = nlohmann::json::parse(f);
        std::string    root_project_dir   = std::filesystem::path(file).parent_path().string();

        std::string    working_space_path = config["workingSpace"];
        if (working_space_path == ".")
        {
            std::string_view lookup_key("$(workingSpace)");
            size_t           length          = lookup_key.size();

            auto&            texture_path    = config["defaultImportDir"]["textureDir"];
            auto&            sound_path      = config["defaultImportDir"]["soundDir"];
            auto&            scene_path      = config["sceneDir"];
            auto&            scene_data_path = config["sceneDataDir"];

            if (texture_path.get<std::string>().find(lookup_key) != std::string::npos)
            {
                config["defaultImportDir"]["textureDir"] = texture_path.get<std::string>().replace(texture_path.get<std::string>().find(lookup_key), length, "");
            }

            if (sound_path.get<std::string>().find(lookup_key) != std::string::npos)
            {
                config["defaultImportDir"]["soundDir"] = sound_path.get<std::string>().replace(sound_path.get<std::string>().find(lookup_key), length, "");
            }

            if (scene_path.get<std::string>().find(lookup_key) != std::string::npos)
            {
                config["sceneDir"] = scene_path.get<std::string>().replace(scene_path.get<std::string>().find(lookup_key), length, "");
            }

            if (scene_data_path.get<std::string>().find(lookup_key) != std::string::npos)
            {
                config["sceneDataDir"] = scene_data_path.get<std::string>().replace(scene_data_path.get<std::string>().find(lookup_key), length, "");
            }

            config["workingSpace"] = root_project_dir;
        }

        ProjectName.init(arena, config["projectName"].get<std::string>().c_str());
        WorkingSpacePath.init(arena, config["workingSpace"].get<std::string>().c_str());
        DefaultImportTexturePath.init(arena, config["defaultImportDir"]["textureDir"].get<std::string>().c_str());
        DefaultImportSoundPath.init(arena, config["defaultImportDir"]["soundDir"].get<std::string>().c_str());
        ScenePath.init(arena, config["sceneDir"].get<std::string>().c_str());
        SceneDataPath.init(arena, config["sceneDataDir"].get<std::string>().c_str());

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
