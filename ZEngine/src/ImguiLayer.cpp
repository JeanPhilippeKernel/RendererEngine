#include <pch.h>
#include <Layers/ImguiLayer.h>
#include <Logging/LoggerDefinition.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Window/GlfwWindow/VulkanWindow.h>
#include <ZEngineDef.h>
#include <fmt/format.h>

using namespace ZEngine::Rendering::Renderers;

namespace ZEngine::Layers
{
    bool ImguiLayer::m_initialized = false;

    ImguiLayer::~ImguiLayer() {}

    void ImguiLayer::Initialize() {}

    void ImguiLayer::Deinitialize()
    {
        m_ui_components.clear();
        m_ui_components.shrink_to_fit();
    }

    bool ImguiLayer::OnEvent(Event::CoreEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);

        event_dispatcher.Dispatch<Event::KeyPressedEvent>(std::bind(&ImguiLayer::OnKeyPressed, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::KeyReleasedEvent>(std::bind(&ImguiLayer::OnKeyReleased, this, std::placeholders::_1));

        event_dispatcher.Dispatch<Event::MouseButtonPressedEvent>(std::bind(&ImguiLayer::OnMouseButtonPressed, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::MouseButtonReleasedEvent>(std::bind(&ImguiLayer::OnMouseButtonReleased, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::MouseButtonMovedEvent>(std::bind(&ImguiLayer::OnMouseButtonMoved, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::MouseButtonWheelEvent>(std::bind(&ImguiLayer::OnMouseButtonWheelMoved, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Event::TextInputEvent>(std::bind(&ImguiLayer::OnTextInputRaised, this, std::placeholders::_1));

        event_dispatcher.Dispatch<Event::WindowClosedEvent>(std::bind(&ImguiLayer::OnWindowClosed, this, std::placeholders::_1));

        return false;
    }

    void ImguiLayer::Update(Core::TimeStep dt)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (dt > 0.0f)
        {
            io.DeltaTime = dt;
        }

        for (const auto& component : m_ui_components)
        {
            component->Update(dt);
        }
    }

    void ImguiLayer::AddUIComponent(const Ref<Components::UI::UIComponent>& component)
    {
        m_ui_components.push_back(component);

        if (!component->HasParentLayer())
        {
            auto last = std::prev(std::end(m_ui_components));
            (*last)->SetParentLayer(this);
        }
    }

    void ImguiLayer::AddUIComponent(Ref<Components::UI::UIComponent>&& component)
    {
        if (!component->HasParentLayer())
        {
            component->SetParentLayer(this);
        }
        m_ui_components.push_back(component);
    }

    void ImguiLayer::AddUIComponent(std::vector<Ref<Components::UI::UIComponent>>&& components)
    {
        std::for_each(std::begin(components), std::end(components), [this](Ref<Components::UI::UIComponent>& component) {
            if (!component->HasParentLayer())
            {
                component->SetParentLayer(this);
            }
        });

        std::move(std::begin(components), std::end(components), std::back_inserter(m_ui_components));
    }

    bool ImguiLayer::OnKeyPressed(Event::KeyPressedEvent& e)
    {
        ImGuiIO& io                       = ImGui::GetIO();
        io.KeysDown[(int) e.GetKeyCode()] = true;
        return false;
    }

    bool ImguiLayer::OnKeyReleased(Event::KeyReleasedEvent& e)
    {
        ImGuiIO& io                       = ImGui::GetIO();
        io.KeysDown[(int) e.GetKeyCode()] = false;
        return false;
    }

    bool ImguiLayer::OnMouseButtonPressed(Event::MouseButtonPressedEvent& e)
    {
        ImGuiIO& io                            = ImGui::GetIO();
        io.MouseDown[(uint32_t) e.GetButton()] = true;
        return false;
    }

    bool ImguiLayer::OnMouseButtonReleased(Event::MouseButtonReleasedEvent& e)
    {
        ImGuiIO& io                       = ImGui::GetIO();
        io.MouseDown[(int) e.GetButton()] = false;
        return false;
    }

    bool ImguiLayer::OnMouseButtonMoved(Event::MouseButtonMovedEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.MousePos = ImVec2(float(e.GetPosX()), float(e.GetPosY()));
        return false;
    }

    bool ImguiLayer::OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (e.GetOffetX() > 0)
        {
            io.MouseWheelH += 1;
        }
        if (e.GetOffetX() < 0)
        {
            io.MouseWheelH -= 1;
        }
        if (e.GetOffetY() > 0)
        {
            io.MouseWheel += 1;
        }
        if (e.GetOffetY() < 0)
        {
            io.MouseWheel -= 1;
        }
        return false;
    }

    bool ImguiLayer::OnTextInputRaised(Event::TextInputEvent& event)
    {
        ImGuiIO& io = ImGui::GetIO();
        for (unsigned char c : event.GetText())
        {
            io.AddInputCharacter(c);
        }
        return false;
    }

    bool ImguiLayer::OnWindowClosed(Event::WindowClosedEvent& event)
    {
        Event::EventDispatcher event_dispatcher(event);
        event_dispatcher.ForwardTo<Event::WindowClosedEvent>(std::bind(&ZEngine::Window::CoreWindow::OnWindowClosed, GetAttachedWindow().get(), std::placeholders::_1));
        return true;
    }

    bool ImguiLayer::OnWindowResized(Event::WindowResizedEvent&)
    {
        return false;
    }

    bool ImguiLayer::OnWindowMinimized(Event::WindowMinimizedEvent&)
    {
        return false;
    }

    bool ImguiLayer::OnWindowMaximized(Event::WindowMaximizedEvent&)
    {
        return false;
    }

    bool ImguiLayer::OnWindowRestored(Event::WindowRestoredEvent&)
    {
        return false;
    }

    void ImguiLayer::Render()
    {
        if (auto window_ptr = m_window.lock())
        {
            if (window_ptr->IsMinimized())
            {
                return;
            }

            GraphicRenderer::BeginImguiFrame();

            for (const auto& component : m_ui_components)
            {
                if (component->GetVisibility() == true)
                {
                    component->Render();
                }
            }
            GraphicRenderer::DrawUIFrame();
            GraphicRenderer::EndImguiFrame();
        }
    }
} // namespace ZEngine::Layers
