#pragma once
#include <UIComponent.h>
#include <ZEngine/Inputs/IInputEventCallback.h>
#include <ZEngine/Layers/Layer.h>
#include <vector>

namespace Tetragrama::Components
{
    class UIComponent;
}

namespace Tetragrama::Layers
{
    class ImguiLayer : public ZEngine::Layers::Layer,
                       public ZEngine::Inputs::IKeyboardEventCallback,
                       public ZEngine::Inputs::IMouseEventCallback,
                       public ZEngine::Inputs::ITextInputEventCallback
    {

    public:
        ImguiLayer(std::string_view name = "ImGUI Layer") : Layer(name) {}

        virtual ~ImguiLayer();

        virtual void Initialize() override;
        virtual void Deinitialize() override;

        bool OnEvent(ZEngine::Event::CoreEvent& event) override;

        void Update(ZEngine::Core::TimeStep dt) override;

        void Render() override;

        virtual void AddUIComponent(const ZEngine::Ref<Components::UIComponent>& component);
        virtual void AddUIComponent(ZEngine::Ref<Components::UIComponent>&& component);
        virtual void AddUIComponent(std::vector<ZEngine::Ref<Components::UIComponent>>&& components);

    protected:
        bool OnKeyPressed(ZEngine::Event::KeyPressedEvent&) override;
        bool OnKeyReleased(ZEngine::Event::KeyReleasedEvent&) override;

        bool OnMouseButtonPressed(ZEngine::Event::MouseButtonPressedEvent&) override;
        bool OnMouseButtonReleased(ZEngine::Event::MouseButtonReleasedEvent&) override;
        bool OnMouseButtonMoved(ZEngine::Event::MouseButtonMovedEvent&) override;
        bool OnMouseButtonWheelMoved(ZEngine::Event::MouseButtonWheelEvent&) override;
        bool OnTextInputRaised(ZEngine::Event::TextInputEvent&) override;

        // bool OnWindowClosed(Event::WindowClosedEvent&) override;
        // bool OnWindowResized(Event::WindowResizedEvent&) override;
        // bool OnWindowMinimized(Event::WindowMinimizedEvent&) override;
        // bool OnWindowMaximized(Event::WindowMaximizedEvent&) override;
        // bool OnWindowRestored(Event::WindowRestoredEvent&) override;

    private:
        static bool                                        m_initialized;
        std::vector<ZEngine::Ref<Components::UIComponent>> m_ui_components;
    };
} // namespace Tetragrama::Layers
