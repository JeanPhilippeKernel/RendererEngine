#include <pch.h>
#include <ProjectViewUIComponent.h>

namespace Tetragrama::Components
{
    ProjectViewUIComponent::ProjectViewUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility, false) {}

    ProjectViewUIComponent::~ProjectViewUIComponent() {}

    void ProjectViewUIComponent::Update(ZEngine::Core::TimeStep dt) {}

    void ProjectViewUIComponent::Render()
    {
        ImGui::Begin(m_name.c_str(), (m_can_be_closed ? &m_can_be_closed : NULL), ImGuiWindowFlags_NoCollapse);

        ImGui::End();
    }
} // namespace Tetragrama::Components
