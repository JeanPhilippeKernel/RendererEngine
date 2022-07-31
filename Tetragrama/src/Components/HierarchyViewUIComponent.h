#pragma once
#include <string>
#include <ZEngine/ZEngine.h>

namespace Tetragrama::Components {
    class HierarchyViewUIComponent : public ZEngine::Components::UI::UIComponent {
    public:
        HierarchyViewUIComponent(std::string_view name = "HierarchyViewUIComponent", bool visibility = true);
        virtual ~HierarchyViewUIComponent();

        void Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render() override;

    protected:
        virtual bool OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) override;
    };
} // namespace Tetragrama::Components
