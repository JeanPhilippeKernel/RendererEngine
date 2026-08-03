#pragma once
#include <GLFW/glfw3.h>
#include <ZEngine/Windows/Inputs/IDevice.h>
#include <array>

namespace ZEngine::Windows::Inputs
{

    struct Mouse : public IDevice
    {
        Mouse(const char* name = "mouse_device") : IDevice(name) {}

        virtual bool IsKeyPressed(ZENGINE_KEYCODE key, CoreWindow* const window) const override
        {
            auto state = glfwGetMouseButton(reinterpret_cast<GLFWwindow*>(window->GetNativeWindow()), (int) key);
            return state == GLFW_PRESS;
        }

        virtual bool IsKeyReleased(ZENGINE_KEYCODE key, CoreWindow* const window) const override
        {
            return !IsKeyPressed(key, window);
        }

        std::array<double, 2> GetMousePosition(CoreWindow* const window) const
        {
            double x, y;
            glfwGetCursorPos(reinterpret_cast<GLFWwindow*>(window->GetNativeWindow()), &x, &y);
            return std::array<double, 2>{x, y};
        }
    };
} // namespace ZEngine::Windows::Inputs
