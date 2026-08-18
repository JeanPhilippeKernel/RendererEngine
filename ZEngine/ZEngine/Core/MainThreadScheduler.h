#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <atomic>
#include <cstdint>

namespace ZEngine::Core
{
    // Lock-free multi-producer / single-consumer scheduler for posting callables
    // from background threads to the main thread.
    //
    // Usage:
    //   Any thread:   MainThreadScheduler::Post(ctx, fn);
    //   Main thread:  MainThreadScheduler::Drain();  // called once per frame
    //
    // Pattern:
    //   Heavy work  → ThreadPoolHelper::Submit(...)
    //   Result back → MainThreadScheduler::Post(ctx, fn)
    class MainThreadScheduler
    {
    public:
        static constexpr uint32_t MAX_TASKS = 512;

        // Called once in Engine::Initialize() — carves the slot array from the arena.
        static void               Initialize(Memory::ArenaAllocator* arena);

        // Thread-safe, lock-free — may be called from any thread.
        static void               Post(void* context, void (*fn)(void*));

        // Main-thread only — drains and executes all committed tasks in submission order.
        // Called once per frame inside Engine::MainThreadRun().
        static void               Drain();

        // Called in Engine::Deinitialize() — resets the write cursor;
        // any tasks not yet drained are discarded.
        static void               Shutdown();

        MainThreadScheduler()  = delete;
        ~MainThreadScheduler() = delete;
    };
} // namespace ZEngine::Core
