#include <Tetragrama/Components/ZUI/ZUIComponent.h>
#include <Tetragrama/Layers/ZUILayer.h>
#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Core/EventDispatcher.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <ZEngine/Windows/Events/KeyEvent.h>
#include <ZEngine/Windows/Inputs/KeyCodeDefinition.h>
#include <GLFW/glfw3.h>
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

        // Root box — fully opaque background covering the full swapchain surface.
        // This is critical: the scene render pass leaves bloom/HDR data in the
        // swapchain; without an opaque root the scene bleeds through semi-transparent
        // panel backgrounds. ImGui solves this the same way with its main DockSpace
        // window background.
        ZEngine::UI::ZUIBox* root = ZEngine::UI::ZUIBeginColumn(m_ctx, "##zui_root",
                                                                  ZEngine::UI::ZPx((float)m_ctx->ScreenW),
                                                                  ZEngine::UI::ZPx((float)m_ctx->ScreenH));
        root->Flags = root->Flags | ZEngine::UI::ZUI_DrawBackground;
        ZUIBoxSetColor(root, 0.02f, 0.02f, 0.02f, 1.0f); // fully opaque — covers anything in the swapchain

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
        auto key = e.GetKeyCode();

        // Modifier tracking
        if (key == ZENGINE_KEY_LEFT_CONTROL  || key == ZENGINE_KEY_RIGHT_CONTROL)
            m_ctx->CtrlDown  = true;
        if (key == ZENGINE_KEY_LEFT_SHIFT    || key == ZENGINE_KEY_RIGHT_SHIFT)
            m_ctx->ShiftDown = true;
        if (key == ZENGINE_KEY_LEFT_ALT      || key == ZENGINE_KEY_RIGHT_ALT)
            m_ctx->AltDown   = true;

        // Backspace (also starts key-repeat timer)
        if (key == ZENGINE_KEY_BACKSPACE)
        {
            m_ctx->BackspacePressed = true;
            m_ctx->BackspaceHeld    = true;
            m_ctx->KeyRepeatTimer   = 0.f;
        }

        // Clipboard paste: Ctrl+V → inject clipboard text into TextInput
        if (m_ctx->CtrlDown && key == ZEngine::Windows::Inputs::GlfwKey::KEY_V)
        {
            if (CurrentApp && CurrentApp->CurrentWindow)
            {
                auto* native = static_cast<GLFWwindow*>(
                    CurrentApp->CurrentWindow->GetNativeWindow());
                const char* clip = glfwGetClipboardString(native);
                if (clip)
                {
                    for (uint32_t i = 0; clip[i] && m_ctx->TextInputLen < 31; ++i)
                        m_ctx->TextInput[m_ctx->TextInputLen++] = clip[i];
                    m_ctx->TextInput[m_ctx->TextInputLen] = '\0';
                }
            }
        }

        return false;
    }

    bool ZUILayer::OnKeyReleased(KeyReleasedEvent& e)
    {
        if (!m_ctx) { return false; }
        auto key = e.GetKeyCode();
        if (key == ZENGINE_KEY_LEFT_CONTROL  || key == ZENGINE_KEY_RIGHT_CONTROL)
            m_ctx->CtrlDown  = false;
        if (key == ZENGINE_KEY_LEFT_SHIFT    || key == ZENGINE_KEY_RIGHT_SHIFT)
            m_ctx->ShiftDown = false;
        if (key == ZENGINE_KEY_LEFT_ALT      || key == ZENGINE_KEY_RIGHT_ALT)
            m_ctx->AltDown   = false;
        if (key == ZENGINE_KEY_BACKSPACE)
        {
            m_ctx->BackspaceHeld  = false;
            m_ctx->KeyRepeatTimer = 0.f;
        }
        return false;
    }

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
