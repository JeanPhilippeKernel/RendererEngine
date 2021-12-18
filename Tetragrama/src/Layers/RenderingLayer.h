#pragma once
#include <vector>
#include <ZEngine/ZEngine.h>
#include <Components/Events/SceneViewportResizedEvent.h>
#include <Components/Events/SceneViewportFocusedEvent.h>
#include <Components/Events/SceneViewportUnfocusedEvent.h>
#include <Components/Events/SceneTextureAvailableEvent.h>
#include <Editor.h>

namespace Tetragrama {
    class Editor;
}

namespace Tetragrama::Layers {
    class RenderingLayer : public ZEngine::Layers::Layer {
    public:
        RenderingLayer(ZEngine::Ref<Editor>&& editor, const char* name = "Rendering layer");

        virtual ~RenderingLayer() = default;

        virtual void Initialize() override;
        virtual void Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render() override;

        virtual bool OnEvent(ZEngine::Event::CoreEvent& e) override;

        virtual void OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent& e);

    private:
        bool OnSceneViewportResized(Components::Event::SceneViewportResizedEvent& e);
        bool OnSceneViewportFocused(Components::Event::SceneViewportFocusedEvent& e);
        bool OnSceneViewportUnfocused(Components::Event::SceneViewportUnfocusedEvent& e);
        bool OnSceneTextureAvailable(Components::Event::SceneTextureAvailableEvent& e);

    private:
        ZEngine::WeakRef<Editor>                                    m_editor;
        ZEngine::Ref<ZEngine::Rendering::Scenes::GraphicScene>      m_scene;
        ZEngine::Ref<ZEngine::Managers::TextureManager>             m_texture_manager;
        std::vector<ZEngine::Ref<ZEngine::Rendering::Meshes::Mesh>> m_mesh_collection;
    };

} // namespace Tetragrama::Layers
