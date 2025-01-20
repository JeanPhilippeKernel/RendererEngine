#pragma once
#include <UIComponent.h>
#include <imgui.h>

namespace Tetragrama::Components
{
    class DemoUIComponent : public UIComponent
    {
    public:
        DemoUIComponent(Layers::ImguiLayer* parent = nullptr, std::string_view name = "DemoUIComponent", bool visibility = true) : UIComponent(parent, name, visibility, true) {}
        virtual ~DemoUIComponent() = default;

        virtual void Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override
        {
            ImGui::ShowDemoWindow(&m_is_open);
        }

        void Update(ZEngine::Core::TimeStep dt) override {}

    private:
        bool m_is_open{true};
    };
} // namespace Tetragrama::Components
