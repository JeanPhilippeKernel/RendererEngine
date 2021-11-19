#pragma once 
#include <ZEngine/ZEngine.h>

namespace Sandbox::Components {
    class AboutUIComponent : public ZEngine::Components::UI::UIComponent {
    public:
        AboutUIComponent(std::string_view name = "AboutUIComponent", bool visibility = true) 
            : UIComponent(name, visibility)
        {}
        virtual ~AboutUIComponent() = default;

        void Update(ZEngine::Core::TimeStep dt) override {}
        
        virtual void Render() override {
            ImGui::ShowAboutWindow(&m_is_open);
        }

    protected:
        virtual bool OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) override { return false; }

    private:
        bool m_is_open{true};        
    };
}