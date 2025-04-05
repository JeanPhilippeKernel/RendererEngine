#include <pch.h>
#include <Editor.h>
#include <MessageToken.h>
#include <Messengers/Messenger.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

using namespace ZEngine;
using namespace ZEngine::Core::Container;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Helpers;
using namespace Tetragrama::Layers;
using namespace Tetragrama::Messengers;
using namespace Tetragrama::Controllers;

namespace Tetragrama
{
    Editor::~Editor()
    {
        ZEngine::Engine::Dispose();
    }

    void Editor::Initialize(ArenaAllocator* arena, const char* file)
    {
        Context = ZPushStruct(arena, EditorContext);

        arena->CreateSubArena(ZMega(50), &(Context->Arena));

        Context->ConfigurationPtr    = ZPushStructCtor(&(Context->Arena), EditorConfiguration);
        Context->CameraControllerPtr = ZPushStructCtor(&(Context->Arena), EditorCameraController);
        UILayer                      = ZPushStructCtor(&(Context->Arena), ImguiLayer);
        CanvasLayer                  = ZPushStructCtor(&(Context->Arena), RenderLayer);

        Context->CurrentScenePtr     = ZPushStructCtor(&(Context->Arena), EditorScene);

        if (Helpers::secure_strlen(file))
        {
            Context->ConfigurationPtr->ReadConfig(file);

            if (Helpers::secure_strlen(Context->ConfigurationPtr->ActiveSceneName))
            {
                Context->CurrentScenePtr->Initialize(&(Context->Arena), Context->ConfigurationPtr->ActiveSceneName);
            }
        }

        UILayer->ParentContext                   = reinterpret_cast<void*>(Context);
        CanvasLayer->ParentContext               = reinterpret_cast<void*>(Context);

        std::string                  title       = fmt::format("{0} - Active Scene : {1}", Context->ConfigurationPtr->ProjectName, Context->CurrentScenePtr->Name);
        Windows::WindowConfiguration window_conf = {.EnableVsync = true};
        window_conf.Title.init(&(Context->Arena), title.c_str());
        window_conf.RenderingLayerCollection.init(&(Context->Arena), 1, 0);
        window_conf.OverlayLayerCollection.init(&(Context->Arena), 1, 0);

        window_conf.RenderingLayerCollection.push(CanvasLayer);
        window_conf.OverlayLayerCollection.push(UILayer);

        Window = ZEngine::Windows::Create(&(Context->Arena), window_conf);

        Context->CameraControllerPtr->Initialize(&(Context->Arena), Window, 150.0, 0.f, 45.f);

        ZEngine::Engine::Initialize(arena, {}, Window);
    }

    void Editor::Run()
    {
        ZEngine::Engine::Run();
    }

    void EditorScene::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, const char* name)
    {
        Name                         = name;

        RenderScene                  = ZPushStructCtor(arena, ZEngine::Rendering::Scenes::GraphicScene);
        RenderScene->IsDrawDataDirty = true;

        MeshFiles.init(arena, 1, 0);
        ModelFiles.init(arena, 1, 0);
        MaterialFiles.init(arena, 1, 0);
        Hashes.init(arena, 1, 0);
    }

    void EditorScene::Push(ZEngine::Core::Memory::ArenaAllocator* arena, const char* mesh, const char* model, const char* material)
    {
        uint16_t mesh_file_id     = MeshFiles.size();
        uint16_t model_file_id    = ModelFiles.size();
        uint16_t material_file_id = MaterialFiles.size();

        String   mesh_file        = {};
        String   model_file       = {};
        String   mat_file         = {};

        mesh_file.init(arena, mesh);
        model_file.init(arena, model);
        mat_file.init(arena, material);

        MeshFiles.push(mesh_file);
        ModelFiles.push(model_file);
        MaterialFiles.push(mat_file);

        std::stringstream ss;
        ss << mesh_file_id << ":" << model_file_id << ":" << material_file_id;
        auto   hash     = ss.str();

        String hash_str = {};
        hash_str.init(arena, hash.c_str());
        Hashes.push(hash_str);

        Data[hash_str.c_str()] = {.MeshFileIndex = mesh_file_id, .ModelPathIndex = model_file_id, .MaterialPathIndex = material_file_id};

        m_has_pending_change   = true;
    }

    bool EditorScene::HasPendingChange() const
    {
        return m_has_pending_change;
    }

    void EditorConfiguration::ReadConfig(std::string_view file)
    {
        std::ifstream  f(file.data());
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

        Helpers::secure_strcpy(ProjectName, 50, config["projectName"].get<std::string>().c_str());

        Helpers::secure_strcpy(WorkingSpacePath, MAX_FILE_PATH_COUNT, config["workingSpace"].get<std::string>().c_str());
        Helpers::secure_strcpy(DefaultImportTexturePath, MAX_FILE_PATH_COUNT, config["defaultImportDir"]["textureDir"].get<std::string>().c_str());
        Helpers::secure_strcpy(DefaultImportSoundPath, MAX_FILE_PATH_COUNT, config["defaultImportDir"]["soundDir"].get<std::string>().c_str());
        Helpers::secure_strcpy(ScenePath, MAX_FILE_PATH_COUNT, config["sceneDir"].get<std::string>().c_str());
        Helpers::secure_strcpy(SceneDataPath, MAX_FILE_PATH_COUNT, config["sceneDataDir"].get<std::string>().c_str());

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
            Helpers::secure_strcpy(ActiveSceneName, 50, scene["name"].get<std::string>().c_str());
            break;
        }
    }
} // namespace Tetragrama
