#include <pch.h>
#include <CoreWindow.h>

using namespace ZEngine::Windows::Layers;
using namespace ZEngine::Helpers;

namespace ZEngine::Windows
{
    CoreWindow::CoreWindow(const WindowConfiguration& cfg) : m_configuration(cfg) {}

    CoreWindow::~CoreWindow() {}

    void CoreWindow::ForwardEventToLayers(Core::CoreEvent& event)
    {
        for (auto layer : m_configuration.OverlayLayerCollection)
        {
            if (event.IsHandled())
            {
                break;
            }
            layer->OnEvent(event);
        }

        for (auto layer : m_configuration.RenderingLayerCollection)
        {
            if (event.IsHandled())
            {
                break;
            }
            layer->OnEvent(event);
        }
    }
} // namespace ZEngine::Windows
