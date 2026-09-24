#include <Tetragrama/Panels/MemoryProfilerPanel.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Engine.h>
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
            snprintf(buf, (size_t) n, "%.2f GB", (double) bytes / (1024.0 * 1024.0 * 1024.0));
        else if (bytes >= 1024ULL * 1024)
            snprintf(buf, (size_t) n, "%.1f MB", (double) bytes / (1024.0 * 1024.0));
        else if (bytes >= 1024ULL)
            snprintf(buf, (size_t) n, "%.1f KB", (double) bytes / 1024.0);
        else
            snprintf(buf, (size_t) n, "%llu B", (unsigned long long) bytes);
    }

    void MemoryProfilerPanel::UsageColor(float f, float out[4])
    {
        if (f >= 0.85f)
        {
            out[0] = 0.90f;
            out[1] = 0.25f;
            out[2] = 0.25f;
            out[3] = 1.f; // red
        }
        else if (f >= 0.60f)
        {
            out[0] = 0.90f;
            out[1] = 0.70f;
            out[2] = 0.10f;
            out[3] = 1.f; // amber
        }
        else
        {
            out[0] = 0.30f;
            out[1] = 0.75f;
            out[2] = 0.45f;
            out[3] = 1.f; // green
        }
    }

    void MemoryProfilerPanel::BuildContent(ZUIContext* ctx, float rect[4])
    {
        static const float kBg[4] = {0.09f, 0.09f, 0.095f, 1.f};

        // Keep the summary and reset action fixed while the detailed memory
        // report below occupies the remaining vertical space.
        ZUIBox*            bg     = ZUIBeginColumn(ctx, "##mp_bg", ZFill(), ZFill());
        bg->Flags                 = bg->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(bg, kBg);
        bg->EdgeSoftness                                       = 0.f;

        float                                        fh        = ZUIGetFrameHeight(ctx);
        float                                        content_w = rect[2] - rect[0];

        // Fetch stats — array must be init'd with an arena before GetStats pushes into it
        ZEngine::Core::Containers::Array<ArenaStats> stats;
        stats.init(&ctx->FrameArena, 32);
        MemoryProfiler::GetStats(stats);

        uint32_t arena_count  = (uint32_t) stats.size();
        uint64_t cpu_current  = 0;
        uint64_t cpu_capacity = 0;
        for (const ArenaStats& stat : stats)
        {
            cpu_current  += stat.CurrentOffset;
            cpu_capacity += stat.Capacity;
        }

        ZEngine::Rendering::Renderers::RendererMemoryStatistics renderer_memory     = {};
        bool                                                    has_renderer_memory = false;
        if (auto* const engine = ZEngine::Engine::GetContext(); engine && engine->App && engine->App->RenderPipeline && engine->App->RenderPipeline->SceneRenderer)
        {
            renderer_memory     = engine->App->RenderPipeline->SceneRenderer->GetMemoryStatistics();
            has_renderer_memory = true;
        }

        // Header
        ZUISpacer(ctx, 6.f);
        {
            ZUIBeginRow(ctx, "##mp_hdr", ZFill(), ZPx(fh));
            ZUISpacer(ctx, 10.f);

            if (arena_count > 0)
            {
                char buf_used[32], buf_cap[32];
                FormatBytes(buf_used, sizeof(buf_used), cpu_current);
                FormatBytes(buf_cap, sizeof(buf_cap), cpu_capacity);
                float cpu_pct = (cpu_capacity > 0) ? (float) cpu_current / (float) cpu_capacity * 100.f : 0.f;
                char  hdr[96];
                snprintf(hdr, sizeof(hdr), "Named CPU owners: %s / %s  (%.0f%%)", buf_used, buf_cap, cpu_pct);
                ZUILabel(ctx, hdr, ctx->Theme.TextDefault);
            }
            else
            {
                ZUILabel(ctx, "No arenas tracked", ctx->Theme.TextDim);
            }

            // Fill spacer
            ZUIBox* fill  = ZUIPushBox(ctx, "##mp_hfill", 10, ZUI_None);
            fill->Size[0] = ZFill();
            fill->Size[1] = ZPx(1.f);
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

        ZUIBeginScrollRegion(ctx, "##mp_scroll", ZFill(), ZFill());

        // Configured production runs have no shared root mapping. Bootstrap is one
        // ordinary named owner and carries the process-lifetime engine objects.
        ZUIBeginRow(ctx, "##mp_cpu_scope", ZFill(), ZPx(fh));
        ZUISpacer(ctx, 10.f);
        ZUILabel(ctx, "CPU only — no shared root/direct allocation in configured runs; Bootstrap is tracked below.", ctx->Theme.TextDim);
        ZUIEndRow(ctx);
        ZUISpacer(ctx, 6.f);

        if (arena_count == 0)
        {
            ZUILabel(ctx, "No arenas tracked.", ctx->Theme.TextDim);
        }

        // Per-arena blocks
        // HOT PATH — runs every frame, no heap allocation allowed.
        for (uint32_t i = 0; i < arena_count && i < (uint32_t) kMaxArenas; ++i)
        {
            const ArenaStats& s        = stats[i];
            float             fraction = (s.Capacity > 0) ? fminf(1.f, (float) s.CurrentOffset / (float) s.Capacity) : 0.f;
            float             col[4];
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
                FormatBytes(b_cap, sizeof(b_cap), s.Capacity);
                FormatBytes(b_peak, sizeof(b_peak), s.PeakOffset);
                char stats_buf[128];
                snprintf(stats_buf, sizeof(stats_buf), "  %s / %s   peak: %s   %.0f%%", b_used, b_cap, b_peak, fraction * 100.f);
                ZUIBeginRow(ctx, "##mp_sr", ZFill(), ZPx(fh));
                ZUISpacer(ctx, 10.f);
                ZUILabel(ctx, stats_buf, ctx->Theme.TextDim);
                ZUIEndRow(ctx);
            }
            ZUISpacer(ctx, 3.f);

            // Progress bar
            {
                static const char* kBgKeys[kMaxArenas] = {
                    "##pb0", "##pb1", "##pb2", "##pb3", "##pb4", "##pb5", "##pb6", "##pb7", "##pb8", "##pb9", "##pba", "##pbb", "##pbc", "##pbd", "##pbe", "##pbf", "##pbg", "##pbh", "##pbi", "##pbj", "##pbk", "##pbl", "##pbm", "##pbn", "##pbo", "##pbp", "##pbq", "##pbr", "##pbs", "##pbt", "##pbu", "##pbv",
                };
                static const char* kFillKeys[kMaxArenas] = {
                    "##pf0", "##pf1", "##pf2", "##pf3", "##pf4", "##pf5", "##pf6", "##pf7", "##pf8", "##pf9", "##pfa", "##pfb", "##pfc", "##pfd", "##pfe", "##pff", "##pfg", "##pfh", "##pfi", "##pfj", "##pfk", "##pfl", "##pfm", "##pfn", "##pfo", "##pfp", "##pfq", "##pfr", "##pfs", "##pft", "##pfu", "##pfv",
                };

                ZUISpacer(ctx, 3.f);
                ZUIBeginRow(ctx, "##mp_pbrow", ZFill(), ZPx(10.f));
                ZUISpacer(ctx, 10.f);

                float   bar_available = fmaxf(1.f, content_w - 26.f);
                float   fill_w        = fmaxf(2.f, fraction * bar_available);

                // Background
                ZUIBox* bg_b          = ZUIBeginRow(ctx, kBgKeys[i], ZPx(bar_available), ZPx(10.f));
                bg_b->Flags           = bg_b->Flags | ZUI_DrawBackground;
                ZUIBoxSetColor(bg_b, 0.15f, 0.15f, 0.17f, 1.f);
                ZUIBoxSetCornerRadius(bg_b, 3.f);
                bg_b->EdgeSoftness = 0.f;
                // Fill
                ZUIBox* fill_b     = ZUIPushBox(ctx, kFillKeys[i], (uint32_t) strlen(kFillKeys[i]), ZUI_DrawBackground);
                fill_b->Size[0]    = ZPx(fill_w);
                fill_b->Size[1]    = ZFill();
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

        ZUISeparator(ctx);
        ZUISpacer(ctx, 6.f);

        // These are deliberately separate accounting domains. Do not add them to
        // the CPU rows above: their ownership and policy are independent.
        ZUIBeginRow(ctx, "##mp_gpu_title", ZFill(), ZPx(fh));
        ZUISpacer(ctx, 10.f);
        ZUILabel(ctx, "Renderer memory (separate from CPU arena capacity)", ctx->Theme.TextDefault);
        ZUIEndRow(ctx);
        ZUISpacer(ctx, 3.f);

        if (!has_renderer_memory)
        {
            ZUIBeginRow(ctx, "##mp_gpu_unavailable", ZFill(), ZPx(fh));
            ZUISpacer(ctx, 10.f);
            ZUILabel(ctx, "Renderer telemetry is unavailable.", ctx->Theme.TextDim);
            ZUIEndRow(ctx);
        }
        else
        {
            char vma_allocated[32], vma_blocks[32], heap_usage[32], heap_budget[32];
            FormatBytes(vma_allocated, sizeof(vma_allocated), renderer_memory.Vma.AllocationBytes);
            FormatBytes(vma_blocks, sizeof(vma_blocks), renderer_memory.Vma.BlockBytes);
            FormatBytes(heap_usage, sizeof(heap_usage), renderer_memory.Vma.HeapUsageBytes);
            FormatBytes(heap_budget, sizeof(heap_budget), renderer_memory.Vma.HeapBudgetBytes);
            char vma_text[192];
            snprintf(vma_text, sizeof(vma_text), "VMA allocations: %s; VMA blocks: %s; %s heap use: %s / %s (%u heap%s)", vma_allocated, vma_blocks, renderer_memory.Vma.UsesDriverBudgetTelemetry ? "driver" : "VMA fallback", heap_usage, heap_budget, renderer_memory.Vma.HeapCount, renderer_memory.Vma.HeapCount == 1 ? "" : "s");
            ZUIBeginRow(ctx, "##mp_vma", ZFill(), ZPx(fh));
            ZUISpacer(ctx, 10.f);
            ZUILabel(ctx, vma_text, ctx->Theme.TextDim);
            ZUIEndRow(ctx);

            char environment_reserved[32], environment_budget[32];
            FormatBytes(environment_reserved, sizeof(environment_reserved), renderer_memory.EnvironmentReservedBytes);
            FormatBytes(environment_budget, sizeof(environment_budget), renderer_memory.EnvironmentBudgetBytes);
            char environment_text[160];
            snprintf(environment_text, sizeof(environment_text), "Persistent environment resources: %s / %s policy cap", environment_reserved, environment_budget);
            ZUIBeginRow(ctx, "##mp_environment", ZFill(), ZPx(fh));
            ZUISpacer(ctx, 10.f);
            ZUILabel(ctx, environment_text, ctx->Theme.TextDim);
            ZUIEndRow(ctx);

            char transient_virtual[32], transient_physical[32], transient_saving[32];
            FormatBytes(transient_virtual, sizeof(transient_virtual), renderer_memory.TransientVirtualBytes());
            FormatBytes(transient_physical, sizeof(transient_physical), renderer_memory.TransientPhysicalBytes());
            FormatBytes(transient_saving, sizeof(transient_saving), renderer_memory.TransientAliasingSavings());
            char transient_text[192];
            snprintf(transient_text, sizeof(transient_text), "Render-graph transients: virtual %s; physical %s; aliasing saved %s (%u image, %u buffer backing%s)", transient_virtual, transient_physical, transient_saving, renderer_memory.TransientImageBackingCount, renderer_memory.TransientBufferBackingCount, renderer_memory.TransientImageBackingCount + renderer_memory.TransientBufferBackingCount == 1 ? "" : "s");
            ZUIBeginRow(ctx, "##mp_transients", ZFill(), ZPx(fh));
            ZUISpacer(ctx, 10.f);
            ZUILabel(ctx, transient_text, ctx->Theme.TextDim);
            ZUIEndRow(ctx);
        }

        ZUISpacer(ctx, 8.f);
        ZUIEndScrollRegion(ctx);
        ZUIEndColumn(ctx);
    }

} // namespace Tetragrama::Panels
