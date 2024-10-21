#pragma once
#include <Logging/LoggerConfiguration.h>
#include <Windows/WindowConfiguration.h>

namespace ZEngine
{

    struct EngineConfiguration
    {
        Logging::LoggerConfiguration LoggerConfiguration;
        Windows::WindowConfiguration WindowConfiguration;
    };

} // namespace ZEngine
