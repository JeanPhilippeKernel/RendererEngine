#pragma once
#include <string>
#include <ZEngine/ZEngine.h>

namespace Tetragrama::Components {
    class InspectorViewUIComponent : public ZEngine::Components::UI::UIComponent {
    public:
        InspectorViewUIComponent(std::string_view name = "InspectorViewUIComponent", bool visibility = true);
        virtual ~InspectorViewUIComponent();

        void Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render() override;

    protected:
        virtual bool OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) override;
    };
} // namespace Tetragrama::Components
