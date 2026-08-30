#include <Tetragrama/Panels/MemoryProfilerPanel.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>
#include <cstring>

using namespace ZEngine::UI;
using namespace ZEngine::Profiling;

namespace Tetragrama::Panels
{
    void MemoryProfilerPanel::FormatBytes(char* buf, int n, uint64_t bytes)
    {
        if (bytes >= 1024ULL * 1024 * 1024)
            snprintf(buf, (size_t)n, "%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
        else if (bytes >= 1024ULL * 1024)
            snprintf(buf, (size_t)n, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
        else if (bytes >= 1024ULL)
            snprintf(buf, (size_t)n, "%.1f KB", (double)bytes / 1024.0);
        else
            snprintf(buf, (size_t)n, "%llu B", (unsigned long long)bytes);
    }

    void MemoryProfilerPanel::UsageColor(float f, float out[4])
    {
        if (f >= 0.85f)
        {
            out[0] = 0.90f; out[1] = 0.25f; out[2] = 0.25f; out[3] = 1.f; // red
        }
        else if (f >= 0.60f)
        {
            out[0] = 0.90f; out[1] = 0.70f; out[2] = 0.10f; out[3] = 1.f; // amber
        }
        else
        {
            out[0] = 0.30f; out[1] = 0.75f; out[2] = 0.45f; out[3] = 1.f; // green
        }
    }

    void MemoryProfilerPanel::BuildContent(ZUIContext* ctx, float rect[4])
    {
        static const float kBg[4] = {0.09f, 0.09f, 0.095f, 1.f};

        // Outer scroll-able column
        ZUIBox* bg   = ZUIBeginColumn(ctx, "##mp_bg", ZFill(), ZFill());
        bg->Flags    = bg->Flags | ZUI_DrawBackground | ZUI_Scrollable;
        ZUIBoxSetColorArr(bg, kBg);
        bg->EdgeSoftness = 0.f;

        float fh = ZUIGetFrameHeight(ctx);
        float content_w = rect[2] - rect[0];

        // Fetch stats — array must be init'd with an arena before GetStats pushes into it
        ZEngine::Core::Containers::Array<ArenaStats> stats;
        stats.init(&ctx->FrameArena, 32);
        MemoryProfiler::GetStats(stats);

        uint32_t arena_count = (uint32_t)stats.size();

        // Sync history count
        if ((int)arena_count > m_history_count)
            m_history_count = (int)arena_count;

        // Update history ring buffers
        for (uint32_t i = 0; i < arena_count && i < (uint32_t)kMaxArenas; ++i)
        {
            ArenaHistory& h     = m_history[i];
            float         mb    = (stats[i].Capacity > 0)
                                ? (float)stats[i].CurrentOffset / (1024.f * 1024.f)
                                : 0.f;
            h.samples[h.head]   = mb;
            h.head              = (h.head + 1) % kHistorySize;
            if (h.count < kHistorySize) h.count++;
        }

        // --- Header ---
        ZUISpacer(ctx, 6.f);
        {
            ZUIBeginRow(ctx, "##mp_hdr", ZFill(), ZPx(fh));
            ZUISpacer(ctx, 10.f);

            // Total = root arena (MainArena, first registered) — its offset IS the
            // real physical memory in use; sub-arenas are slices of it, not additional.
            if (arena_count > 0)
            {
                const ArenaStats& root = stats[0];
                char buf_used[32], buf_cap[32];
                FormatBytes(buf_used, sizeof(buf_used), root.CurrentOffset);
                FormatBytes(buf_cap,  sizeof(buf_cap),  root.Capacity);
                float root_pct = (root.Capacity > 0)
                               ? (float)root.CurrentOffset / (float)root.Capacity * 100.f
                               : 0.f;
                char hdr[96];
                snprintf(hdr, sizeof(hdr), "Total: %s / %s  (%.0f%%)", buf_used, buf_cap, root_pct);
                ZUILabel(ctx, hdr, ctx->Theme.TextDefault);
            }
            else
            {
                ZUILabel(ctx, "No arenas tracked", ctx->Theme.TextDim);
            }

            // Fill spacer
            ZUIBox* fill  = ZUIPushBox(ctx, "##mp_hfill", 10, ZUI_None);
            fill->Size[0] = ZFill(); fill->Size[1] = ZPx(1.f);
            ZUIPopBox(ctx);

            // Reset Peaks button
            ZUISignal rst = ZUIButton(ctx, "Reset Peaks##mp_rst");
            if (rst.Flags & ZUI_SignalClicked)
                MemoryProfiler::ResetPeaks();

            ZUISpacer(ctx, 8.f);
            ZUIEndRow(ctx);
        }
        ZUISpacer(ctx, 4.f);
        ZUISeparator(ctx);
        ZUISpacer(ctx, 6.f);

        if (arena_count == 0)
        {
            ZUILabel(ctx, "No arenas tracked.", ctx->Theme.TextDim);
        }

        // --- Per-arena blocks ---
        for (uint32_t i = 0; i < arena_count && i < (uint32_t)kMaxArenas; ++i)
        {
            const ArenaStats& s = stats[i];
            ArenaHistory&     h = m_history[i];

            float fraction = (s.Capacity > 0)
                           ? fminf(1.f, (float)s.CurrentOffset / (float)s.Capacity)
                           : 0.f;
            float col[4];
            UsageColor(fraction, col);

            // Arena name (color-coded)
            {
                ZUIBeginRow(ctx, "##mp_nr", ZFill(), ZPx(fh));
                ZUISpacer(ctx, 10.f);
                ZUILabel(ctx, s.Name ? s.Name : "?", col);
                ZUIEndRow(ctx);
            }

            // Stats line
            {
                char b_used[32], b_cap[32], b_peak[32];
                FormatBytes(b_used, sizeof(b_used), s.CurrentOffset);
                FormatBytes(b_cap,  sizeof(b_cap),  s.Capacity);
                FormatBytes(b_peak, sizeof(b_peak),  s.PeakOffset);
                char stats_buf[128];
                snprintf(stats_buf, sizeof(stats_buf),
                         "  %s / %s   peak: %s   %.0f%%",
                         b_used, b_cap, b_peak, fraction * 100.f);
                ZUIBeginRow(ctx, "##mp_sr", ZFill(), ZPx(fh));
                ZUISpacer(ctx, 10.f);
                ZUILabel(ctx, stats_buf, ctx->Theme.TextDim);
                ZUIEndRow(ctx);
            }
            ZUISpacer(ctx, 3.f);

            // Progress bar
            {
                static const char* kBgKeys[kMaxArenas] = {
                    "##pb0","##pb1","##pb2","##pb3","##pb4","##pb5","##pb6","##pb7",
                    "##pb8","##pb9","##pba","##pbb","##pbc","##pbd","##pbe","##pbf",
                    "##pbg","##pbh","##pbi","##pbj","##pbk","##pbl","##pbm","##pbn",
                    "##pbo","##pbp","##pbq","##pbr","##pbs","##pbt","##pbu","##pbv",
                };
                static const char* kFillKeys[kMaxArenas] = {
                    "##pf0","##pf1","##pf2","##pf3","##pf4","##pf5","##pf6","##pf7",
                    "##pf8","##pf9","##pfa","##pfb","##pfc","##pfd","##pfe","##pff",
                    "##pfg","##pfh","##pfi","##pfj","##pfk","##pfl","##pfm","##pfn",
                    "##pfo","##pfp","##pfq","##pfr","##pfs","##pft","##pfu","##pfv",
                };

                ZUISpacer(ctx, 3.f);
                ZUIBeginRow(ctx, "##mp_pbrow", ZFill(), ZPx(10.f));
                ZUISpacer(ctx, 10.f);

                float bar_available = fmaxf(1.f, content_w - 26.f);
                float fill_w        = fmaxf(2.f, fraction * bar_available);

                // Background
                ZUIBox* bg_b  = ZUIBeginRow(ctx, kBgKeys[i], ZPx(bar_available), ZPx(10.f));
                bg_b->Flags   = bg_b->Flags | ZUI_DrawBackground;
                ZUIBoxSetColor(bg_b, 0.15f, 0.15f, 0.17f, 1.f);
                ZUIBoxSetCornerRadius(bg_b, 3.f);
                bg_b->EdgeSoftness = 0.f;
                // Fill
                ZUIBox* fill_b = ZUIPushBox(ctx, kFillKeys[i], (uint32_t)strlen(kFillKeys[i]),
                                            ZUI_DrawBackground);
                fill_b->Size[0] = ZPx(fill_w);
                fill_b->Size[1] = ZFill();
                ZUIBoxSetColorArr(fill_b, col);
                ZUIBoxSetCornerRadius(fill_b, 3.f);
                fill_b->EdgeSoftness = 0.f;
                ZUIPopBox(ctx);
                ZUIEndRow(ctx); // bar bg

                ZUIEndRow(ctx); // pb row
                ZUISpacer(ctx, 3.f);
            }

            ZUISpacer(ctx, 6.f); // gap between arenas
        }

        ZUISpacer(ctx, 8.f);
        ZUIEndColumn(ctx);
    }

} // namespace Tetragrama::Panels
