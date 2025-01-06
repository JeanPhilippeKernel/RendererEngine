#include <pch.h>
#include <ProjectViewUIComponent.h>
#include <imgui.h>

namespace Tetragrama::Components
{
    ProjectViewUIComponent::ProjectViewUIComponent(std::string_view name, bool visibility) : UIComponent(name, visibility, false) {}

    ProjectViewUIComponent::~ProjectViewUIComponent() {}

    void ProjectViewUIComponent::Update(ZEngine::Core::TimeStep dt) {}

    void ProjectViewUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Rendering::Buffers::CommandBuffer* const command_buffer)
    {
        ImGui::Begin(Name.c_str(), (CanBeClosed ? &CanBeClosed : NULL), ImGuiWindowFlags_NoCollapse);

        ImGui::End();
    }
} // namespace Tetragrama::Components
