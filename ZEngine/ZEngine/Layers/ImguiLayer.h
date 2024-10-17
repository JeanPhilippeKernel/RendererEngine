#pragma once
#include <Components/UIComponent.h>
#include <Core/TimeStep.h>
#include <Inputs/IKeyboardEventCallback.h>
#include <Inputs/IMouseEventCallback.h>
#include <Inputs/ITextInputEventCallback.h>
#include <Inputs/KeyCode.h>
#include <Layers/Layer.h>
#include <Rendering/Swapchain.h>
#include <Window/ICoreWindowEventCallback.h>
#include <backends/imgui_impl_vulkan.h>
#include <imconfig.h>
#include <imgui.h>
#include <functional>
#include <string_view>
#include <vector>

namespace ZEngine::Components::UI
{
    class UIComponent;
}

namespace ZEngine::Layers
{
    class ImguiLayer : public Layer,
                       public Inputs::IKeyboardEventCallback,
                       public Inputs::IMouseEventCallback,
                       public Inputs::ITextInputEventCallback,
                       public Window::ICoreWindowEventCallback
    {

    public:
        ImguiLayer(std::string_view name = "ImGUI Layer") : Layer(name) {}

        virtual ~ImguiLayer();

        virtual void Initialize() override;
        virtual void Deinitialize() override;

        bool OnEvent(Event::CoreEvent& event) override;

        void Update(Core::TimeStep dt) override;

        void Render() override;

        virtual void AddUIComponent(const Ref<Components::UI::UIComponent>& component);
        virtual void AddUIComponent(Ref<Components::UI::UIComponent>&& component);
        virtual void AddUIComponent(std::vector<Ref<Components::UI::UIComponent>>&& components);

    protected:
        bool OnKeyPressed(Event::KeyPressedEvent&) override;
        bool OnKeyReleased(Event::KeyReleasedEvent&) override;

        bool OnMouseButtonPressed(Event::MouseButtonPressedEvent&) override;
        bool OnMouseButtonReleased(Event::MouseButtonReleasedEvent&) override;
        bool OnMouseButtonMoved(Event::MouseButtonMovedEvent&) override;
        bool OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent&) override;
        bool OnTextInputRaised(Event::TextInputEvent&) override;

        bool OnWindowClosed(Event::WindowClosedEvent&) override;
        bool OnWindowResized(Event::WindowResizedEvent&) override;
        bool OnWindowMinimized(Event::WindowMinimizedEvent&) override;
        bool OnWindowMaximized(Event::WindowMaximizedEvent&) override;
        bool OnWindowRestored(Event::WindowRestoredEvent&) override;

    private:
        static bool                                   m_initialized;
        std::vector<Ref<Components::UI::UIComponent>> m_ui_components;
    };
} // namespace ZEngine::Layers
