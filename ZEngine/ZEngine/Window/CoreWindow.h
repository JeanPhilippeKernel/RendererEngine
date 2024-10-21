#pragma once

#include <Core/IEventable.h>
#include <Core/IInitializable.h>
#include <Core/IRenderable.h>
#include <Core/IUpdatable.h>
#include <Core/TimeStep.h>
#include <Event/CoreEvent.h>
#include <Event/EventDispatcher.h>
#include <Event/TextInputEvent.h>
#include <Event/WindowEvent.h>
#include <Inputs/IInputEventCallback.h>
#include <Layers/Layer.h>
#include <Layers/LayerStack.h>
#include <Rendering/Swapchain.h>
#include <Window/WindowConfiguration.h>
#include <Window/WindowProperty.h>

namespace ZEngine::Layers
{
    class Layer;
    class LayerStack;
} // namespace ZEngine::Layers

namespace ZEngine::Window
{

    struct ICoreWindowEventCallback
    {
        virtual bool OnWindowClosed(Event::WindowClosedEvent&)       = 0;
        virtual bool OnWindowResized(Event::WindowResizedEvent&)     = 0;
        virtual bool OnWindowMinimized(Event::WindowMinimizedEvent&) = 0;
        virtual bool OnWindowMaximized(Event::WindowMaximizedEvent&) = 0;
        virtual bool OnWindowRestored(Event::WindowRestoredEvent&)   = 0;
    };

    class CoreWindow : public Helpers::RefCounted,
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

        virtual void             InitializeLayer()                = 0;
        virtual uint32_t         GetHeight() const                = 0;
        virtual uint32_t         GetWidth() const                 = 0;
        virtual std::string_view GetTitle() const                 = 0;
        virtual void             SetTitle(std::string_view title) = 0;
        virtual bool             IsMinimized() const              = 0;

        virtual bool                  IsVSyncEnable() const                                = 0;
        virtual void                  SetVSync(bool value)                                 = 0;
        virtual void                  SetCallbackFunction(const EventCallbackFn& callback) = 0;
        virtual const WindowProperty& GetWindowProperty() const                            = 0;

        virtual bool                      CreateSurface(void* instance, void** out_window_surface) = 0;
        virtual std::vector<std::string>  GetRequiredExtensionLayers()                             = 0;
        virtual void*                     GetNativeWindow() const                                  = 0;
        virtual Ref<Rendering::Swapchain> GetSwapchain() const                                     = 0;

        virtual void  PollEvent()    = 0;
        virtual float GetTime()      = 0;
        virtual float GetDeltaTime() = 0;

        virtual void ForwardEventToLayers(Event::CoreEvent& event);

        virtual void PushOverlayLayer(const Ref<Layers::Layer>& layer);
        virtual void PushOverlayLayer(Ref<Layers::Layer>&& layer);
        virtual void PushLayer(const Ref<Layers::Layer>& layer);
        virtual void PushLayer(Ref<Layers::Layer>&& layer);

    protected:
        Core::TimeStep                              m_delta_time;
        WindowProperty                              m_property;
        ZEngine::Scope<ZEngine::Layers::LayerStack> m_layer_stack_ptr{nullptr};
    };

    CoreWindow* Create(const WindowConfiguration&);
} // namespace ZEngine::Window
