#include <pch.h>
#include <CoreWindow.h>

using namespace ZEngine::Windows::Layers;

namespace ZEngine::Windows
{
    CoreWindow::CoreWindow()
    {
        m_layer_stack_ptr = CreateScope<LayerStack>();
    }

    CoreWindow::~CoreWindow() {}

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

    void CoreWindow::ForwardEventToLayers(Core::CoreEvent& event)
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
} // namespace ZEngine::Windows
