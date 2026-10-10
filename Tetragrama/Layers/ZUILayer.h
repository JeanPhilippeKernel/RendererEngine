#pragma once
#include <ZEngine/Applications/Layer.h>
#include <ZEngine/Windows/Inputs/IInputEventCallback.h>

namespace ZEngine::UI
{
    struct ZUIContext;
}
namespace Tetragrama::Components
{
    struct ZUIComponent;
}

namespace Tetragrama::Layers
{
    struct ZUILayer : public ZEngine::Applications::Layer, public ZEngine::Windows::Inputs::IKeyboardEventCallback, public ZEngine::Windows::Inputs::IMouseEventCallback, public ZEngine::Windows::Inputs::ITextInputEventCallback
    {
        ZUILayer(cstring name = "ZUI Layer") : ZEngine::Applications::Layer(name) {}

        void Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, ZEngine::Applications::GameApplicationPtr app) override;
        void Deinitialize() override {}

        bool OnEvent(ZEngine::Core::CoreEvent& event) override;
        void Update(ZEngine::Core::TimeStep dt) override {}
        void Render(ZEngine::Rendering::Renderers::GraphicRenderer* const, ZEngine::Hardwares::CommandBuffer* const) override;

        bool OnKeyPressed(ZEngine::Windows::Events::KeyPressedEvent&) override;
        bool OnKeyReleased(ZEngine::Windows::Events::KeyReleasedEvent&) override;

        bool OnMouseButtonPressed(ZEngine::Windows::Events::MouseButtonPressedEvent&) override;
        bool OnMouseButtonReleased(ZEngine::Windows::Events::MouseButtonReleasedEvent&) override;
        bool OnMouseButtonMoved(ZEngine::Windows::Events::MouseButtonMovedEvent&) override;
        bool OnMouseButtonWheelMoved(ZEngine::Windows::Events::MouseButtonWheelEvent&) override;

        bool OnTextInputRaised(ZEngine::Windows::Events::TextInputEvent&) override;

        void AddComponent(Components::ZUIComponent* cmp);

    private:
        static constexpr uint32_t kMaxComponents               = 32;
        ZEngine::UI::ZUIContext*  m_ctx                        = nullptr;
        Components::ZUIComponent* m_components[kMaxComponents] = {};
        uint32_t                  m_component_count            = 0;
    };
    ZDEFINE_PTR(ZUILayer);
} // namespace Tetragrama::Layers
