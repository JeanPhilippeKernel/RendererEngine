#pragma once
#include <GLFW/glfw3.h>
#include <ZEngine/Inputs/IDevice.h>
#include <array>

namespace Tetragrama::Inputs
{

    class Mouse : public ZEngine::Inputs::IDevice
    {
    public:
        Mouse(const char* name = "mouse_device") : ZEngine::Inputs::IDevice(name) {}

        virtual bool IsKeyPressed(ZENGINE_KEYCODE key, const ZEngine::Ref<ZEngine::Window::CoreWindow>& window) const override
        {

            auto state = glfwGetMouseButton(reinterpret_cast<GLFWwindow*>(window->GetNativeWindow()), (int) key);
            return state == GLFW_PRESS;
        }

        virtual bool IsKeyReleased(ZENGINE_KEYCODE key, const ZEngine::Ref<ZEngine::Window::CoreWindow>& window) const override
        {
            return !IsKeyPressed(key, window);
        }

        std::array<double, 2> GetMousePosition(const ZEngine::Ref<ZEngine::Window::CoreWindow>& window) const
        {
            double x, y;
            glfwGetCursorPos(reinterpret_cast<GLFWwindow*>(window->GetNativeWindow()), &x, &y);
            return std::array<double, 2>{x, y};
        }
    };
} // namespace Tetragrama::Inputs
