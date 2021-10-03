#pragma once 
#include <ZEngine/ZEngine.h>

namespace Sandbox::Components {
    class DemoUIComponent : public ZEngine::Components::UI::UIComponent {
    public:
        DemoUIComponent(std::string_view name = "DemoUIComponent", bool visibility = true) 
            : UIComponent(name, visibility)
        {}
        virtual ~DemoUIComponent() = default;

        virtual void Render() override {
            ImGui::ShowDemoWindow(&m_is_open);
        }
    private:
        bool m_is_open{true};
    };
}