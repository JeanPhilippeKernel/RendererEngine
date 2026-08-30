#include <Tetragrama/Panels/ConsolePanel.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cctype>
#include <cstring>

namespace Tetragrama::Panels
{
    using namespace ZEngine::UI;

    static bool ContainsCI(const char* haystack, const char* needle)
    {
        if (!needle[0])
            return true;
        for (; *haystack; ++haystack)
        {
            const char* h = haystack;
            const char* n = needle;
            while (*h && *n && tolower((unsigned char) *h) == tolower((unsigned char) *n))
            {
                ++h;
                ++n;
            }
            if (!*n)
                return true;
        }
        return false;
    }

    ConsolePanel::ConsolePanel()
    {
        Title = "Console";
    }

    ConsolePanel::~ConsolePanel()
    {
        if (m_cookie)
            ZEngine::Logging::Logger::RemoveEventHandler(m_cookie);
    }

    void ConsolePanel::PushEntry(const LogEntry& e)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_ring[m_head] = e;
        m_head         = (m_head + 1) % kMaxEntries;
        if (m_count < kMaxEntries)
            ++m_count;
        if (m_auto_scroll.load(std::memory_order_relaxed))
            m_scroll_pending = true;
    }

    void ConsolePanel::OnLogEntry(void* user, const ZEngine::Logging::LogMessage& msg)
    {
        auto*    self = static_cast<ConsolePanel*>(user);
        LogEntry e    = {};
        int      len  = msg.Message ? (int) strlen(msg.Message) : 0;
        if (len > (int) sizeof(e.Text) - 1)
            len = (int) sizeof(e.Text) - 1;
        if (msg.Message)
            memcpy(e.Text, msg.Message, len);
        e.Text[len] = '\0';
        e.Color[0]  = msg.Color[0];
        e.Color[1]  = msg.Color[1];
        e.Color[2]  = msg.Color[2];
        e.Color[3]  = msg.Color[3];
        e.Level     = static_cast<uint8_t>(msg.Level);
        self->PushEntry(e);
    }

    bool ConsolePanel::PassesFilter(const LogEntry& e) const
    {
        if (m_filter_level > 0 && e.Level != (uint8_t) (m_filter_level - 1))
            return false;
        if (m_search[0] && !ContainsCI(e.Text, m_search))
            return false;
        return true;
    }

    void ConsolePanel::BuildContent(ZUIContext* ctx, float rect[4])
    {
        if (!m_initialized)
        {
            m_cookie      = ZEngine::Logging::Logger::AddEventHandler({OnLogEntry, this});
            m_initialized = true;
        }

        float   pw = rect[2] - rect[0];
        float   fh = ZUIGetFrameHeight(ctx);

        ZUIBox* bg = ZUIBeginColumn(ctx, "##con_bg", ZFill(), ZFill());
        bg->Flags  = bg->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
        bg->EdgeSoftness = 0.f;

        ZUISpacer(ctx, 6.f);

        // ── Toolbar: search | Filters popup | Clear | auto-scroll ─────────────
        ZUIBeginRow(ctx, "##con_tb", ZFill(), ZPx(fh));
        ZUISpacer(ctx, 8.f);
        ZUISearchBox(ctx, "##con_search", m_search, sizeof(m_search), "Search Log...", ZPx(fmaxf(pw * 0.50f, 120.f)));
        ZUISpacer(ctx, 8.f);

        // Filters dropdown — shows "Filters" when All, level name when filtered
        const char* flt_preview = (m_filter_level == 0) ? "Filters" : kLevels[m_filter_level];
        if (ZUIBeginCombo(ctx, "##con_flt", flt_preview, ZPx(100.f)))
        {
            for (int i = 0; i < 6; ++i)
                if (ZUIComboItem(ctx, kLevels[i], m_filter_level == i))
                    m_filter_level = i;
            ZUIEndCombo(ctx);
        }

        ZUISpacer(ctx, 8.f);
        bool do_clear = (ZUIButton(ctx, "Clear##con").Flags & ZUI_SignalClicked) != 0;
        ZUISpacer(ctx, 8.f);
        // atomic<bool> — load to local, pass pointer, store result back
        bool as = m_auto_scroll.load(std::memory_order_relaxed);
        ZUICheckbox(ctx, "##con_as", &as);
        m_auto_scroll.store(as, std::memory_order_relaxed);
        ZUISpacer(ctx, 4.f);
        ZUILabel(ctx, "Auto-scroll", ctx->Theme.TextDim);
        ZUISpacer(ctx, 8.f);
        ZUIEndRow(ctx);

        ZUISpacer(ctx, 4.f);
        ZUISeparator(ctx);

        // ── Log entries ──────────────────────────────────────────────────────
        bool    pending;
        ZUIBox* scroll = ZUIBeginScrollRegion(ctx, "##con_scroll", ZFill(), ZFill());
        ZUIPaddingXY(scroll, 4.f, 2.f);
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            pending          = m_scroll_pending;
            m_scroll_pending = false;

            if (do_clear)
            {
                m_count     = 0;
                m_head      = 0;
                m_search[0] = '\0';
            }

            int total = (m_count >= kMaxEntries) ? kMaxEntries : m_count;
            int start = (m_count >= kMaxEntries) ? m_head : 0;

            for (int i = 0; i < total; ++i)
            {
                const LogEntry& e = m_ring[(start + i) % kMaxEntries];
                if (!PassesFilter(e))
                    continue;
                ZUILabel(ctx, e.Text, e.Color);
            }
        }
        if (pending)
            ZUIScrollToBottom(ctx, "##con_scroll");

        ZUIEndScrollRegion(ctx);
        ZUIEndColumn(ctx);
    }
} // namespace Tetragrama::Panels
