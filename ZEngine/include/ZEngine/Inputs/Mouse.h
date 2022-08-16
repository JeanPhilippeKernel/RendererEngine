#pragma once
#include <array>
#include <Inputs/IDevice.h>
#ifdef ZENGINE_KEY_MAPPING_SDL
#include <SDL2/include/SDL_mouse.h>
#else
#include <GLFW/glfw3.h>
#endif

namespace ZEngine::Inputs {

    class Mouse : public IDevice {
    public:
        Mouse(const char* name = "mouse_device") : IDevice(name) {}

        virtual bool IsKeyPressed(ZENGINE_KEYCODE key, const Ref<Window::CoreWindow>& window) const override {
#ifdef ZENGINE_KEY_MAPPING_SDL
            bool           is_pressed{false};
            const uint32_t state = SDL_GetMouseState(NULL, NULL);
            unsigned int   mask  = 0;

            switch (key) {
            case KeyCode::MOUSE_LEFT:
                mask = SDL_BUTTON_LMASK;
                break;
            case KeyCode::MOUSE_WHEEL:
                mask = SDL_BUTTON_MMASK;
                break;
            case KeyCode::MOUSE_RIGHT:
                mask = SDL_BUTTON_RMASK;
                break;
            default:
                break;
            }

            is_pressed = (state & mask) >= 1 ? true : false;
            return is_pressed;
#else
            auto state = glfwGetMouseButton(static_cast<GLFWwindow*>(window->GetNativeWindow()), (int) key);
            return state == GLFW_PRESS;
#endif
        }

        virtual bool IsKeyReleased(ZENGINE_KEYCODE key, const Ref<Window::CoreWindow>& window) const override {
            return !IsKeyPressed(key, window);
        }

        std::array<double, 2> GetMousePosition(const Ref<Window::CoreWindow>& window) const {
            double x, y;
#ifdef ZENGINE_KEY_MAPPING_SDL
            const auto state = SDL_GetMouseState(&x, &y);
#else
            glfwGetCursorPos(static_cast<GLFWwindow*>(window->GetNativeWindow()), &x, &y);
#endif
            return std::array<double, 2>{x, y};
        }
        // virtual bool IsKeyPressed(ZENGINE_KEYCODE key, const Ref<Window::CoreWindow>& window) const override {
        // 	auto state = glfwGetMouseButton(static_cast<GLFWwindow*>(window->GetNativeWindow()), (int)key);
        // 	return state == GLFW_PRESS;
        // }

        // virtual bool IsKeyReleased(ZENGINE_KEYCODE key, const Ref<Window::CoreWindow>& window) const override {
        // 	auto state = glfwGetMouseButton(static_cast<GLFWwindow*>(window->GetNativeWindow()), (int)key);
        // 	return state == GLFW_RELEASE;
        // }

        // std::array<double, 2> GetMousePosition(const Ref<Window::CoreWindow>& window) const {
        // 	double x, y;
        // 	glfwGetCursorPos(static_cast<GLFWwindow*>(window->GetNativeWindow()), &x, &y);
        // 	return std::array<double, 2> {x, y};
        // }
    };
} // namespace ZEngine::Inputs
