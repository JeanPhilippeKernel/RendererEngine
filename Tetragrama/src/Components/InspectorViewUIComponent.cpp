#include <pch.h>
#include <InspectorViewUIComponent.h>

namespace Tetragrama::Components {
    InspectorViewUIComponent::InspectorViewUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility) {}

    InspectorViewUIComponent::~InspectorViewUIComponent() {}

    void InspectorViewUIComponent::Update(ZEngine::Core::TimeStep dt) {}

    bool InspectorViewUIComponent::OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) {
        return false;
    }

    void InspectorViewUIComponent::Render() {
        ImGui::Begin(m_name.c_str(), &m_visibility, ImGuiWindowFlags_NoCollapse);

        ImGui::End();
    }
} // namespace Tetragrama::Components
