#pragma once
#include <ZEngine/Core/Containers/MPSCQueue.h>
#include <ZEngine/ZEngineDef.h>
#include <atomic>

namespace ZEngine::Core
{
    using ReadyCallback  = bool (*)(void* ctx);
    using ActionCallback = void (*)(void* ctx);

    // C-style coroutine action — zero allocation, fits in a queue slot.
    // ReadyCallback: optional predicate; nullptr means always ready.
    // ActionCallback: continuation dispatched to the thread pool when ready.
    struct CoroutineAction
    {
        void*          ReadyCtx  = nullptr;
        ReadyCallback  Ready     = nullptr;
        void*          ActionCtx = nullptr;
        ActionCallback Action    = nullptr;

        bool           IsValid() const
        {
            return Action != nullptr;
        }
        bool IsReady() const
        {
            return !Ready || Ready(ReadyCtx);
        }
    };

    struct CoroutineScheduler
    {
        static constexpr uint32_t MAX_ACTIONS = 256;

        // Thread-safe — post from any thread.
        static void               Schedule(const CoroutineAction& action);

    private:
        static std::atomic_bool                                          s_running;
        static Core::Containers::MPSCQueue<CoroutineAction, MAX_ACTIONS> s_queue;

        static void                                                      Start();
        static void                                                      Run();
    };

} // namespace ZEngine::Core
