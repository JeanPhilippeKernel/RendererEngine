#pragma once
#include <ZEngine/Logging/Logger.h>
#include <ZEngine/UI/ZUIPanel.h>
#include <atomic>
#include <mutex>

namespace Tetragrama::Panels
{
    // Console panel

    /// @brief Log panel that subscribes to the engine logger and displays a
    ///        ring-buffered, searchable, filterable list of log entries.
    struct ConsolePanel : ZEngine::UI::ZUIPanelView
    {
        ConsolePanel();
        ~ConsolePanel();

        /// @brief Single log entry stored in the ring buffer.
        struct LogEntry
        {
            char    Text[256] = {};
            float   Color[4]  = {0.9f, 0.9f, 0.9f, 1.f};
            uint8_t Level     = 0;
        };

        // kLevels[0] = "All" (show everything); kLevels[1..5] map to LogLevel 0..4.
        // Putting "All" first matches standard log panel convention.
        static constexpr int         kMaxEntries         = 512;
        static constexpr const char* kLevels[]           = {"All", "Trace", "Info", "Warn", "Error", "Critical"};

        LogEntry                     m_ring[kMaxEntries] = {};
        int                          m_head              = 0;
        int                          m_count             = 0;
        std::mutex                   m_mutex;
        uint32_t                     m_cookie       = 0;
        bool                         m_initialized  = false;
        char                         m_search[128]  = {};
        int                          m_filter_level = 0;  // 0 = All; 1..5 = exact LogLevel 0..4
        std::atomic<bool>            m_auto_scroll{true}; // atomic: read on logger thread, written on UI thread
        bool                         m_scroll_pending = false;

        /// @brief Thread-safe push of a new log entry into the ring buffer.
        /// @param e Entry to push.
        void                         PushEntry(const LogEntry& e);

        /// @brief Logger event-handler callback; invoked on the logger thread.
        /// @param user Pointer to the owning ConsolePanel instance.
        /// @param msg  Log message from the engine logger.
        static void                  OnLogEntry(void* user, const ZEngine::Logging::LogMessage& msg);

        /// @brief Returns true if @p e satisfies the current search and level filter.
        /// @param e Entry to test.
        /// @returns True when the entry should be visible.
        bool                         PassesFilter(const LogEntry& e) const;

        /// @brief Builds the console toolbar and scrollable log list for this frame.
        /// @param ctx ZUI context for the current frame.
        /// @param rect Panel bounding rect [x0, y0, x1, y1].
        void                         BuildContent(ZEngine::UI::ZUIContext* ctx, float rect[4]) override;
    };
} // namespace Tetragrama::Panels
