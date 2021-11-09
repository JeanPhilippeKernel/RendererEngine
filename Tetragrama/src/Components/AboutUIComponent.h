#pragma once 
#include <ZEngine/ZEngine.h>

namespace Tetragrama::Components {
    class AboutUIComponent : public ZEngine::Components::UI::UIComponent {
    public:
        AboutUIComponent(std::string_view name = "AboutUIComponent", bool visibility = true) 
            : UIComponent(name, visibility)
        {}
        virtual ~AboutUIComponent() = default;

        virtual void Render() override {
            ImGui::ShowAboutWindow(&m_is_open);
        }
    private:
        bool m_is_open{true};        
    };
}