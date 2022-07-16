#include <pch.h>
#include <UserInterfaceLayer.h>
#include <Messenger.h>
#include <MessageToken.h>

using namespace Tetragrama::Messengers;

namespace Tetragrama::Layers {

    void UserInterfaceLayer::Initialize() {
        ImguiLayer::Initialize();

        m_dockspace_component = std::make_shared<Components::DockspaceUIComponent>();
        m_scene_component     = std::make_shared<Components::SceneViewportUIComponent>();
        m_log_component       = std::make_shared<Components::LogUIComponent>();
        m_demo_component      = std::make_shared<Components::DemoUIComponent>();

        m_dockspace_component->AddChild(m_demo_component);
        m_dockspace_component->AddChild(m_scene_component);
        m_dockspace_component->AddChild(m_log_component);

        this->AddUIComponent(m_dockspace_component);

        // Register components
        IMessenger::Register<ZEngine::Components::UI::UIComponent, GenericMessage<uint32_t>>(m_scene_component, EDITOR_COMPONENT_SCENEVIEWPORT_TEXTURE_AVAILABLE,
            std::bind(&Components::SceneViewportUIComponent::SceneTextureAvailableMessageHandler, reinterpret_cast<Components::SceneViewportUIComponent*>(m_scene_component.get()),
                std::placeholders::_1));

        IMessenger::Register<ZEngine::Components::UI::UIComponent, GenericMessage<bool>>(m_scene_component, EDITOR_COMPONENT_SCENEVIEWPORT_FOCUSED,
            std::bind(&Components::SceneViewportUIComponent::SceneViewportFocusedMessageHandler, reinterpret_cast<Components::SceneViewportUIComponent*>(m_scene_component.get()),
                std::placeholders::_1));

        IMessenger::Register<ZEngine::Components::UI::UIComponent, GenericMessage<bool>>(m_scene_component, EDITOR_COMPONENT_SCENEVIEWPORT_UNFOCUSED,
            std::bind(&Components::SceneViewportUIComponent::SceneViewportUnfocusedMessageHandler, reinterpret_cast<Components::SceneViewportUIComponent*>(m_scene_component.get()),
                std::placeholders::_1));

        IMessenger::Register<ZEngine::Components::UI::UIComponent, GenericMessage<std::pair<float, float>>>(m_scene_component, EDITOR_COMPONENT_SCENEVIEWPORT_RESIZED,
            std::bind(&Components::SceneViewportUIComponent::SceneViewportResizedMessageHandler, reinterpret_cast<Components::SceneViewportUIComponent*>(m_scene_component.get()),
                std::placeholders::_1));
    }
} // namespace Tetragrama::Layers
