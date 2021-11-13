#pragma once
#include <GLFW/glfw3.h>

#include <Rendering/Graphics/GraphicContext.h>

using namespace ZEngine::Rendering::Graphics;

namespace ZEngine::Rendering::Graphics::GLFWGraphic {

	class OpenGLContext : public GraphicContext {
	public:
		OpenGLContext() = default;
		OpenGLContext(const CoreWindow* window)
			:GraphicContext(window)
		{
		}

		~OpenGLContext() 
		{
		}

		virtual void* GetNativeContext() const override { return glfwGetCurrentContext(); }

		virtual void MarkActive() override;
	};

}