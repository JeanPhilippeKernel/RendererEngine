#pragma once
#include <memory>
#include <Window/CoreWindow.h>

using namespace ZEngine::Window;

namespace ZEngine::Rendering::Graphics {

    class GraphicContext {
    public:
        GraphicContext() = default;
        GraphicContext(const CoreWindow* window) : m_window(window) {}

        virtual ~GraphicContext() = default;

        virtual void* GetNativeContext() const {
            return nullptr;
        }
        virtual void MarkActive() {}

        virtual void SetAttachedWindow(const CoreWindow* window) {
            m_window = window;
        };

        virtual const CoreWindow* GetAttachedWindow() const {
            return m_window;
        }

    protected:
        const CoreWindow* m_window{nullptr};
    };

    GraphicContext* CreateContext();
    GraphicContext* CreateContext(const CoreWindow* window);

} // namespace ZEngine::Rendering::Graphics
