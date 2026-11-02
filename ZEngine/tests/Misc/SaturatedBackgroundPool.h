#pragma once
#include <ZEngine/Helpers/ThreadPool.h>
#include <chrono>

// Test-only single-worker pool held behind a controlled gate. Restores the
// process pool and joins the held worker before its context can be destroyed.
struct SaturatedBackgroundPool
{
    ZEngine::Helpers::Scope<ZEngine::Helpers::ThreadPool> Original;
    std::mutex                                            Mutex;
    std::condition_variable                               CV;
    bool                                                  Started  = false;
    bool                                                  Released = false;

    SaturatedBackgroundPool() : Original(std::move(ZEngine::Helpers::ThreadPoolHelper::Pool))
    {
        ZEngine::Helpers::ThreadPoolHelper::Pool = ZEngine::Helpers::CreateScope<ZEngine::Helpers::ThreadPool>(1);
    }
    bool Fill()
    {
        using namespace ZEngine::Helpers;
        if (!ThreadPoolHelper::SubmitToWorker(
                0, this, +[](void* context) {
                    auto&            gate = *static_cast<SaturatedBackgroundPool*>(context);
                    std::unique_lock lock(gate.Mutex);
                    gate.Started = true;
                    gate.CV.notify_all();
                    gate.CV.wait(lock, [&] { return gate.Released; });
                }))
            return false;
        {
            std::unique_lock lock(Mutex);
            if (!CV.wait_for(lock, std::chrono::seconds(2), [&] { return Started; }))
                return false;
        }
        for (uint32_t i = 0; i < ThreadPool::MAX_TASKS_PER_WORKER; ++i)
            if (!ThreadPoolHelper::SubmitToWorker(0, nullptr, +[](void*) {}))
                return false;
        return true;
    }
    void Release()
    {
        std::lock_guard lock(Mutex);
        Released = true;
        CV.notify_all();
    }
    ~SaturatedBackgroundPool()
    {
        Release();
        ZEngine::Helpers::ThreadPoolHelper::Shutdown();
        ZEngine::Helpers::ThreadPoolHelper::Pool = std::move(Original);
    }
};
