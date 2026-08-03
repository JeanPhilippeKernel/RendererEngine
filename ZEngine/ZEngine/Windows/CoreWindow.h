#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/CoreEvent.h>
#include <ZEngine/Core/EventDispatcher.h>
#include <ZEngine/Core/IEventable.h>
#include <ZEngine/Core/IInitializable.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/TimeStep.h>
#include <ZEngine/Windows/Inputs/IInputEventCallback.h>
#include <ZEngine/Windows/WindowConfiguration.h>
#include <ZEngine/Windows/WindowProperty.h>
#include <future>
#include <span>

namespace ZEngine::Windows
{
    class CoreWindow : public Inputs::IWindowEventCallback, public Core::IEventable
    {

    public:
        using EventCallbackFn = std::function<void(Core::CoreEvent&)>;

    public:
        CoreWindow() {}
        CoreWindow(const WindowConfiguration& cfg);
        virtual ~CoreWindow();

        Core::Containers::Array<const char*> RequiredExtensionLayers                                            = {};

        virtual uint32_t                     GetHeight() const                                                  = 0;
        virtual uint32_t                     GetWidth() const                                                   = 0;
        virtual Core::Containers::StringView GetTitle() const                                                   = 0;
        virtual void                         SetTitle(Core::Containers::StringView title)                       = 0;
        virtual bool                         IsMinimized() const                                                = 0;

        virtual bool                         IsVSyncEnable() const                                              = 0;
        virtual void                         SetVSync(bool value)                                               = 0;
        virtual void                         SetCallbackFunction(const EventCallbackFn& callback)               = 0;
        virtual const WindowProperty&        GetWindowProperty() const                                          = 0;

        virtual bool                         CreateSurface(void* instance, void** out_window_surface)           = 0;
        virtual void*                        GetNativeWindow() const                                            = 0;

        virtual std::future<std::string>     OpenFileDialogAsync(std::span<std::string_view> type_filters = {}) = 0;

        virtual void                         PollEvent()                                                        = 0;
        virtual float                        GetTime()                                                          = 0;
        virtual float                        GetDeltaTime()                                                     = 0;

        virtual void                         Deinitialize() {}

    protected:
        Core::TimeStep      m_delta_time;
        WindowProperty      m_property;
        WindowConfiguration m_configuration;
    };
    ZDEFINE_PTR(CoreWindow);
} // namespace ZEngine::Windows
