#pragma once 
#include <ZEngine/ZEngine.h>

namespace Sandbox3D::Components {
    class DemoUIComponent : public ZEngine::Components::UI::UIComponent {
    public:
        DemoUIComponent(std::string_view name = "DemoUIComponent", bool visibility = true) 
            : UIComponent(name, visibility)
        {}
        virtual ~DemoUIComponent() = default;
        
        void Update(ZEngine::Core::TimeStep dt) override {}

        virtual void Render() override {
            ImGui::ShowDemoWindow(&m_is_open);
        }

    protected:
        virtual bool OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) override { return false; }

    private:
        bool m_is_open{true};
    };
}