#pragma once
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Helpers/ThreadSafeQueue.h>
#include <ZEngine/ZEngineDef.h>
#include <atomic>
#include <functional>

namespace ZEngine::Core
{
    struct CoroutineAction : public Helpers::RefCounted
    {
        using ReadyCallback    = std::function<bool(void)>;
        using ExecuteCallback  = std::function<void(void)>;

        ReadyCallback   Ready  = nullptr;
        ExecuteCallback Action = nullptr;
        // clang-format off
        operator bool() noexcept
        {
            return (Ready && Action);
        }
        // clang-format on
    };

    struct CoroutineScheduler
    {
        using SchedulerQueue = Helpers::ThreadSafeQueue<CoroutineAction>;

        static void Schedule(CoroutineAction&& action);

    private:
        static std::atomic_bool                                        s_running;
        static Helpers::Ref<Helpers::ThreadSafeQueue<CoroutineAction>> s_action_queue;

        static void                                                    Start();
        static void                                                    Run(Helpers::WeakRef<SchedulerQueue> queue);
    };

} // namespace ZEngine::Core
