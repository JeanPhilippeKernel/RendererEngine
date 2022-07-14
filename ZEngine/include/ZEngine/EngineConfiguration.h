#pragma once
#include <Logging/LoggerConfiguration.h>
#include <Window/WindowConfiguration.h>

namespace ZEngine {

    struct EngineConfiguration {
        Logging::LoggerConfiguration LoggerConfiguration;
        Window::WindowConfiguration  WindowConfiguration;
    };

} // namespace ZEngine
