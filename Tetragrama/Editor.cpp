#include <pch.h>
#include <Editor.h>
#include <Layers/UILayer.h>
#include <MessageToken.h>
#include <Messengers/Messenger.h>
#include <fmt/format.h>

using namespace Tetragrama::Messengers;
using namespace ZEngine;

namespace Tetragrama
{
    EditorConfiguration       Editor::s_editor_configuration;
    ZEngine::Ref<EditorScene> Editor::s_editor_scene{nullptr};
    std::recursive_mutex      Editor::s_mutex;

    Editor::Editor(const EditorConfiguration& config) : m_engine_configuration()
    {
        m_ui_layer     = ZEngine::CreateRef<Layers::UILayer>();
        m_render_layer = ZEngine::CreateRef<Layers::RenderLayer>();

        s_editor_configuration = config;
        if ((!s_editor_scene) && config.ActiveSceneName.empty())
        {
            s_editor_scene = ZEngine::CreateRef<EditorScene>("Empty_Scene");
        }
        else if (!config.ActiveSceneName.empty())
        {
            s_editor_scene = ZEngine::CreateRef<EditorScene>(config.ActiveSceneName);
        }

        auto title = config.ProjectName;
        if (s_editor_scene)
        {
            title = fmt::format("{0} - Active Scene : {1}", title, s_editor_scene->GetName());
        }
        m_engine_configuration.WindowConfiguration = {.EnableVsync = true, .Title = title, .RenderingLayerCollection = {m_render_layer}, .OverlayLayerCollection = {m_ui_layer}};

        m_window.reset(ZEngine::Windows::Create(m_engine_configuration.WindowConfiguration));
    }

    Editor::~Editor()
    {
        m_ui_layer.reset();
        m_render_layer.reset();
        m_window.reset();
        ZEngine::Engine::Dispose();
    }

    void Editor::Initialize()
    {
        ZEngine::Engine::Initialize(m_engine_configuration, m_window);

        MESSENGER_REGISTER(
            Windows::Layers::Layer,
            SINGLE_ARG(GenericMessage<std::pair<float, float>>),
            EDITOR_RENDER_LAYER_SCENE_REQUEST_RESIZE,
            m_render_layer.get(),
            return m_render_layer->SceneRequestResizeMessageHandlerAsync(*message_ptr))

        MESSENGER_REGISTER(
            Windows::Layers::Layer,
            GenericMessage<bool>,
            EDITOR_RENDER_LAYER_SCENE_REQUEST_FOCUS,
            m_render_layer.get(),
            return m_render_layer->SceneRequestFocusMessageHandlerAsync(*message_ptr))

        MESSENGER_REGISTER(
            Windows::Layers::Layer,
            GenericMessage<bool>,
            EDITOR_RENDER_LAYER_SCENE_REQUEST_UNFOCUS,
            m_render_layer.get(),
            return m_render_layer->SceneRequestUnfocusMessageHandlerAsync(*message_ptr))

        MESSENGER_REGISTER(
            Windows::Layers::Layer,
            SINGLE_ARG(GenericMessage<std::pair<int, int>>),
            EDITOR_RENDER_LAYER_SCENE_REQUEST_SELECT_ENTITY_FROM_PIXEL,
            m_render_layer.get(),
            return m_render_layer->SceneRequestSelectEntityFromPixelMessageHandlerAsync(*message_ptr))
    }

    void Editor::Run()
    {
        ZEngine::Engine::Run();
    }

    const EditorConfiguration& Editor::GetCurrentEditorConfiguration()
    {
        return s_editor_configuration;
    }

    ZEngine::Ref<EditorScene> Editor::GetCurrentEditorScene()
    {
        {
            std::unique_lock l(s_mutex);
            return s_editor_scene;
        }
    }

    void Editor::SetCurrentEditorScene(EditorScene&& scene)
    {
        {
            std::unique_lock l(s_mutex);
            s_editor_scene.reset(new EditorScene(scene));
        }
    }

    void EditorScene::AddModel(const Model& m)
    {
        {
            std::lock_guard l(m_mutex);
            m_models[m.Name]     = m;
            m_has_pending_change = true;
        }
    }

    void EditorScene::AddModel(Model&& m)
    {
        {
            std::lock_guard l(m_mutex);
            m_models[m.Name]     = std::move(m);
            m_has_pending_change = true;
        }
    }

    void EditorScene::SetName(std::string_view name)
    {
        {
            std::lock_guard l(m_mutex);
            m_name = name;
        }
    }

    std::string_view EditorScene::GetName() const
    {
        return m_name;
    }

    const std::unordered_map<std::string, EditorScene::Model>& EditorScene::GetModels() const
    {
        return m_models;
    }

    bool EditorScene::HasPendingChange() const
    {
        return m_has_pending_change;
    }
} // namespace Tetragrama
