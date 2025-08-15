#pragma once
#include <GLFW/glfw3.h>
#include <IDevice.h>

namespace ZEngine::Windows::Inputs
{

    struct Keyboard : public IDevice
    {
        Keyboard(const char* name = "keyboard_device") : IDevice(name) {}
        ~Keyboard() = default;

        virtual bool IsKeyPressed(ZENGINE_KEYCODE key, CoreWindow* const window) const override
        {
            auto state = glfwGetKey(reinterpret_cast<GLFWwindow*>(window->GetNativeWindow()), (int) key);
            return state == GLFW_PRESS;
        }

        virtual bool IsKeyReleased(ZENGINE_KEYCODE key, CoreWindow* const window) const override
        {
            auto state = glfwGetKey(reinterpret_cast<GLFWwindow*>(window->GetNativeWindow()), (int) key);
            return state == GLFW_RELEASE;
        }
    };
} // namespace ZEngine::Windows::Inputs
