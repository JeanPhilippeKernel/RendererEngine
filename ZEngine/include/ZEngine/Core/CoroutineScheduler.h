#pragma once
#include <Helpers/ThreadSafeQueue.h>
#include <ZEngineDef.h>
#include <atomic>
#include <functional>

namespace ZEngine::Core
{
    struct CoroutineAction : public Helpers::RefCounted
    {
        using ReadyCallback   = std::function<bool(void)>;
        using ExecuteCallback = std::function<void(void)>;

        ReadyCallback   Ready  = nullptr;
        ExecuteCallback Action = nullptr;

        operator bool() noexcept
        {
            return (Ready && Action);
        }
    };

    struct CoroutineScheduler
    {
        using SchedulerQueue = Helpers::ThreadSafeQueue<CoroutineAction>;

        static void Schedule(CoroutineAction&& action);

    private:
        static std::atomic_bool                               s_running;
        static Ref<Helpers::ThreadSafeQueue<CoroutineAction>> s_action_queue;

        static void Start();
        static void Run(WeakRef<SchedulerQueue> queue);
    };

} // namespace ZEngine::Core
