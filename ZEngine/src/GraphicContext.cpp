#include <pch.h>
#include <ZEngineDef.h>

#ifdef ZENGINE_WINDOW_SDL
#include <Rendering/Graphics/SDLGraphic/OpenGLContext.h>
#else
#include <Rendering/Graphics/GlfwGraphic/OpenGLContext.h>
#endif

#include <Rendering/Graphics/GraphicContext.h>

namespace ZEngine::Rendering::Graphics {

    GraphicContext* CreateContext() {
        return new ZENGINE_OPENGL_CONTEXT();
    }

    GraphicContext* CreateContext(const ZEngine::Window::CoreWindow* window) {
        return new ZENGINE_OPENGL_CONTEXT(window);
    }
} // namespace ZEngine::Rendering::Graphics
