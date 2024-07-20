#pragma once
#include <memory>
#include <stdlib.h>
#include <Helpers/IntrusivePtr.h>

#define BIT(x) (1 << (x))
#define ZENGINE_EXIT_FAILURE() exit(EXIT_FAILURE);


#define ZENGINE_KEYCODE ZEngine::Inputs::GlfwKeyCode

namespace ZEngine
{
    template <typename T>
    using Ref = Helpers::IntrusivePtr<T>;

    template <typename T>
    using WeakRef = Helpers::IntrusiveWeakPtr<T>;

    template <typename T>
    using Scope = std::unique_ptr<T>;

    template <typename T, typename... Args>
    Ref<T> CreateRef(Args&&... args)
    {
        return Helpers::make_intrusive<T>(std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    Scope<T> CreateScope(Args&&... args)
    {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }
} // namespace ZEngine


#include "Logging/LoggerDefinition.h"

#ifdef _MSC_VER
    #define ZENGINE_DEBUG_BREAK() __debugbreak();
#elif defined(__APPLE__)
    #include <signal.h>
    #define ZENGINE_DEBUG_BREAK() __builtin_trap()
#else
    #error "Platform not supported!"
#endif


#define ZENGINE_VALIDATE_ASSERT(condition, message) \
    {                                               \
        if (!(condition))                           \
        {                                           \
            ZENGINE_CORE_CRITICAL(message)          \
            assert(condition && message);           \
            ZENGINE_DEBUG_BREAK()                   \
        }                                           \
    }

#define ZENGINE_DESTROY_VULKAN_HANDLE(device, function, handle, ...) \
    if (device && handle)                                            \
    {                                                                \
        function(device, handle, __VA_ARGS__);                       \
        handle = nullptr;                                            \
    }                                                                \

#define SINGLE_ARG(...) __VA_ARGS__