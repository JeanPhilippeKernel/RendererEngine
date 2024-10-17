#include <pch.h>
#include "Editor.h"
#include "nlohmann/json.hpp"

#ifdef ZENGINE_PLATFORM

namespace fs = std::filesystem;

static void ConfigureWorkingSpace(nlohmann::json& config, std::string_view ws_path)
{
    std::string working_space_path = config["workingSpace"];
    if (working_space_path == ".")
    {
        std::string_view lookup_key("$(workingSpace)");
        size_t           length = lookup_key.size();

        auto& texture_path    = config["defaultImportDir"]["textureDir"];
        auto& sound_path      = config["defaultImportDir"]["soundDir"];
        auto& scene_path      = config["sceneDir"];
        auto& scene_data_path = config["sceneDataDir"];

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

        config["workingSpace"] = ws_path;
    }
}

static void ConfigureDefaultDirectories(const nlohmann::json& config, std::string_view ws_path)
{
    std::string texture_path    = fs::path(ws_path).append((config["defaultImportDir"]["textureDir"]).get<std::string>()).string();
    std::string sound_path      = fs::path(ws_path).append((config["defaultImportDir"]["soundDir"]).get<std::string>()).string();
    std::string scene_path      = fs::path(ws_path).append(config["sceneDir"].get<std::string>()).string();
    std::string scene_data_path = fs::path(ws_path).append(config["sceneDataDir"].get<std::string>()).string();

    std::error_code err;
    if (!fs::exists(texture_path))
    {
        fs::create_directories(texture_path, err);
    }

    if (!fs::exists(sound_path))
    {
        fs::create_directories(sound_path, err);
    }

    if (!fs::exists(scene_path))
    {
        fs::create_directories(scene_path, err);
    }

    if (!fs::exists(scene_data_path))
    {
        fs::create_directories(scene_data_path, err);
    }
}

int applicationEntryPoint(int argc, char* argv[])
{
    std::string_view project_config_json;

    if (argc == 1)
    {
        return -2; // Missing arguments
    }
    else if (argc > 1)
    {
        std::string_view config_file_arg = "--projectConfigFile";
        for (int i = 1; i < argc; ++i)
        {
            if (config_file_arg == std::string_view(argv[i]))
            {
                project_config_json = argv[i + 1];
            }
        }
    }

    try
    {
        if (project_config_json.empty())
        {
            return -2;
        }

        std::ifstream  f(project_config_json.data());
        nlohmann::json config           = nlohmann::json::parse(f);
        std::string    root_project_dir = std::filesystem::path(project_config_json).parent_path().string();
        ConfigureWorkingSpace(config, root_project_dir);
        ConfigureDefaultDirectories(config, root_project_dir);

        Tetragrama::EditorConfiguration editor_config = {};
        editor_config.ProjectName                     = config["projectName"];
        editor_config.WorkingSpacePath                = config["workingSpace"];
        editor_config.DefaultImportTexturePath        = config["defaultImportDir"]["textureDir"];
        editor_config.DefaultImportSoundPath          = config["defaultImportDir"]["soundDir"];
        editor_config.ScenePath                       = config["sceneDir"];
        editor_config.SceneDataPath                   = config["sceneDataDir"];

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
            editor_config.ActiveSceneName = scene["name"];
            break;
        }

        auto editor = ZEngine::CreateRef<Tetragrama::Editor>(editor_config);
        editor->Initialize();
        editor->Run();
    }
    catch (const std::exception& e)
    {
    }
    return 0;
}

#ifdef _WIN32
#include <windows.h>

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
{
    return applicationEntryPoint(__argc, __argv);
}

#else
int main(int argc, char* argv[])
{
    return applicationEntryPoint(argc, argv);
}
#endif
#endif
