#include "OpenGLContext.h"


namespace Z_Engine::Rendering::Graphics {

	GraphicContext* CreateContext() {
		return new SDLGraphic::OpenGLContext();
	}

	GraphicContext* CreateContext(const CoreWindow* window) {
		return new SDLGraphic::OpenGLContext(window);
	}

}

namespace Z_Engine::Rendering::Graphics::SDLGraphic {
	void OpenGLContext::MarkActive() {

		auto native_window = static_cast<SDL_Window*> (m_window->GetNativeWindow());
		if (native_window != nullptr) {
			SDL_GL_MakeCurrent(native_window, m_native_context);
		}
	}
}

