#pragma once
#include <memory>
#include <Window/CoreWindow.h>

namespace ZEngine::Rendering::Graphics {

    class GraphicContext {
    public:
        GraphicContext() = default;
        GraphicContext(const ZEngine::Window::CoreWindow* window) : m_window(window) {}

        virtual ~GraphicContext() = default;

        virtual void* GetNativeContext() const {
            return nullptr;
        }
        virtual void MarkActive() {}

        virtual void SetAttachedWindow(const ZEngine::Window::CoreWindow* window) {
            m_window = window;
        };

        virtual const ZEngine::Window::CoreWindow* GetAttachedWindow() const {
            return m_window;
        }

    protected:
        const ZEngine::Window::CoreWindow* m_window{nullptr};
    };

    GraphicContext* CreateContext();
    GraphicContext* CreateContext(const ZEngine::Window::CoreWindow* window);

} // namespace ZEngine::Rendering::Graphics
