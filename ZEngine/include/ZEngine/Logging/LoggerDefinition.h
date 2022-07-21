#pragma once
#include <Logging/Logger.h>

#define ZENGINE_CORE_INFO(...)                                            \
    {                                                                     \
        ::ZEngine::Logging::Logger::GetLogger()->info(__VA_ARGS__);       \
        ::ZEngine::Logging::Logger::GetEngineLogger()->info(__VA_ARGS__); \
    }

#define ZENGINE_CORE_TRACE(...)                                            \
    {                                                                      \
        ::ZEngine::Logging::Logger::GetLogger()->trace(__VA_ARGS__);       \
        ::ZEngine::Logging::Logger::GetEngineLogger()->trace(__VA_ARGS__); \
    }

#define ZENGINE_CORE_WARN(...)                                            \
    {                                                                     \
        ::ZEngine::Logging::Logger::GetLogger()->warn(__VA_ARGS__);       \
        ::ZEngine::Logging::Logger::GetEngineLogger()->warn(__VA_ARGS__); \
    }

#define ZENGINE_CORE_ERROR(...)                                            \
    {                                                                      \
        ::ZEngine::Logging::Logger::GetLogger()->error(__VA_ARGS__);       \
        ::ZEngine::Logging::Logger::GetEngineLogger()->error(__VA_ARGS__); \
    }

#define ZENGINE_CORE_CRITICAL(...)                                            \
    {                                                                         \
        ::ZEngine::Logging::Logger::GetLogger()->critical(__VA_ARGS__);       \
        ::ZEngine::Logging::Logger::GetEngineLogger()->critical(__VA_ARGS__); \
    }

#define ZENGINE_EDITOR_INFO(...)                                          \
    {                                                                     \
        ::ZEngine::Logging::Logger::GetLogger()->info(__VA_ARGS__);       \
        ::ZEngine::Logging::Logger::GetEditorLogger()->info(__VA_ARGS__); \
    }

#define ZENGINE_EDITOR_TRACE(...)                                          \
    {                                                                      \
        ::ZEngine::Logging::Logger::GetLogger()->trace(__VA_ARGS__);       \
        ::ZEngine::Logging::Logger::GetEditorLogger()->trace(__VA_ARGS__); \
    }

#define ZENGINE_EDITOR_WARN(...)                                          \
    {                                                                     \
        ::ZEngine::Logging::Logger::GetLogger()->warn(__VA_ARGS__);       \
        ::ZEngine::Logging::Logger::GetEditorLogger()->warn(__VA_ARGS__); \
    }

#define ZENGINE_EDITOR_ERROR(...)                                          \
    {                                                                      \
        ::ZEngine::Logging::Logger::GetLogger()->error(__VA_ARGS__);       \
        ::ZEngine::Logging::Logger::GetEditorLogger()->error(__VA_ARGS__); \
    }

#define ZENGINE_EDITOR_CRITICAL(...)                                          \
    {                                                                         \
        ::ZEngine::Logging::Logger::GetLogger()->critical(__VA_ARGS__);       \
        ::ZEngine::Logging::Logger::GetEditorLogger()->critical(__VA_ARGS__); \
    }
