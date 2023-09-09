#pragma once
#include <string>
#include <vector>
#include <string_view>
#include <Event/CoreEvent.h>
#include <ZEngineDef.h>
#include <Core/TimeStep.h>
#include <Window/CoreWindow.h>
#include <Core/IInitializable.h>
#include <Core/IEventable.h>
#include <Core/IRenderable.h>
#include <Core/IUpdatable.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Window
{
    class CoreWindow;
}

namespace ZEngine::Layers
{

    class Layer : public Core::IInitializable, public Core::IUpdatable, public Core::IEventable, public Core::IRenderable
    {

    public:
        Layer(std::string_view name = "default_layer") : m_name(name) {}

        virtual ~Layer() = default;

        std::string_view GetName() const
        {
            return m_name;
        }

        void SetAttachedWindow(const ZEngine::Ref<Window::CoreWindow>& window)
        {
            m_window = window;
        }

        ZEngine::Ref<ZEngine::Window::CoreWindow> GetAttachedWindow() const
        {
            if (!m_window.expired())
                return m_window.lock();

            return nullptr;
        }

    protected:
        std::string                                   m_name;
        ZEngine::WeakRef<ZEngine::Window::CoreWindow> m_window;
    };
} // namespace ZEngine::Layers
