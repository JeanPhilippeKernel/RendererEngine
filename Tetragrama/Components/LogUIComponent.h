#pragma once
#include <UIComponent.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Logging/Logger.h>
#include <atomic>

namespace Tetragrama::Components
{

    struct UILogMessage
    {
        ZEngine::Core::Containers::Array<float> Color   = {};
        ZEngine::Core::Containers::String       Type    = {};
        ZEngine::Core::Containers::String       Content = {};
    };

    class LogUIComponent : public UIComponent
    {
    public:
        LogUIComponent();
        virtual ~LogUIComponent();

        ZEngine::Core::Memory::ArenaAllocator                           LocalArena     = {};
        ZEngine::Helpers::ThreadSafeQueue<ZEngine::Logging::LogMessage> EngineLogQueue = {};
        ZEngine::Core::Containers::Array<UILogMessage>                  UILogQueue     = {};

        void                                                            Initialize(Layers::ImguiLayer* parent = nullptr, const char* name = "Console", bool visibility = true, bool closed = false) override;

        virtual void                                                    Update(ZEngine::Core::TimeStep dt) override;
        virtual void                                                    Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override;

        void                                                            OnLog(ZEngine::Logging::LogMessage);

        void                                                            ClearLog();

    private:
        bool             m_auto_scroll            = true;
        bool             m_is_copy_button_pressed = false;
        char             m_search_buffer[256]     = "";
        uint32_t         m_maxCount               = 1024;
        uint32_t         m_handler_cookie         = 0;
        std::atomic_uint m_currentCount           = 0;
    };
} // namespace Tetragrama::Components
