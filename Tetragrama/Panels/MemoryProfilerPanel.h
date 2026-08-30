#pragma once
#include <Tetragrama/Panels/PanelHelpers.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Profiling/MemoryProfiler.h>
#include <ZEngine/UI/ZUIPanel.h>

namespace Tetragrama::Panels
{
    struct MemoryProfilerPanel : ZEngine::UI::ZUIPanelView
    {
        MemoryProfilerPanel()
        {
            Title = "Profiler";
        }

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

        static void FormatBytes(char* buf, int n, uint64_t bytes);

        // Returns {r,g,b,a} color based on usage fraction
        static void UsageColor(float fraction, float out[4]);
    };

} // namespace Tetragrama::Panels
