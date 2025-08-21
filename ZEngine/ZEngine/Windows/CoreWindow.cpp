#include <pch.h>
#include <CoreWindow.h>

using namespace ZEngine::Helpers;

namespace ZEngine::Windows
{
    CoreWindow::CoreWindow(const WindowConfiguration& cfg) : m_configuration(cfg) {}

    CoreWindow::~CoreWindow() {}
} // namespace ZEngine::Windows
