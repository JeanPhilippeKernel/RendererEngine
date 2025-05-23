#include <pch.h>
#include <Editor.h>
#include <MessageToken.h>
#include <Messengers/Messenger.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

using namespace ZEngine;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Helpers;
using namespace Tetragrama::Layers;
using namespace Tetragrama::Messengers;
using namespace Tetragrama::Controllers;
using namespace Tetragrama::Managers;

namespace Tetragrama
{
    void Editor::Initialize(ArenaAllocator* arena, const char* file)
    {
        AssetManager::Initialize(arena);

        Context                      = ZPushStruct(arena, EditorContext);
        Context->Arena               = arena;

        Context->AssetManagerPtr     = AssetManager::Instance();
        Context->ConfigurationPtr    = ZPushStructCtor(Context->Arena, EditorConfiguration);
        Context->CameraControllerPtr = ZPushStructCtor(Context->Arena, EditorCameraController);
        UILayer                      = ZPushStructCtor(Context->Arena, ImguiLayer);
        CanvasLayer                  = ZPushStructCtor(Context->Arena, RenderLayer);

        Context->CurrentScenePtr     = ZPushStructCtor(Context->Arena, EditorScene);

        if (ZEngine::Helpers::secure_strlen(file))
        {
            Context->ConfigurationPtr->ReadConfig(arena, file);

            if (!Context->ConfigurationPtr->ActiveSceneName.empty())
            {
                Context->CurrentScenePtr->Initialize(Context->Arena, Context->ConfigurationPtr->ActiveSceneName.c_str());
            }
        }

        UILayer->ParentContext                   = reinterpret_cast<void*>(Context);
        CanvasLayer->ParentContext               = reinterpret_cast<void*>(Context);

        std::string                  title       = fmt::format("{0} - Active Scene : {1}", Context->ConfigurationPtr->ProjectName.c_str(), Context->CurrentScenePtr->Name);
        Windows::WindowConfiguration window_conf = {.EnableVsync = true};
        window_conf.Title.init(Context->Arena, title.c_str());
        window_conf.RenderingLayerCollection.init(Context->Arena, 1);
        window_conf.OverlayLayerCollection.init(Context->Arena, 1);

        window_conf.RenderingLayerCollection.push(CanvasLayer);
        window_conf.OverlayLayerCollection.push(UILayer);

        Window = ZEngine::Windows::Create(Context->Arena, window_conf);

        Context->CameraControllerPtr->Initialize(Context->Arena, Window, 150.0, 0.f, 45.f);

        ZEngine::Engine::Initialize(arena, Window);
    }

    void Editor::Dispose()
    {
        Engine::Dispose();
        AssetManager::Shutdown();
    }

    void Editor::Run()
    {
        AssetManager::Run();
        Engine::Run();
    }

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
