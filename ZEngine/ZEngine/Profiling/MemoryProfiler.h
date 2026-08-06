// ZEngine/Profiling/MemoryProfiler.h
#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::Profiling
{

    struct ArenaStats
    {
        cstring  Name          = nullptr;
        uint64_t CurrentOffset = 0; // bytes currently allocated
        uint64_t PeakOffset    = 0; // high-watermark since last reset
        uint64_t Capacity      = 0; // total arena size in bytes
    };

    class MemoryProfiler
    {
    public:
        // Must be called once before any TrackArena call, passing the engine's main arena.
        // Reserves capacity for all sub-arenas that will be registered.
        static void Initialize(Core::Memory::ArenaAllocator* arena);

        // Register an arena for watermark tracking. `name` must be a string literal.
        // Call once per arena at engine startup (before the first frame).
        static void TrackArena(const char* name, Core::Memory::ArenaAllocator* arena);

        // Called once per frame. Updates CurrentOffset and PeakOffset for all
        // tracked arenas. Emits ZENGINE_PROFILE_VALUE for each arena.
        static void Update();

        // Fill `out` with a snapshot of all tracked arena stats.
        // Called by the debug overlay once per frame.
        static void GetStats(Core::Containers::Array<ArenaStats>& out);

        // Notify of a raw allocation (used by ZENGINE_PROFILE_ALLOC fallback).
        static void RecordAlloc(const void* ptr, uint64_t size);

        // Notify of a raw free.
        static void RecordFree(const void* ptr);

        // Reset all peak watermarks (e.g. after a level load).
        static void ResetPeaks();

    private:
        struct TrackedArena
        {
            cstring                       Name              = nullptr;
            Core::Memory::ArenaAllocator* Arena             = nullptr;
            uint64_t                      PeakOffset        = 0;
            int64_t                       LastWarningTimeNs = 0;
            bool                          HasWarned         = false;
        };

        static Core::Containers::Array<TrackedArena> s_arenas;
    };

} // namespace ZEngine::Profiling