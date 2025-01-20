#pragma once
#include <UIComponent.h>
#include <string>

namespace Tetragrama::Components
{
    class ProjectViewUIComponent : public UIComponent
    {
    public:
        ProjectViewUIComponent(Layers::ImguiLayer* parent = nullptr, std::string_view name = "Project", bool visibility = true);
        virtual ~ProjectViewUIComponent();

        void         Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override;
    };
} // namespace Tetragrama::Components
