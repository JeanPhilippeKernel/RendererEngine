#pragma once
#include <GLFW/glfw3.h>
#include <ZEngine/Windows/Inputs/IDevice.h>
#include <array>

namespace Tetragrama::Inputs
{

    class Mouse : public ZEngine::Windows::Inputs::IDevice
    {
    public:
        Mouse(const char* name = "mouse_device") : ZEngine::Windows::Inputs::IDevice(name) {}

        virtual bool IsKeyPressed(ZENGINE_KEYCODE key, const ZEngine::Ref<ZEngine::Windows::CoreWindow>& window) const override
        {

            auto state = glfwGetMouseButton(reinterpret_cast<GLFWwindow*>(window->GetNativeWindow()), (int) key);
            return state == GLFW_PRESS;
        }

        virtual bool IsKeyReleased(ZENGINE_KEYCODE key, const ZEngine::Ref<ZEngine::Windows::CoreWindow>& window) const override
        {
            return !IsKeyPressed(key, window);
        }

        std::array<double, 2> GetMousePosition(const ZEngine::Ref<ZEngine::Windows::CoreWindow>& window) const
        {
            double x, y;
            glfwGetCursorPos(reinterpret_cast<GLFWwindow*>(window->GetNativeWindow()), &x, &y);
            return std::array<double, 2>{x, y};
        }
    };
} // namespace Tetragrama::Inputs
