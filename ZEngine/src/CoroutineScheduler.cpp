#include <thread>
#include <Core/CoroutineScheduler.h>
#include <Helpers/ThreadPool.h>

using namespace ZEngine::Helpers;

namespace ZEngine::Core
{
    std::atomic_bool                        CoroutineScheduler::s_running      = false;
    Ref<CoroutineScheduler::SchedulerQueue> CoroutineScheduler::s_action_queue = CreateRef<CoroutineScheduler::SchedulerQueue>();

    void CoroutineScheduler::Schedule(CoroutineAction&& action)
    {
        if (!action)
        {
            return;
        }

        s_action_queue->Emplace(std::move(action));
        if (!s_running.exchange(true))
        {
            CoroutineScheduler::Start();
        }
    }

    void CoroutineScheduler::Start()
    {
        std::thread(Run, s_action_queue.Weak()).detach();
    }

    void CoroutineScheduler::Run(WeakRef<SchedulerQueue> queue_ref)
    {
        while (auto queue = queue_ref.lock())
        {
            CoroutineAction co_action;

            if (!queue->Pop(co_action))
            {
                continue;
            }

            if (co_action && !co_action.Ready())
            {
                queue->Emplace(std::move(co_action));
                continue;
            }

            ThreadPoolHelper::Submit(co_action.Action);
        }
    }
} // namespace ZEngine::Core
