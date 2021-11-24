#include <pch.h>
#include <SceneViewportUIComponent.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <Layers/UserInterfaceLayer.h>
#include <Event/EventDispatcher.h>

using namespace ZEngine::Components::UI::Event;
using namespace Tetragrama::Components::Event;

namespace Tetragrama::Components {
    SceneViewportUIComponent::SceneViewportUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility) {}

    SceneViewportUIComponent::~SceneViewportUIComponent() {}

    bool SceneViewportUIComponent::OnUIComponentRaised(UIComponentEvent& e) {
        // ZEngine::Event::EventDispatcher event_dispatcher(e);
        // event_dispatcher.Dispatch<SceneViewportResizedEvent>(std::bind(&SceneViewportUIComponent::OnSceneViewportResized, this, std::placeholders::_1));
        return false;
    }

    void SceneViewportUIComponent::Update(ZEngine::Core::TimeStep dt) {
        if ((m_viewport_size.x != m_content_region_available_size.x) || (m_viewport_size.y != m_content_region_available_size.y)) {
            m_viewport_size = m_content_region_available_size;
            SceneViewportResizedEvent e{m_viewport_size.x, m_viewport_size.y};
            OnSceneViewportResized(e);
        }

        if (m_is_window_hovered && m_is_window_focused) {
            SceneViewportFocusedEvent e;
            OnSceneViewportFocused(e);
        } else {
            SceneViewportUnfocusedEvent e;
            OnSceneViewportUnfocused(e);
        }
    }

    void SceneViewportUIComponent::Render() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin(m_name.c_str(), &m_visibility, ImGuiWindowFlags_NoCollapse);

        m_content_region_available_size = ImGui::GetContentRegionAvail();
        m_is_window_focused             = ImGui::IsWindowFocused();
        m_is_window_hovered             = ImGui::IsWindowHovered();

        ImGui::Image((void*) m_scene_texture_identifier, m_viewport_size, ImVec2(0, 1), ImVec2(1, 0));

        ImGui::End();

        ImGui::PopStyleVar();
    }

    bool SceneViewportUIComponent::OnSceneViewportResized(Event::SceneViewportResizedEvent& e) {
        ZENGINE_CORE_INFO("Viewport resized : {} - {}", e.GetWidth(), e.GetHeight());
        auto layer = m_parent_layer.lock();

        const auto user_interface_ptr = dynamic_cast<Layers::UserInterfaceLayer*>(layer.get());
        if (user_interface_ptr) {
            ZEngine::Event::EventDispatcher event_dispatcher(e);
            event_dispatcher.ForwardTo<SceneViewportResizedEvent>(std::bind(&Layers::UserInterfaceLayer::OnUIComponentRaised, user_interface_ptr, std::placeholders::_1));
        }
        return false;
    }

    bool SceneViewportUIComponent::OnSceneViewportFocused(Event::SceneViewportFocusedEvent& e) {
        auto layer = m_parent_layer.lock();

        const auto user_interface_ptr = dynamic_cast<Layers::UserInterfaceLayer*>(layer.get());
        if (user_interface_ptr) {
            ZEngine::Event::EventDispatcher event_dispatcher(e);
            event_dispatcher.ForwardTo<SceneViewportFocusedEvent>(std::bind(&Layers::UserInterfaceLayer::OnUIComponentRaised, user_interface_ptr, std::placeholders::_1));
        }
        return false;
    }

    bool SceneViewportUIComponent::OnSceneViewportUnfocused(Event::SceneViewportUnfocusedEvent& e) {
        auto layer = m_parent_layer.lock();

        const auto user_interface_ptr = dynamic_cast<Layers::UserInterfaceLayer*>(layer.get());
        if (user_interface_ptr) {
            ZEngine::Event::EventDispatcher event_dispatcher(e);
            event_dispatcher.ForwardTo<SceneViewportUnfocusedEvent>(std::bind(&Layers::UserInterfaceLayer::OnUIComponentRaised, user_interface_ptr, std::placeholders::_1));
        }
        return false;
    }
} // namespace Tetragrama::Components
