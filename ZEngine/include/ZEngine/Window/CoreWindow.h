#pragma once

#include <memory>

#include <Window/WindowProperty.h>
#include <Event/CoreEvent.h>
#include <Event/WindowClosedEvent.h>
#include <Event/WindowResizedEvent.h>
#include <Event/EventDispatcher.h>
#include <Event/TextInputEvent.h>
#include <Core/TimeStep.h>

#include <Inputs/IKeyboardEventCallback.h>
#include <Inputs/IMouseEventCallback.h>
#include <Inputs/ITextInputEventCallback.h>

#include <Core/IInitializable.h>
#include <Core/IUpdatable.h>
#include <Core/IRenderable.h>
#include <Core/IEventable.h>

#include <Window/ICoreWindowEventCallback.h>
#include <Layers/Layer.h>
#include <Layers/LayerStack.h>
#include <Window/WindowConfiguration.h>

namespace ZEngine
{
    class Engine;
}

namespace ZEngine::Layers
{
    class Layer;
    class LayerStack;
} // namespace ZEngine::Layers

namespace ZEngine::Window
{

    class CoreWindow : public std::enable_shared_from_this<CoreWindow>,
                       public Inputs::IKeyboardEventCallback,
                       public Inputs::IMouseEventCallback,
                       public Inputs::ITextInputEventCallback,
                       public Core::IUpdatable,
                       public Core::IRenderable,
                       public Core::IEventable,
                       public Core::IInitializable,
                       public ICoreWindowEventCallback
    {

    public:
        using EventCallbackFn = std::function<void(Event::CoreEvent&)>;

    public:
        CoreWindow();
        virtual ~CoreWindow();

        virtual uint32_t         GetHeight() const                = 0;
        virtual uint32_t         GetWidth() const                 = 0;
        virtual std::string_view GetTitle() const                 = 0;
        virtual void             SetTitle(std::string_view title) = 0;
        virtual bool             IsMinimized() const              = 0;

        virtual bool IsVSyncEnable() const                                = 0;
        virtual void SetVSync(bool value)                                 = 0;
        virtual void SetCallbackFunction(const EventCallbackFn& callback) = 0;

        virtual void* GetNativeWindow() const = 0;

        virtual const WindowProperty& GetWindowProperty() const = 0;

        virtual void  PollEvent() = 0;
        virtual float GetTime()   = 0;

        virtual void ForwardEventToLayers(Event::CoreEvent& event);

        virtual void PushOverlayLayer(const Ref<Layers::Layer>& layer);
        virtual void PushOverlayLayer(Ref<Layers::Layer>&& layer);
        virtual void PushLayer(const Ref<Layers::Layer>& layer);
        virtual void PushLayer(Ref<Layers::Layer>&& layer);

    protected:
        WindowProperty                              m_property;
        ZEngine::Scope<ZEngine::Layers::LayerStack> m_layer_stack_ptr{nullptr};
    };

    CoreWindow* Create(const WindowConfiguration&);
} // namespace ZEngine::Window
