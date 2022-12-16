#include <pch.h>
#include <Window/CoreWindow.h>
#include <ZEngineDef.h>

#include <Window/GlfwWindow/VulkanWindow.h>

#include <ZEngine/Engine.h>

using namespace ZEngine;
using namespace ZEngine::Event;
using namespace ZEngine::Layers;

namespace ZEngine::Window
{
    const char* CoreWindow::ATTACHED_PROPERTY = "WINDOW_ATTACHED_PROPERTY";

    CoreWindow::CoreWindow()
    {
        m_layer_stack_ptr = CreateScope<LayerStack>();
    }

    CoreWindow::~CoreWindow()
    {
    }

    void CoreWindow::SetAttachedEngine(Engine* const engine)
    {
        m_engine = engine;
    }

    ZEngine::Engine* CoreWindow::GetAttachedEngine()
    {
        assert(m_engine != nullptr);
        return m_engine;
    }

    void CoreWindow::PushOverlayLayer(const Ref<Layer>& layer)
    {
        m_layer_stack_ptr->PushOverlayLayer(layer);
    }

    void CoreWindow::PushLayer(const Ref<Layer>& layer)
    {
        m_layer_stack_ptr->PushLayer(layer);
    }

    void CoreWindow::PushOverlayLayer(Ref<Layer>&& layer)
    {
        m_layer_stack_ptr->PushOverlayLayer(std::move(layer));
    }

    void CoreWindow::PushLayer(Ref<Layer>&& layer)
    {
        m_layer_stack_ptr->PushLayer(std::move(layer));
    }

    void CoreWindow::ForwardEventToLayers(CoreEvent& event)
    {
        for (auto it = m_layer_stack_ptr->rbegin(); it != m_layer_stack_ptr->rend(); ++it)
        {
            if (event.IsHandled())
            {
                break;
            }
            it->get()->OnEvent(event);
        }
    }

    CoreWindow* Create(const WindowConfiguration& configuration, ZEngine::Engine& engine)
    {
        WindowProperty prop = {};
        prop.Height         = configuration.Height;
        prop.Width          = configuration.Width;
        prop.Title          = configuration.Title;
        prop.VSync          = configuration.EnableVsync;

        auto core_window = new GLFWWindow::VulkanWindow(prop, engine.GetVulkanInstance());
        core_window->SetCallbackFunction(std::bind(&CoreWindow::OnEvent, core_window, std::placeholders::_1));
        return core_window;
    }
} // namespace ZEngine::Window
