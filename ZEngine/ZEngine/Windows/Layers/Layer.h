#pragma once
#include <Core/IEventable.h>
#include <Core/IInitializable.h>
#include <Core/IRenderable.h>
#include <Core/IUpdatable.h>
#include <Windows/CoreWindow.h>
#include <string>

namespace ZEngine::Windows
{
    class CoreWindow;
}

namespace ZEngine::Windows::Layers
{

    class Layer : public Core::IInitializable, public Core::IUpdatable, public Core::IEventable, public Core::IRenderable, public Helpers::RefCounted
    {

    public:
        Layer(std::string_view name = "default_layer") : m_name(name) {}

        virtual ~Layer() = default;

        std::string_view GetName() const
        {
            return m_name;
        }

        void SetAttachedWindow(const ZEngine::Ref<Windows::CoreWindow>& window)
        {
            m_window = window;
        }

        ZEngine::Ref<ZEngine::Windows::CoreWindow> GetAttachedWindow() const
        {
            if (!m_window.expired())
                return m_window.lock();

            return nullptr;
        }

    protected:
        std::string                                    m_name;
        ZEngine::WeakRef<ZEngine::Windows::CoreWindow> m_window;
    };
} // namespace ZEngine::Windows::Layers
