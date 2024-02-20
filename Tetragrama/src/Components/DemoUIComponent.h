#pragma once
#include <ZEngine/ZEngine.h>

namespace Tetragrama::Components {
    class DemoUIComponent : public ZEngine::Components::UI::UIComponent {
    public:
        DemoUIComponent(std::string_view name = "DemoUIComponent", bool visibility = true) : UIComponent(name, visibility, true) {}
        virtual ~DemoUIComponent() = default;

        virtual void Render() override {
            ImGui::ShowDemoWindow(&m_is_open);
        }

        void Update(ZEngine::Core::TimeStep dt) override {}

    private:
        bool m_is_open{true};
    };
} // namespace Tetragrama::Components
