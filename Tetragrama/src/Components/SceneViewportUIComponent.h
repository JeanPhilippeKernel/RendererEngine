#pragma once
#include <ZEngine/ZEngine.h>

namespace Tetragrama::Components {
    class SceneViewportUIComponent : public ZEngine::Components::UI::UIComponent {
    public:
        SceneViewportUIComponent(std::string_view name = "Scene viewport UI Component", bool visibility = true);
        virtual ~SceneViewportUIComponent();

        virtual void Render() override;

    protected:
        virtual bool OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent&) override;
    };
}