#include <pch.h>
#include <Editor.h>
#include <MessageToken.h>
#include <Messengers/Messenger.h>
#include <fmt/format.h>

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
} // namespace Tetragrama
