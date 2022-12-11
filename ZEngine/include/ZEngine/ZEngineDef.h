#pragma once
#include <memory>
#include <stdlib.h>

#define BIT(x) (1 << (x))
#define ZENGINE_EXIT_FAILURE() exit(EXIT_FAILURE);

#define ZENGINE_OPENGL_WINDOW GLFWWindow::OpenGLWindow
#define ZENGINE_OPENGL_CONTEXT GLFWGraphic::OpenGLContext

#define ZENGINE_KEYCODE ZEngine::Inputs::GlfwKeyCode

namespace ZEngine
{

    template <typename T>
    using Ref = std::shared_ptr<T>;

    template <typename T>
    using WeakRef = std::weak_ptr<T>;

    template <typename T>
    using Scope = std::unique_ptr<T>;

    template <typename T, typename... Args>
    Ref<T> CreateRef(Args&&... args)
    {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    Scope<T> CreateScope(Args&&... args)
    {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }
} // namespace ZEngine


#include "Logging/LoggerDefinition.h"

#define ZENGINE_VALIDATE_ASSERT(condition, message) \
    {                                               \
        if (!(condition))                           \
        {                                           \
            ZENGINE_CORE_CRITICAL(message)          \
            __debugbreak();                         \
        }                                           \
    }
