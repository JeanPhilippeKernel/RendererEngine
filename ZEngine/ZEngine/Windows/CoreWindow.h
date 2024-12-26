#pragma once
#include <Core/CoreEvent.h>
#include <Core/EventDispatcher.h>
#include <Core/IEventable.h>
#include <Core/IInitializable.h>
#include <Core/IRenderable.h>
#include <Core/IUpdatable.h>
#include <Core/TimeStep.h>
#include <Helpers/IntrusivePtr.h>
#include <Inputs/IInputEventCallback.h>
#include <Layers/Layer.h>
#include <Layers/LayerStack.h>
#include <WindowConfiguration.h>
#include <WindowProperty.h>
#include <future>
#include <span>

namespace ZEngine::Windows::Layers
{
    class Layer;
    class LayerStack;
} // namespace ZEngine::Windows::Layers

namespace ZEngine::Windows
{
    class CoreWindow : public Helpers::RefCounted,
                       public Inputs::IKeyboardEventCallback,
                       public Inputs::IMouseEventCallback,
                       public Inputs::ITextInputEventCallback,
                       public Inputs::IWindowEventCallback,
                       public Core::IUpdatable,
                       public Core::IRenderable,
                       public Core::IEventable,
                       public Core::IInitializable
    {

    public:
        using EventCallbackFn = std::function<void(Core::CoreEvent&)>;

    public:
        CoreWindow();
        CoreWindow(const WindowConfiguration& cfg);
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

        virtual bool                     CreateSurface(void* instance, void** out_window_surface) = 0;
        virtual std::vector<std::string> GetRequiredExtensionLayers()                             = 0;
        virtual void*                    GetNativeWindow() const                                  = 0;

        virtual std::future<std::string> OpenFileDialogAsync(std::span<std::string_view> type_filters = {}) = 0;

        virtual void  PollEvent()    = 0;
        virtual float GetTime()      = 0;
        virtual float GetDeltaTime() = 0;

        virtual void ForwardEventToLayers(Core::CoreEvent& event);

        virtual void PushOverlayLayer(const Helpers::Ref<Layers::Layer>& layer);
        virtual void PushOverlayLayer(Helpers::Ref<Layers::Layer>&& layer);
        virtual void PushLayer(const Helpers::Ref<Layers::Layer>& layer);
        virtual void PushLayer(Helpers::Ref<Layers::Layer>&& layer);

    protected:
        Core::TimeStep                     m_delta_time;
        WindowProperty                     m_property;
        WindowConfiguration                m_configuration;
        Helpers::Scope<Layers::LayerStack> m_layer_stack_ptr{nullptr};
    };

    CoreWindow* Create(const WindowConfiguration&);
} // namespace ZEngine::Windows
