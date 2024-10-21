#pragma once
#include <UIComponent.h>
#include <ZEngine/Windows/Inputs/IInputEventCallback.h>
#include <ZEngine/Windows/Layers/Layer.h>
#include <vector>

namespace Tetragrama::Components
{
    class UIComponent;
}

namespace Tetragrama::Layers
{
    class ImguiLayer : public ZEngine::Windows::Layers::Layer,
                       public ZEngine::Windows::Inputs::IKeyboardEventCallback,
                       public ZEngine::Windows::Inputs::IMouseEventCallback,
                       public ZEngine::Windows::Inputs::ITextInputEventCallback,
                       public ZEngine::Windows::Inputs::IWindowEventCallback
    {

    public:
        ImguiLayer(std::string_view name = "ImGUI Layer") : Layer(name) {}

        virtual ~ImguiLayer();

        virtual void Initialize() override;
        virtual void Deinitialize() override;

        bool OnEvent(ZEngine::Core::CoreEvent& event) override;

        void Update(ZEngine::Core::TimeStep dt) override;

        void Render() override;

        virtual void AddUIComponent(const ZEngine::Ref<Components::UIComponent>& component);
        virtual void AddUIComponent(ZEngine::Ref<Components::UIComponent>&& component);
        virtual void AddUIComponent(std::vector<ZEngine::Ref<Components::UIComponent>>&& components);

    protected:
        bool OnKeyPressed(ZEngine::Windows::Events::KeyPressedEvent&) override;
        bool OnKeyReleased(ZEngine::Windows::Events::KeyReleasedEvent&) override;

        bool OnMouseButtonPressed(ZEngine::Windows::Events::MouseButtonPressedEvent&) override;
        bool OnMouseButtonReleased(ZEngine::Windows::Events::MouseButtonReleasedEvent&) override;
        bool OnMouseButtonMoved(ZEngine::Windows::Events::MouseButtonMovedEvent&) override;
        bool OnMouseButtonWheelMoved(ZEngine::Windows::Events::MouseButtonWheelEvent&) override;
        bool OnTextInputRaised(ZEngine::Windows::Events::TextInputEvent&) override;

        bool OnWindowClosed(ZEngine::Windows::Events::WindowClosedEvent&) override;
        bool OnWindowResized(ZEngine::Windows::Events::WindowResizedEvent&) override;
        bool OnWindowMinimized(ZEngine::Windows::Events::WindowMinimizedEvent&) override;
        bool OnWindowMaximized(ZEngine::Windows::Events::WindowMaximizedEvent&) override;
        bool OnWindowRestored(ZEngine::Windows::Events::WindowRestoredEvent&) override;

    private:
        static bool                                        m_initialized;
        std::vector<ZEngine::Ref<Components::UIComponent>> m_ui_components;
    };
} // namespace Tetragrama::Layers
