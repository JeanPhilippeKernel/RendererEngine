#include <Rendering/Graphics/GlfwGraphic/OpenGLContext.h>

namespace ZEngine::Rendering::Graphics::GLFWGraphic {
	void OpenGLContext::MarkActive() {
		auto native_window = static_cast<GLFWwindow*> (m_window->GetNativeWindow());
		if (native_window != nullptr) {
			glfwMakeContextCurrent(native_window);
		}
	}
}

