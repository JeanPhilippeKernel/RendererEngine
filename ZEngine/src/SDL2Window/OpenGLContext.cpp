#include <pch.h>
#include <Rendering/Graphics/SDLGraphic/OpenGLContext.h>

namespace ZEngine::Rendering::Graphics::SDLGraphic {
	void OpenGLContext::MarkActive() {

		auto native_window = static_cast<SDL_Window*> (m_window->GetNativeWindow());
		if (native_window != nullptr) {
			SDL_GL_MakeCurrent(native_window, m_native_context);
		}
	}
}

