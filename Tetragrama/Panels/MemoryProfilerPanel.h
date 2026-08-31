#pragma once
#include <Tetragrama/Panels/PanelHelpers.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Profiling/MemoryProfiler.h>
#include <ZEngine/UI/ZUIPanel.h>

namespace Tetragrama::Panels
{
    /// @brief Memory profiler panel.  Queries MemoryProfiler::GetStats() each
    ///        frame, maintains per-arena usage history, and renders progress bars
    ///        color-coded by usage fraction (green / amber / red).
    struct MemoryProfilerPanel : ZEngine::UI::ZUIPanelView
    {
        MemoryProfilerPanel()
        {
            Title = "Profiler";
        }

        /// @brief Builds the profiler header and per-arena progress bars.
        /// @param ctx ZUI context for the current frame.
        /// @param rect Panel bounding rect [x0, y0, x1, y1].
        void BuildContent(ZEngine::UI::ZUIContext* ctx, float rect[4]) override;

    private:
        static constexpr int kHistorySize = 128;
        static constexpr int kMaxArenas   = 32;

        struct ArenaHistory
        {
            float samples[kHistorySize] = {};
            int   head                  = 0;
            int   count                 = 0;
        };

        ArenaHistory m_history[kMaxArenas] = {};
        int          m_history_count       = 0;

        /// @brief Formats a byte count as a human-readable string (B / KB / MB / GB).
        /// @param buf Output buffer.
        /// @param n   Buffer length in bytes.
        /// @param bytes Raw byte count to format.
        static void  FormatBytes(char* buf, int n, uint64_t bytes);

        /// @brief Fills @p out with an RGBA color reflecting arena usage.
        /// @param fraction Usage in [0, 1].
        /// @param out       Destination 4-float RGBA array.
        static void  UsageColor(float fraction, float out[4]);
    };

} // namespace Tetragrama::Panels
