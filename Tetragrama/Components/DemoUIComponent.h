#pragma once
#include <UIComponent.h>
#include <imgui.h>

namespace Tetragrama::Components
{
    class DemoUIComponent : public UIComponent
    {
    public:
        DemoUIComponent() {}
        virtual ~DemoUIComponent() = default;

        void Initialize(Layers::ImguiLayer* parent = nullptr, const char* name = "DemoUIComponent", bool visibility = true, bool closed = false) override
        {
            UIComponent::Initialize(parent, name, visibility, closed);
        }

        virtual void Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override
        {
            ImGui::ShowDemoWindow(&m_is_open);
        }

        void Update(ZEngine::Core::TimeStep dt) override {}

    private:
        bool m_is_open{true};
    };
} // namespace Tetragrama::Components
