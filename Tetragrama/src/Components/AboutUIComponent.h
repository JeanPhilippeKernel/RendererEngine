#pragma once
#include <ZEngine/ZEngine.h>

namespace Tetragrama::Components {
    class AboutUIComponent : public ZEngine::Components::UI::UIComponent {
    public:
        AboutUIComponent(std::string_view name = "AboutUIComponent", bool visibility = true) : UIComponent(name, visibility, true) {}
        virtual ~AboutUIComponent() = default;

        virtual void Render() override {
            ImGui::ShowAboutWindow(&m_is_open);
        }

        void Update(ZEngine::Core::TimeStep dt) override {}

    private:
        bool m_is_open{true};
    };
} // namespace Tetragrama::Components
