#pragma once
#include <Tetragrama/Components/UIComponent.h>
#include <mutex>

namespace Tetragrama::Components
{
    class StatusBarUIComponent : public UIComponent
    {
    public:
        StatusBarUIComponent() = default;
        ~StatusBarUIComponent() override;

        void         Initialize(Layers::ImguiLayer* parent = nullptr, cstring name = "##StatusBar", bool visibility = true, bool closed = false) override;
        void         Update(ZEngine::Core::TimeStep dt) override;
        virtual void Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override;

    private:
        // FPS smoothing
        static constexpr int kFtSamples                = 30;
        float                m_frame_times[kFtSamples] = {};
        int                  m_ft_head                 = 0;
        float                m_smoothed_dt             = 0.016f;

        // Console overlay log ring
        struct OverlayEntry
        {
            char    Text[256] = {};
            float   Color[4]  = {0.8f, 0.8f, 0.8f, 1.0f};
            uint8_t Level     = 0; // 0=info,1=error,2=warn,3=critical,4=trace
        };
        static constexpr int kMaxEntries             = 512;
        OverlayEntry         m_log_ring[kMaxEntries] = {};
        int                  m_log_head              = 0;
        int                  m_log_count             = 0;
        std::mutex           m_log_mutex;
        uint32_t             m_log_cookie             = 0;
        bool                 m_console_target_open    = false; // desired state
        float                m_overlay_current_height = 0.0f;  // animated height
        bool                 m_scroll_to_bottom       = false;
        char                 m_search_buffer[256]     = {};
        bool                 m_copy_requested         = false;
        int                  m_filter_level           = 5; // 5 = All

        void                 PushEntry(const OverlayEntry& e);
        static void          OnLogEntry(void* ctx, const ZEngine::Logging::LogMessage& msg);
    };
} // namespace Tetragrama::Components
