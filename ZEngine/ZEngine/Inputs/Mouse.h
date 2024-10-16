#pragma once
#include <GLFW/glfw3.h>
#include <Inputs/IDevice.h>
#include <array>

namespace ZEngine::Inputs
{

    class Mouse : public IDevice
    {
    public:
        Mouse(const char* name = "mouse_device") : IDevice(name) {}

        virtual bool IsKeyPressed(ZENGINE_KEYCODE key, const Ref<Window::CoreWindow>& window) const override
        {

            auto state = glfwGetMouseButton(static_cast<GLFWwindow*>(window->GetNativeWindow()), (int) key);
            return state == GLFW_PRESS;
        }

        virtual bool IsKeyReleased(ZENGINE_KEYCODE key, const Ref<Window::CoreWindow>& window) const override
        {
            return !IsKeyPressed(key, window);
        }

        std::array<double, 2> GetMousePosition(const Ref<Window::CoreWindow>& window) const
        {
            double x, y;
            glfwGetCursorPos(static_cast<GLFWwindow*>(window->GetNativeWindow()), &x, &y);
            return std::array<double, 2>{x, y};
        }
    };
} // namespace ZEngine::Inputs
