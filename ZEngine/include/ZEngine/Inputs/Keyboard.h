#pragma once
#include <Inputs/IDevice.h>

#ifdef ZENGINE_KEY_MAPPING_SDL
#include <SDL2/include/SDL_keyboard.h>
#else
#include <GLFW/glfw3.h>
#endif

namespace ZEngine::Inputs {

	class Keyboard : public IDevice {
	public:
		Keyboard(const char* name = "keyboard_device")
			: IDevice(name)
		{}
		~Keyboard() = default;

#ifdef ZENGINE_KEY_MAPPING_SDL
		virtual bool IsKeyPressed(ZENGINE_KEYCODE key) const override {
			auto state = SDL_GetKeyboardState(NULL);
			return *(state + (int)key) == 1;
		}

		virtual bool IsKeyReleased(ZENGINE_KEYCODE key) const override {
			auto state = SDL_GetKeyboardState(NULL);
			return *(state + (int)key) == 0;
		}
#else
		virtual bool IsKeyPressed(ZENGINE_KEYCODE key, const Ref<Window::CoreWindow>& window) const override {
			auto state = glfwGetKey(static_cast<GLFWwindow*>(window->GetNativeWindow()), (int)key);
			return state == GLFW_PRESS;
		}

		virtual bool IsKeyReleased(ZENGINE_KEYCODE key, const Ref<Window::CoreWindow>& window) const override {
			auto state = glfwGetKey(static_cast<GLFWwindow*>(window->GetNativeWindow()), (int)key);
			return state == GLFW_RELEASE;
		}
#endif
	};
}
