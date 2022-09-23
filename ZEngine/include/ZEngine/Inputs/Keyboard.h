#pragma once
#include <Inputs/IDevice.h>

#include <GLFW/glfw3.h>

namespace ZEngine::Inputs {

    class Keyboard : public IDevice {
    public:
        Keyboard(const char* name = "keyboard_device") : IDevice(name) {}
        ~Keyboard() = default;

        virtual bool IsKeyPressed(ZENGINE_KEYCODE key, const Ref<Window::CoreWindow>& window) const override {

            auto state = glfwGetKey(static_cast<GLFWwindow*>(window->GetNativeWindow()), (int) key);
            return state == GLFW_PRESS;

        }

        virtual bool IsKeyReleased(ZENGINE_KEYCODE key, const Ref<Window::CoreWindow>& window) const override {

            auto state = glfwGetKey(static_cast<GLFWwindow*>(window->GetNativeWindow()), (int) key);
            return state == GLFW_RELEASE;
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
