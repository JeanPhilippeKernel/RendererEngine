#pragma once
#include <memory>
#include <stdlib.h>

#define BIT(x) (1 << (x))
#define ZENGINE_EXIT_FAILURE() exit(EXIT_FAILURE)

#ifdef __linux__
#define ZENGINE_WINDOW_SDL
#endif

#ifdef ZENGINE_WINDOW_SDL
#define ZENGINE_KEY_MAPPING_SDL
#define ZENGINE_OPENGL_WINDOW SDLWin::OpenGLWindow
#define ZENGINE_OPENGL_CONTEXT SDLGraphic::OpenGLContext
#else
#define ZENGINE_OPENGL_WINDOW GLFWWindow::OpenGLWindow
#define ZENGINE_OPENGL_CONTEXT GLFWGraphic::OpenGLContext
#endif

#ifdef ZENGINE_KEY_MAPPING_SDL
#define ZENGINE_KEYCODE ZEngine::Inputs::KeyCode
#else
#define ZENGINE_KEYCODE ZEngine::Inputs::GlfwKeyCode
#endif

namespace ZEngine {

    template <typename T>
    using Ref = std::shared_ptr<T>;

    template <typename T>
    using WeakRef = std::weak_ptr<T>;

    template <typename T>
    using Scope = std::unique_ptr<T>;
} // namespace ZEngine
