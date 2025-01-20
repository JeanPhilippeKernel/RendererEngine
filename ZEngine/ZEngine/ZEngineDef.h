#pragma once
#include <Logging/LoggerDefinition.h>

#define BIT(x)                 (1 << (x))
#define ZENGINE_EXIT_FAILURE() exit(EXIT_FAILURE);

#define ZENGINE_KEYCODE        ZEngine::Windows::Inputs::GlfwKeyCode

#ifdef _MSC_VER
#define ZENGINE_DEBUG_BREAK() __debugbreak();
#elif defined(__APPLE__)
#include <signal.h>
#define ZENGINE_DEBUG_BREAK() __builtin_trap();
#else
#error "Platform not supported!"
#endif

#define ZENGINE_VALIDATE_ASSERT(condition, message) \
    {                                               \
        if (!(condition))                           \
        {                                           \
            ZENGINE_CORE_CRITICAL(message)          \
            assert(condition&& message);            \
            ZENGINE_DEBUG_BREAK()                   \
        }                                           \
    }

#define ZENGINE_DESTROY_VULKAN_HANDLE(device, function, handle, ...) \
    if (device && handle)                                            \
    {                                                                \
        function(device, handle, __VA_ARGS__);                       \
        handle = nullptr;                                            \
    }

#define ZENGINE_CLEAR_STD_VECTOR(collection) \
    if (!collection.empty())                 \
    {                                        \
        collection.clear();                  \
        collection.shrink_to_fit();          \
    }

#define SINGLE_ARG(...)     __VA_ARGS__

#define MAX_FILE_PATH_COUNT 256
