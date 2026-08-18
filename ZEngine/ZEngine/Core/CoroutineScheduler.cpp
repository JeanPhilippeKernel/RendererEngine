#include <ZEngine/Core/CoroutineScheduler.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <thread>

namespace ZEngine::Core
{
    std::atomic_bool                                                              CoroutineScheduler::s_running = false;
    Core::Containers::MPSCQueue<CoroutineAction, CoroutineScheduler::MAX_ACTIONS> CoroutineScheduler::s_queue;

    void                                                                          CoroutineScheduler::Schedule(const CoroutineAction& action)
    {
        if (!action.IsValid())
            return;

        s_queue.push(action);

        if (!s_running.exchange(true, std::memory_order_acq_rel))
            Start();
    }

    void CoroutineScheduler::Start()
    {
        std::thread(Run).detach();
    }

    void CoroutineScheduler::Run()
    {
        // Local staging buffer for not-yet-ready actions.
        // Avoids re-pushing back into the MPSC while consuming, which
        // would race on the same slot indices.
        CoroutineAction deferred[MAX_ACTIONS];
        uint32_t        deferred_count = 0;

        while (true)
        {
            deferred_count = 0;

            // Drain all pending items.
            CoroutineAction action;
            while (s_queue.pop(action))
            {
                if (!action.IsValid())
                    continue;
                if (action.IsReady())
                    Helpers::ThreadPoolHelper::Submit(action.ActionCtx, action.Action);
                else
                    deferred[deferred_count++] = action;
            }

            // Re-push deferred (not-yet-ready) actions — queue is fully
            // drained at this point so there are no aliased slots.
            for (uint32_t i = 0; i < deferred_count; ++i)
                s_queue.push(deferred[i]);

            // Exit when nothing left to process.
            if (s_queue.empty() && deferred_count == 0)
            {
                s_running.store(false, std::memory_order_release);
                // Re-check: a Schedule() may have raced with the store above.
                if (s_queue.empty())
                    break;
                // Something arrived — keep running.
                s_running.store(true, std::memory_order_relaxed);
            }
        }
    }

} // namespace ZEngine::Core
