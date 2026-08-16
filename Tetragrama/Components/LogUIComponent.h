#pragma once
#include <Tetragrama/Components/UIComponent.h>
#include <ZEngine/Logging/Logger.h>
#include <mutex>

namespace Tetragrama::Components
{
    class LogUIComponent : public UIComponent
    {
    public:
        LogUIComponent() = default;
        ~LogUIComponent() override;

        void         Initialize(Layers::ImguiLayer* parent = nullptr, cstring name = "Console", bool visibility = true, bool closed = false) override;
        void         Update(ZEngine::Core::TimeStep dt) override;
        virtual void Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override;

    private:
        struct LogEntry
        {
            char    Text[256] = {};
            float   Color[4]  = {0.8f, 0.8f, 0.8f, 1.0f};
            uint8_t Level     = 0;
        };

        static constexpr int kMaxEntries         = 512;
        LogEntry             m_ring[kMaxEntries] = {};
        int                  m_head              = 0;
        int                  m_count             = 0;
        std::mutex           m_mutex;
        uint32_t             m_cookie           = 0;

        bool                 m_scroll_to_bottom = false;
        char                 m_search_buf[256]  = {};
        bool                 m_copy_requested   = false;
        int                  m_filter_level     = 5;

        void                 PushEntry(const LogEntry& e);
        static void          OnLogEntry(void* ctx, const ZEngine::Logging::LogMessage& msg);
    };
} // namespace Tetragrama::Components
