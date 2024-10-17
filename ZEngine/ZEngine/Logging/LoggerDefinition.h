#pragma once
#include <Logging/Logger.h>
#include <fmt/format.h>

#define ZENGINE_CORE_INFO(...) ::ZEngine::Logging::Logger::Info(fmt::format(__VA_ARGS__));

#define ZENGINE_CORE_TRACE(...) ::ZEngine::Logging::Logger::Trace(fmt::format(__VA_ARGS__));

#define ZENGINE_CORE_WARN(...) ::ZEngine::Logging::Logger::Warn(fmt::format(__VA_ARGS__));

#define ZENGINE_CORE_ERROR(...) ::ZEngine::Logging::Logger::Error(fmt::format(__VA_ARGS__));

#define ZENGINE_CORE_CRITICAL(...) ::ZEngine::Logging::Logger::Critical(fmt::format(__VA_ARGS__));\
