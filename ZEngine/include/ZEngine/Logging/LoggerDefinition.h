#pragma once 
#include <Logging/Logger.h>

#define Z_ENGINE_CORE_INFO(...)     ::ZEngine::Logging::Logger::GetCurrent()->info(__VA_ARGS__)
#define Z_ENGINE_CORE_TRACE(...)    ::ZEngine::Logging::Logger::GetCurrent()->trace(__VA_ARGS__)
#define Z_ENGINE_CORE_WARN(...)     ::ZEngine::Logging::Logger::GetCurrent()->warn(__VA_ARGS__)
#define Z_ENGINE_CORE_ERROR(...)    ::ZEngine::Logging::Logger::GetCurrent()->error(__VA_ARGS__)
#define Z_ENGINE_CORE_CRITICAL(...) ::ZEngine::Logging::Logger::GetCurrent()->critical(__VA_ARGS__)