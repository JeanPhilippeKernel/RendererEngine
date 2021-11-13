#pragma once
#include <SDL2/include/SDL.h>

#include <Rendering/Graphics/GraphicContext.h>

using namespace ZEngine::Rendering::Graphics;

namespace ZEngine::Rendering::Graphics::SDLGraphic {

	class OpenGLContext : public GraphicContext {
	public:
		OpenGLContext() = default;
		OpenGLContext(const CoreWindow* window)
			:GraphicContext(window)
		{
			auto native_window = static_cast<SDL_Window*>(m_window->GetNativeWindow());
			m_native_context = SDL_GL_CreateContext(native_window);
		}

		~OpenGLContext() {
			SDL_GL_DeleteContext(m_native_context);
		}

		virtual void* GetNativeContext() const override { return m_native_context; }

		virtual void MarkActive() override;

	private:
		SDL_GLContext m_native_context{};
	};

}