#include <pch.h>
#include <Editor.h>
#include <MessageToken.h>
#include <Messengers/Messenger.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

using namespace ZEngine;
using namespace ZEngine::Helpers;
using namespace Tetragrama::Layers;
using namespace Tetragrama::Messengers;
using namespace Tetragrama::Controllers;

namespace Tetragrama
{
    Editor::Editor(const EditorConfiguration& config)
    {
        Configuration = config;
        Context       = CreateRef<EditorContext>();
        CurrentScene  = CreateRef<EditorScene>();
        UILayer       = CreateRef<ImguiLayer>();
        CanvasLayer   = CreateRef<RenderLayer>();
    }

    Editor::~Editor()
    {
        UILayer.reset();
        CanvasLayer.reset();
        m_window.reset();
        ZEngine::Engine::Dispose();
    }

    void Editor::Initialize()
    {
        if (!Configuration.ActiveSceneName.empty())
        {
            CurrentScene->Name = Configuration.ActiveSceneName;
        }

        Context->ConfigurationPtr                              = &Configuration;
        Context->CurrentScenePtr                               = CurrentScene.get();
        Context->CurrentScenePtr->RenderScene->IsDrawDataDirty = true;

        UILayer->ParentContext                                 = reinterpret_cast<void*>(Context.get());
        CanvasLayer->ParentContext                             = reinterpret_cast<void*>(Context.get());

        std::string title                                      = fmt::format("{0} - Active Scene : {1}", Configuration.ProjectName, CurrentScene->Name);
        m_window.reset(ZEngine::Windows::Create({.EnableVsync = true, .Title = title, .RenderingLayerCollection = {CanvasLayer}, .OverlayLayerCollection = {UILayer}}));
        CameraController             = CreateRef<EditorCameraController>(m_window, 150.0, 0.f, 45.f);
        Context->CameraControllerPtr = CameraController.get();

        ZEngine::Engine::Initialize({}, m_window);
    }

    void Editor::Run()
    {
        ZEngine::Engine::Run();
    }

    void EditorScene::Push(std::string_view mesh, std::string_view model, std::string_view material)
    {
        uint16_t mesh_file_id     = MeshFiles.size();
        uint16_t model_file_id    = ModelFiles.size();
        uint16_t material_file_id = MaterialFiles.size();

        MeshFiles.emplace_back(mesh);
        ModelFiles.emplace_back(model);
        MaterialFiles.emplace_back(material);

        std::stringstream ss;
        ss << mesh_file_id << ":" << model_file_id << ":" << material_file_id;
        auto hash            = ss.str();
        Data[hash]           = {.MeshFileIndex = mesh_file_id, .ModelPathIndex = model_file_id, .MaterialPathIndex = material_file_id};

        m_has_pending_change = true;
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

        ProjectName              = config["projectName"];
        WorkingSpacePath         = config["workingSpace"];
        DefaultImportTexturePath = config["defaultImportDir"]["textureDir"];
        DefaultImportSoundPath   = config["defaultImportDir"]["soundDir"];
        ScenePath                = config["sceneDir"];
        SceneDataPath            = config["sceneDataDir"];

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
            ActiveSceneName = scene["name"];
            break;
        }
    }
} // namespace Tetragrama
