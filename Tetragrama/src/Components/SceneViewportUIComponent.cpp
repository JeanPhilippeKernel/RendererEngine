#include <pch.h>
#include <SceneViewportUIComponent.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <Layers/UILayer.h>
#include <Event/EventDispatcher.h>
#include <Messengers/Messenger.h>
#include <MessageToken.h>

using namespace ZEngine::Components::UI::Event;
using namespace Tetragrama::Components::Event;

namespace Tetragrama::Components {
    SceneViewportUIComponent::SceneViewportUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility, false) {}

    SceneViewportUIComponent::~SceneViewportUIComponent() {}

    void SceneViewportUIComponent::Update(ZEngine::Core::TimeStep dt) {
        if ((m_viewport_size.x != m_content_region_available_size.x) || (m_viewport_size.y != m_content_region_available_size.y)) {
            m_viewport_size = m_content_region_available_size;

            Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::GenericMessage<std::pair<float, float>>>(
                EDITOR_COMPONENT_SCENEVIEWPORT_RESIZED, Messengers::GenericMessage<std::pair<float, float>>{{m_viewport_size.x, m_viewport_size.y}});
        }

        if (m_is_window_hovered && m_is_window_focused) {
            Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::GenericMessage<bool>>(
                EDITOR_COMPONENT_SCENEVIEWPORT_FOCUSED, Messengers::GenericMessage<bool>{true});

        } else {
            Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::GenericMessage<bool>>(
                EDITOR_COMPONENT_SCENEVIEWPORT_UNFOCUSED, Messengers::GenericMessage<bool>{false});
        }
    }

    void SceneViewportUIComponent::Render() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin(m_name.c_str(), (m_can_be_closed ? &m_can_be_closed : NULL), ImGuiWindowFlags_NoCollapse);

        m_content_region_available_size = ImGui::GetContentRegionAvail();
        m_is_window_focused             = ImGui::IsWindowFocused();
        m_is_window_hovered             = ImGui::IsWindowHovered();

        ImGui::Image(reinterpret_cast<void*>(m_scene_texture_identifier), m_viewport_size, ImVec2(0, 1), ImVec2(1, 0));

        ImGui::End();

        ImGui::PopStyleVar();
    }

    void SceneViewportUIComponent::SetSceneTexture(uint32_t scene_texture) {
        m_scene_texture_identifier = scene_texture;
    }

    void SceneViewportUIComponent::SceneTextureAvailableMessageHandler(Messengers::GenericMessage<uint32_t>& message) {
        SetSceneTexture(message.GetValue());
    }

    void SceneViewportUIComponent::SceneViewportResizedMessageHandler(Messengers::GenericMessage<std::pair<float, float>>& e) {
        const auto& value = e.GetValue();
        ZENGINE_EDITOR_INFO("Viewport resized : {} - {}", value.first, value.second);

        Messengers::IMessenger::SendAsync<ZEngine::Layers::Layer, Messengers::GenericMessage<std::pair<float, float>>>(
            EDITOR_RENDER_LAYER_SCENE_REQUEST_RESIZE, Messengers::GenericMessage<std::pair<float, float>>{e});
    }

    void SceneViewportUIComponent::SceneViewportFocusedMessageHandler(Messengers::GenericMessage<bool>& e) {
        Messengers::IMessenger::SendAsync<ZEngine::Layers::Layer, Messengers::GenericMessage<bool>>(EDITOR_RENDER_LAYER_SCENE_REQUEST_FOCUS, Messengers::GenericMessage<bool>{e});
    }

    void SceneViewportUIComponent::SceneViewportUnfocusedMessageHandler(Messengers::GenericMessage<bool>& e) {
        Messengers::IMessenger::SendAsync<ZEngine::Layers::Layer, Messengers::GenericMessage<bool>>(EDITOR_RENDER_LAYER_SCENE_REQUEST_UNFOCUS, Messengers::GenericMessage<bool>{e});
    }

    void SceneViewportUIComponent::SceneViewportRequestRecomputationMessageHandler(Messengers::EmptyMessage&) {
        Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::GenericMessage<std::pair<float, float>>>(
            EDITOR_COMPONENT_SCENEVIEWPORT_RESIZED, Messengers::GenericMessage<std::pair<float, float>>{{m_viewport_size.x, m_viewport_size.y}});
    }
} // namespace Tetragrama::Components
