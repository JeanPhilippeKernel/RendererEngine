#include <pch.h>
#include <ProjectViewUIComponent.h>
#include <imgui.h>

namespace Tetragrama::Components
{
    ProjectViewUIComponent::ProjectViewUIComponent(Layers::ImguiLayer* parent, std::string_view name, bool visibility) : UIComponent(parent, name, visibility, false) {}

    ProjectViewUIComponent::~ProjectViewUIComponent() {}

    void ProjectViewUIComponent::Update(ZEngine::Core::TimeStep dt) {}

    void ProjectViewUIComponent::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer)
    {
        ImGui::Begin(Name.c_str(), (CanBeClosed ? &CanBeClosed : NULL), ImGuiWindowFlags_NoCollapse);

        ImGui::End();
    }
} // namespace Tetragrama::Components
