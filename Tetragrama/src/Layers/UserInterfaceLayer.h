#pragma once
#include <ZEngine/ZEngine.h>
#include <Editor.h>
#include <Components/DockspaceUIComponent.h>
#include <Components/SceneViewportUIComponent.h>
#include <Components/LogUIComponent.h>
#include <Components/DemoUIComponent.h>
#include <Components/Events/SceneTextureAvailableEvent.h>

namespace Tetragrama {
    class Editor;
}

namespace Tetragrama::Layers {
    class UserInterfaceLayer : public ZEngine::Layers::ImguiLayer {
    public:
        UserInterfaceLayer(const char* name = "user interface layer") : ImguiLayer(name) {}

        UserInterfaceLayer(ZEngine::Ref<Editor>&& editor, const char* name = "user interface layer") : ImguiLayer(name), m_editor(std::move(editor)) {}

        virtual ~UserInterfaceLayer() = default;

        void Initialize() override;

        virtual bool OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent& e) {
            ZEngine::Event::EventDispatcher event_dispatcher(e);
            event_dispatcher.Dispatch<Components::Event::SceneTextureAvailableEvent>(std::bind(&UserInterfaceLayer::OnSceneTextureAvailableRaised, this, std::placeholders::_1));

            if (!m_editor.expired()) {
                const auto editor_ptr = m_editor.lock();
                if (!e.IsHandled()) {
                    event_dispatcher.ForwardTo<Components::Event::SceneViewportResizedEvent>(std::bind(&Editor::OnUIComponentRaised, editor_ptr.get(), std::placeholders::_1));
                    event_dispatcher.ForwardTo<Components::Event::SceneViewportFocusedEvent>(std::bind(&Editor::OnUIComponentRaised, editor_ptr.get(), std::placeholders::_1));
                    event_dispatcher.ForwardTo<Components::Event::SceneViewportUnfocusedEvent>(std::bind(&Editor::OnUIComponentRaised, editor_ptr.get(), std::placeholders::_1));
                }
            }
            return false;
        }

        bool OnSceneTextureAvailableRaised(Components::Event::SceneTextureAvailableEvent& e);

    private:
        ZEngine::WeakRef<Editor>                           m_editor;
        ZEngine::Ref<ZEngine::Components::UI::UIComponent> m_dockspace_component;
        ZEngine::Ref<ZEngine::Components::UI::UIComponent> m_scene_component;
        ZEngine::Ref<ZEngine::Components::UI::UIComponent> m_log_component;
        ZEngine::Ref<ZEngine::Components::UI::UIComponent> m_demo_component;
    };

} // namespace Tetragrama::Layers
