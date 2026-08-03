#pragma once
#include <ZEngine/Logging/LoggerConfiguration.h>
#include <ZEngine/Windows/WindowConfiguration.h>

namespace ZEngine
{

    struct EngineConfiguration
    {
        Logging::LoggerConfiguration LoggerConfiguration;
        Windows::WindowConfiguration WindowConfiguration;
    };

} // namespace ZEngine
