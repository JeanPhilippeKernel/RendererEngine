#pragma once
#include <Tetragrama/Components/ZUI/ZUIComponent.h>
#include <ZEngine/Logging/Logger.h>
#include <mutex>

namespace Tetragrama::Components
{
    class ZUILogComponent : public ZUIComponent
    {
    public:
        ZUILogComponent()          = default;
        ~ZUILogComponent() override;

        void Initialize(Tetragrama::Layers::ZUILayer* parent,
                        cstring name       = "Console",
                        bool    visibility = true) override;

        void BuildUI(ZEngine::UI::ZUIContext* ctx) override;

    private:
        struct LogEntry
        {
            char    Text[256] = {};
            float   Color[4]  = {0.90f, 0.90f, 0.90f, 1.f};
            uint8_t Level     = 0;
        };

        static constexpr int kMaxEntries    = 512;
        static constexpr int kVisibleLines  = 12;  // entries shown without scroll
        static constexpr float kPanelW      = 420.f;
        static constexpr float kPanelH      = 310.f;
        static constexpr float kEntryH      = 22.f;

        LogEntry  m_ring[kMaxEntries] = {};
        int       m_head              = 0;
        int       m_count             = 0;
        std::mutex m_mutex;
        uint32_t  m_cookie            = 0;

        void        PushEntry(const LogEntry& e);
        static void OnLogEntry(void* ctx, const ZEngine::Logging::LogMessage& msg);
    };
    ZDEFINE_PTR(ZUILogComponent);
} // namespace Tetragrama::Components
