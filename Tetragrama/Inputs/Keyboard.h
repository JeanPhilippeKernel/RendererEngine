#pragma once
#include <GLFW/glfw3.h>
#include <ZEngine/Windows/Inputs/IDevice.h>

namespace Tetragrama::Inputs
{

    class Keyboard : public ZEngine::Windows::Inputs::IDevice
    {
    public:
        Keyboard(const char* name = "keyboard_device") : ZEngine::Windows::Inputs::IDevice(name) {}
        ~Keyboard() = default;

        virtual bool IsKeyPressed(ZENGINE_KEYCODE key, const ZEngine::Ref<ZEngine::Windows::CoreWindow>& window) const override
        {
            auto state = glfwGetKey(reinterpret_cast<GLFWwindow*>(window->GetNativeWindow()), (int) key);
            return state == GLFW_PRESS;
        }

        virtual bool IsKeyReleased(ZENGINE_KEYCODE key, const ZEngine::Ref<ZEngine::Windows::CoreWindow>& window) const override
        {
            auto state = glfwGetKey(reinterpret_cast<GLFWwindow*>(window->GetNativeWindow()), (int) key);
            return state == GLFW_RELEASE;
        }
    };
} // namespace Tetragrama::Inputs
