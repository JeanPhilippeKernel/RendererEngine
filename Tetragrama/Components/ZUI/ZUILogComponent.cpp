#include <Tetragrama/Components/ZUI/ZUILogComponent.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>

using namespace ZEngine::UI;
using namespace ZEngine::Helpers;

namespace Tetragrama::Components
{
    ZUILogComponent::~ZUILogComponent()
    {
        if (m_cookie)
            ZEngine::Logging::Logger::RemoveEventHandler(m_cookie);
    }

    void ZUILogComponent::Initialize(Tetragrama::Layers::ZUILayer* parent,
                                     cstring name, bool visibility)
    {
        ParentLayer = parent;
        Name        = name;
        Visible     = visibility;
        m_cookie    = ZEngine::Logging::Logger::AddEventHandler({OnLogEntry, this});
    }

    void ZUILogComponent::OnLogEntry(void* user, const ZEngine::Logging::LogMessage& msg)
    {
        auto* self = static_cast<ZUILogComponent*>(user);
        LogEntry e;
        secure_strncpy(e.Text, sizeof(e.Text), msg.Message ? msg.Message : "", sizeof(e.Text) - 1);
        e.Color[0] = msg.Color[0];
        e.Color[1] = msg.Color[1];
        e.Color[2] = msg.Color[2];
        e.Color[3] = msg.Color[3];
        e.Level    = static_cast<uint8_t>(msg.Level);
        self->PushEntry(e);
    }

    void ZUILogComponent::PushEntry(const LogEntry& e)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ring[m_head] = e;
        m_head         = (m_head + 1) % kMaxEntries;
        if (m_count < kMaxEntries)
            ++m_count;
        m_scroll_to_bottom = true;
    }

    void ZUILogComponent::BuildUI(ZEngine::UI::ZUIContext* ctx)
    {
        if (!Visible) { return; }

        float sx = RegionW > 0 ? RegionX : 20.f;
        float sy = RegionW > 0 ? RegionY : 500.f;
        float sw = RegionW > 0 ? RegionW : kPanelW;
        float sh = RegionW > 0 ? RegionH : kPanelH;

        if (RegionW == 0) { RegionX = sx; RegionY = sy; RegionW = sw; RegionH = sh; }
        ZUIBox* panel    = ZUIBeginColumn(ctx, "##zui_log_panel", ZPx(RegionW), ZPx(RegionH));
        panel->Flags = panel->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY;
        panel->FloatPos[0] = RegionX;
        panel->FloatPos[1] = RegionY;
        panel->BgColor[0]  = ctx->Theme.PanelBg[0];
        panel->BgColor[1]  = ctx->Theme.PanelBg[1];
        panel->BgColor[2]  = ctx->Theme.PanelBg[2];
        panel->BgColor[3]  = ctx->Theme.PanelBg[3];
        panel->BorderColor[0] = ctx->Theme.PanelBorder[0];
        panel->BorderColor[1] = ctx->Theme.PanelBorder[1];
        panel->BorderColor[2] = ctx->Theme.PanelBorder[2];
        panel->BorderColor[3] = ctx->Theme.PanelBorder[3];
        panel->BorderThickness = 1.f;

        // --- Header row — draggable ---
        ZUIBox* hdr = ZUIBeginRow(ctx, "##log_header", ZFill(), ZPx(28.f));
        hdr->Flags  = hdr->Flags | ZUI_DrawBackground | ZUI_Clickable;
        hdr->BgColor[0] = ctx->Theme.HeaderBg[0]; hdr->BgColor[1] = ctx->Theme.HeaderBg[1];
        hdr->BgColor[2] = ctx->Theme.HeaderBg[2]; hdr->BgColor[3] = ctx->Theme.HeaderBg[3];
            ZUISpacer(ctx, 6.f);
            ZUILabel(ctx, Name ? Name : "Console", ctx->Theme.TextDefault);
            ZUISpacer(ctx, 8.f);
            ZUISignal clear_sig = ZUIButton(ctx, "Clear##log");
            ZUISignal drag_sig  = ZUISignalFromBox(ctx, hdr);
        ZUIEndRow(ctx);
        if ((drag_sig.Flags & ZUI_SignalHeld) &&
            (drag_sig.DragDelta[0] != 0.f || drag_sig.DragDelta[1] != 0.f))
        {
            RegionX += drag_sig.DragDelta[0];
            RegionY += drag_sig.DragDelta[1];
            Detached = true;
            panel->FloatPos[0] = RegionX;
            panel->FloatPos[1] = RegionY;
        }
        if (drag_sig.Flags & ZUI_SignalDoubleClicked) { Detached = false; }

        ZUISeparator(ctx);

        // --- Search + level filter toolbar ---
        static const char* kLevelLabels[6] = {"Trace", "Info", "Warn", "Error", "Critical", "All"};
        ZUIBeginRow(ctx, "##log_toolbar", ZFill(), ZPx(24.f));
            ZUILabel(ctx, "Search:", ctx->Theme.TextDim);
            ZUISpacer(ctx, 4.f);
            ZUITextField(ctx, "##log_search", m_search_buf, sizeof(m_search_buf), 160.f);
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, "Level:", ctx->Theme.TextDim);
            ZUISpacer(ctx, 4.f);
            if (ZUIBeginCombo(ctx, "##log_lvl", kLevelLabels[m_filter_level], ZEngine::UI::ZPx(90.f)))
            {
                for (int lvl = 0; lvl < 6; ++lvl)
                {
                    bool sel = (m_filter_level == lvl);
                    if (ZUIComboItem(ctx, kLevelLabels[lvl], sel))
                        m_filter_level = lvl;
                }
                ZUIEndCombo(ctx);
            }
        ZUIEndRow(ctx);

        // --- All log entries inside a scroll region ---
        bool do_scroll;
        ZUIBox* scroll = ZUIBeginScrollRegion(ctx, "##log_scroll", ZFill(), ZFill());
        ZUIPaddingXY(scroll, 4.f, 2.f);
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            do_scroll          = m_scroll_to_bottom;
            m_scroll_to_bottom = false;

            int total = m_count < kMaxEntries ? m_count : kMaxEntries;
            int start = (m_count >= kMaxEntries) ? m_head : 0;

            for (int i = 0; i < total; ++i)
            {
                const LogEntry& e = m_ring[(start + i) % kMaxEntries];

                // Level filter
                if (m_filter_level < 5 && e.Level != (uint8_t)m_filter_level)
                    continue;

                // Search filter (case-sensitive strstr)
                if (m_search_buf[0] && !strstr(e.Text, m_search_buf))
                    continue;

                ZUILabel(ctx, e.Text, e.Color);
            }

            if (clear_sig.Flags & ZUI_SignalClicked)
            {
                m_count = 0;
                m_head  = 0;
            }
        }

        if (do_scroll)
            ZUIScrollToBottom(ctx, "##log_scroll");

        ZUIEndScrollRegion(ctx);
        ZUIEndColumn(ctx); // end panel
    }
} // namespace Tetragrama::Components
