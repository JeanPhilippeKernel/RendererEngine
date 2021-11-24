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
        Keyboard(const char* name = "keyboard_device") : IDevice(name) {}
        ~Keyboard() = default;

        virtual bool IsKeyPressed(ZENGINE_KEYCODE key, const Ref<Window::CoreWindow>& window) const override {
#ifdef ZENGINE_KEY_MAPPING_SDL
            auto state = SDL_GetKeyboardState(NULL);
            return *(state + (int) key) == 1;
#else
            auto state = glfwGetKey(static_cast<GLFWwindow*>(window->GetNativeWindow()), (int) key);
            return state == GLFW_PRESS;
#endif
        }

        virtual bool IsKeyReleased(ZENGINE_KEYCODE key, const Ref<Window::CoreWindow>& window) const override {
#ifdef ZENGINE_KEY_MAPPING_SDL
            auto state = SDL_GetKeyboardState(NULL);
            return *(state + (int) key) == 0;
#else
            auto state = glfwGetKey(static_cast<GLFWwindow*>(window->GetNativeWindow()), (int) key);
            return state == GLFW_RELEASE;
#endif
        }

        // virtual bool IsKeyPressed(ZENGINE_KEYCODE key, const Ref<Window::CoreWindow>& window) const override {
        // 	auto state = glfwGetKey(static_cast<GLFWwindow*>(window->GetNativeWindow()), (int)key);
        // 	return state == GLFW_PRESS;
        // }

        // virtual bool IsKeyReleased(ZENGINE_KEYCODE key, const Ref<Window::CoreWindow>& window) const override {
        // 	auto state = glfwGetKey(static_cast<GLFWwindow*>(window->GetNativeWindow()), (int)key);
        // 	return state == GLFW_RELEASE;
        // }
    };
} // namespace ZEngine::Inputs
