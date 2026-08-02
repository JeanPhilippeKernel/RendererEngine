#pragma once
#include <Tetragrama/Components/UIComponent.h>
#include <ZEngine/Applications/Layer.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/VFSScanner.h>
#include <ZEngine/Helpers/NodeHierarchyHelper.h>
#include <ZEngine/Windows/Inputs/IInputEventCallback.h>

namespace Tetragrama::Components
{
    struct UIComponent;
}

namespace Tetragrama::Layers
{
    struct ImguiLayer : public ZEngine::Applications::Layer, public ZEngine::Windows::Inputs::IKeyboardEventCallback, public ZEngine::Windows::Inputs::IMouseEventCallback, public ZEngine::Windows::Inputs::ITextInputEventCallback, public ZEngine::Windows::Inputs::IWindowEventCallback
    {
        ImguiLayer(cstring name = "ImGUI Layer") : ZEngine::Applications::Layer(name) {}
        virtual ~ImguiLayer();

        ZEngine::Core::Containers::Array<ZEngine::Helpers::NodeHierarchy>                       NodeHierarchies  = {};
        ZEngine::Core::Containers::Array<uint32_t>                                              NodeToRender     = {};
        ZEngine::Core::Containers::UnorderedHashMap<uint32_t, Components::UIComponent*>         NodeUIComponents = {};
        ZEngine::Core::Containers::UnorderedHashMap<ZEngine::Windows::Inputs::GlfwKeyCode, int> KeyEntries       = {};

        ZEngine::Core::VFS::VFSScanner                                                          Scanner          = {};
        ZEngine::Core::VFS::VFSDirectoryCache                                                   Cache            = {};

        virtual void                                                                            Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, ZEngine::Applications::GameApplicationPtr app) override;
        virtual void                                                                            Deinitialize() override;

        bool                                                                                    OnEvent(ZEngine::Core::CoreEvent& event) override;

        void                                                                                    Update(ZEngine::Core::TimeStep dt) override;

        void                                                                                    Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override;

        int                                                                                     AddNode(Components::UIComponent* cmp, int parent, int depth);
        virtual void                                                                            AddUIComponent(Components::UIComponent* cmp, int parent, int depth);

        bool                                                                                    OnKeyPressed(ZEngine::Windows::Events::KeyPressedEvent&) override;
        bool                                                                                    OnKeyReleased(ZEngine::Windows::Events::KeyReleasedEvent&) override;

        bool                                                                                    OnMouseButtonPressed(ZEngine::Windows::Events::MouseButtonPressedEvent&) override;
        bool                                                                                    OnMouseButtonReleased(ZEngine::Windows::Events::MouseButtonReleasedEvent&) override;
        bool                                                                                    OnMouseButtonMoved(ZEngine::Windows::Events::MouseButtonMovedEvent&) override;
        bool                                                                                    OnMouseButtonWheelMoved(ZEngine::Windows::Events::MouseButtonWheelEvent&) override;
        bool                                                                                    OnTextInputRaised(ZEngine::Windows::Events::TextInputEvent&) override;

        bool                                                                                    OnWindowClosed(ZEngine::Windows::Events::WindowClosedEvent&) override;
        bool                                                                                    OnWindowResized(ZEngine::Windows::Events::WindowResizedEvent&) override;
        bool                                                                                    OnWindowMinimized(ZEngine::Windows::Events::WindowMinimizedEvent&) override;
        bool                                                                                    OnWindowMaximized(ZEngine::Windows::Events::WindowMaximizedEvent&) override;
        bool                                                                                    OnWindowRestored(ZEngine::Windows::Events::WindowRestoredEvent&) override;
    };
    ZDEFINE_PTR(ImguiLayer);
} // namespace Tetragrama::Layers
