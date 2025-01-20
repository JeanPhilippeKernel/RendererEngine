#pragma once
#include <UIComponent.h>
#include <ZEngine/Windows/Inputs/IInputEventCallback.h>
#include <ZEngine/Windows/Layers/Layer.h>
#include <vector>

namespace Tetragrama::Components
{
    struct UIComponent;
}

namespace Tetragrama::Layers
{
    struct NodeHierarchy
    {
        int Parent       = -1;
        int FirstChild   = -1;
        int RightSibling = -1;
        int DepthLevel   = -1;
    };

    class ImguiLayer : public ZEngine::Windows::Layers::Layer, public ZEngine::Windows::Inputs::IKeyboardEventCallback, public ZEngine::Windows::Inputs::IMouseEventCallback, public ZEngine::Windows::Inputs::ITextInputEventCallback, public ZEngine::Windows::Inputs::IWindowEventCallback
    {

    public:
        ImguiLayer(std::string_view name = "ImGUI Layer") : Layer(name) {}
        virtual ~ImguiLayer();

        std::vector<NodeHierarchy>                                         NodeHierarchies  = {};
        std::map<uint32_t, ZEngine::Helpers::Ref<Components::UIComponent>> NodeUIComponents = {};
        std::vector<uint32_t>                                              NodeToRender     = {};

        virtual void                                                       Initialize() override;
        virtual void                                                       Deinitialize() override;

        bool                                                               OnEvent(ZEngine::Core::CoreEvent& event) override;

        void                                                               Update(ZEngine::Core::TimeStep dt) override;

        void                                                               Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override;

        int                                                                AddNode(Components::UIComponent* cmp, int parent, int depth);
        virtual void                                                       AddUIComponent(Components::UIComponent* cmp, int parent, int depth);

        bool                                                               OnKeyPressed(ZEngine::Windows::Events::KeyPressedEvent&) override;
        bool                                                               OnKeyReleased(ZEngine::Windows::Events::KeyReleasedEvent&) override;

        bool                                                               OnMouseButtonPressed(ZEngine::Windows::Events::MouseButtonPressedEvent&) override;
        bool                                                               OnMouseButtonReleased(ZEngine::Windows::Events::MouseButtonReleasedEvent&) override;
        bool                                                               OnMouseButtonMoved(ZEngine::Windows::Events::MouseButtonMovedEvent&) override;
        bool                                                               OnMouseButtonWheelMoved(ZEngine::Windows::Events::MouseButtonWheelEvent&) override;
        bool                                                               OnTextInputRaised(ZEngine::Windows::Events::TextInputEvent&) override;

        bool                                                               OnWindowClosed(ZEngine::Windows::Events::WindowClosedEvent&) override;
        bool                                                               OnWindowResized(ZEngine::Windows::Events::WindowResizedEvent&) override;
        bool                                                               OnWindowMinimized(ZEngine::Windows::Events::WindowMinimizedEvent&) override;
        bool                                                               OnWindowMaximized(ZEngine::Windows::Events::WindowMaximizedEvent&) override;
        bool                                                               OnWindowRestored(ZEngine::Windows::Events::WindowRestoredEvent&) override;
    };
} // namespace Tetragrama::Layers
