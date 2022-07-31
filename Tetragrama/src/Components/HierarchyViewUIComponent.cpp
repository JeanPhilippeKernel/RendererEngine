#include <pch.h>
#include <HierarchyViewUIComponent.h>

namespace Tetragrama::Components {
    HierarchyViewUIComponent::HierarchyViewUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility) {}

    HierarchyViewUIComponent::~HierarchyViewUIComponent() {}

    void HierarchyViewUIComponent::Update(ZEngine::Core::TimeStep dt) {}

    bool HierarchyViewUIComponent::OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) {
        return false;
    }

    void HierarchyViewUIComponent::Render() {
        ImGui::Begin(m_name.c_str(), &m_visibility, ImGuiWindowFlags_NoCollapse);

        ImGui::End();
    }
} // namespace Tetragrama::Components
