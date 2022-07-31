#include <pch.h>
#include <ProjectViewUIComponent.h>

namespace Tetragrama::Components {
    ProjectViewUIComponent::ProjectViewUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility) {}

    ProjectViewUIComponent::~ProjectViewUIComponent() {}

    void ProjectViewUIComponent::Update(ZEngine::Core::TimeStep dt) {}

    bool ProjectViewUIComponent::OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) {
        return false;
    }

    void ProjectViewUIComponent::Render() {
        ImGui::Begin(m_name.c_str(), &m_visibility, ImGuiWindowFlags_NoCollapse);

        ImGui::End();
    }
} // namespace Tetragrama::Components
