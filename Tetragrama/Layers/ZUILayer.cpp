#include <Tetragrama/Components/ZUI/ZUIComponent.h>
#include <Tetragrama/Layers/ZUILayer.h>
#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Core/EventDispatcher.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <ZEngine/Windows/Events/KeyEvent.h>
#include <ZEngine/Windows/Inputs/KeyCodeDefinition.h>
#include <ZEngine/Windows/Events/MouseEvent.h>
#include <ZEngine/Windows/Events/TextInputEvent.h>

using namespace ZEngine::Windows::Events;

namespace Tetragrama::Layers
{
    void ZUILayer::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena,
                              ZEngine::Applications::GameApplicationPtr app)
    {
        Arena      = arena;
        CurrentApp = app;
        m_ctx      = app->RenderPipeline ? app->RenderPipeline->ZUICtx : nullptr;
        // Components carved sub-arenas from LocalArena in their Initialize() calls.
        // Without this, LocalArena has no backing memory → CreateSubArena asserts.
        arena->CreateSubArena(ZMega(4), &LocalArena);
    }

    bool ZUILayer::OnEvent(ZEngine::Core::CoreEvent& event)
    {
        ZEngine::Core::EventDispatcher event_dispatcher(event);
        event_dispatcher.Dispatch<KeyPressedEvent>(std::bind(&ZUILayer::OnKeyPressed, this, std::placeholders::_1));
        event_dispatcher.Dispatch<KeyReleasedEvent>(std::bind(&ZUILayer::OnKeyReleased, this, std::placeholders::_1));
        event_dispatcher.Dispatch<MouseButtonPressedEvent>(std::bind(&ZUILayer::OnMouseButtonPressed, this, std::placeholders::_1));
        event_dispatcher.Dispatch<MouseButtonReleasedEvent>(std::bind(&ZUILayer::OnMouseButtonReleased, this, std::placeholders::_1));
        event_dispatcher.Dispatch<MouseButtonMovedEvent>(std::bind(&ZUILayer::OnMouseButtonMoved, this, std::placeholders::_1));
        event_dispatcher.Dispatch<MouseButtonWheelEvent>(std::bind(&ZUILayer::OnMouseButtonWheelMoved, this, std::placeholders::_1));
        event_dispatcher.Dispatch<TextInputEvent>(std::bind(&ZUILayer::OnTextInputRaised, this, std::placeholders::_1));
        return false;
    }

    void ZUILayer::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const,
                          ZEngine::Hardwares::CommandBuffer* const)
    {
        if (!m_ctx || m_component_count == 0) { return; }

        // Root box — transparent, explicitly sized to the swapchain surface.
        // ZFill() collapses to 0 when there is no parent; children that use
        // ZFill() need a non-zero parent size to expand into.
        ZEngine::UI::ZUIBox* root = ZEngine::UI::ZUIBeginColumn(m_ctx, "##zui_root",
                                                                  ZEngine::UI::ZPx((float)m_ctx->ScreenW),
                                                                  ZEngine::UI::ZPx((float)m_ctx->ScreenH));
        root->BgColor[3] = 0.f; // fully transparent — no background draw

        for (uint32_t i = 0; i < m_component_count; ++i)
        {
            m_components[i]->BuildUI(m_ctx);
        }

        ZEngine::UI::ZUIEndColumn(m_ctx);
    }

    void ZUILayer::AddComponent(Components::ZUIComponent* cmp)
    {
        if (m_component_count < kMaxComponents)
        {
            m_components[m_component_count++] = cmp;
        }
    }

    bool ZUILayer::OnKeyPressed(KeyPressedEvent& e)
    {
        if (!m_ctx) { return false; }
        if (e.GetKeyCode() == ZENGINE_KEY_BACKSPACE)
        {
            m_ctx->BackspacePressed = true;
        }
        return false;
    }
    bool ZUILayer::OnKeyReleased(KeyReleasedEvent&)   { return false; }

    bool ZUILayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        if (!m_ctx) { return false; }
        int btn = (int)e.GetButton();
        if (btn >= 0 && btn < 3)
        {
            m_ctx->MouseDown[btn]    = true;
            m_ctx->MousePressed[btn] = true;
        }
        return false;
    }

    bool ZUILayer::OnMouseButtonReleased(MouseButtonReleasedEvent& e)
    {
        if (!m_ctx) { return false; }
        int btn = (int)e.GetButton();
        if (btn >= 0 && btn < 3)
        {
            m_ctx->MouseDown[btn]     = false;
            m_ctx->MouseReleased[btn] = true;
        }
        return false;
    }

    bool ZUILayer::OnMouseButtonMoved(MouseButtonMovedEvent& e)
    {
        if (!m_ctx) { return false; }
        m_ctx->MousePos[0] = (float)e.GetPosX();
        m_ctx->MousePos[1] = (float)e.GetPosY();
        return false;
    }

    bool ZUILayer::OnMouseButtonWheelMoved(MouseButtonWheelEvent& e)
    {
        if (!m_ctx) { return false; }
        m_ctx->ScrollDelta += (float)e.GetOffetY();
        return false;
    }

    bool ZUILayer::OnTextInputRaised(TextInputEvent& e)
    {
        if (!m_ctx) { return false; }
        for (unsigned char c : e.GetText())
        {
            if (m_ctx->TextInputLen < 31)
            {
                m_ctx->TextInput[m_ctx->TextInputLen++] = (char)c;
            }
        }
        m_ctx->TextInput[m_ctx->TextInputLen] = '\0';
        return false;
    }
} // namespace Tetragrama::Layers
