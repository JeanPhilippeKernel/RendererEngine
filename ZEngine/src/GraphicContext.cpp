#include <pch.h>
#include <ZEngineDef.h>


#include <Rendering/Graphics/GlfwGraphic/OpenGLContext.h>

#include <Rendering/Graphics/GraphicContext.h>

namespace ZEngine::Rendering::Graphics {

    GraphicContext* CreateContext() {
        return new ZENGINE_OPENGL_CONTEXT();
    }

    GraphicContext* CreateContext(const CoreWindow* window) {
        return new ZENGINE_OPENGL_CONTEXT(window);
    }
} // namespace ZEngine::Rendering::Graphics
