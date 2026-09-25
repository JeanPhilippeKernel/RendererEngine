#include <ZEngine/Core/Containers/MPSCQueue.h>
#include <ZEngine/Core/MainThreadScheduler.h>
#include <ZEngine/ZEngineDef.h>
#include <new>

namespace ZEngine::Core
{
    struct MainThreadTask
    {
        void* Context     = nullptr;
        void (*Fn)(void*) = nullptr;
    };

    using MainThreadTaskQueue           = Containers::MPSCQueue<MainThreadTask, MainThreadScheduler::MAX_TASKS>;

    static MainThreadTaskQueue* s_queue = nullptr;

    void                        MainThreadScheduler::Initialize(Memory::ArenaAllocator* arena)
    {
        void* storage = arena->Allocate(sizeof(MainThreadTaskQueue), alignof(MainThreadTaskQueue));
        s_queue       = new (storage) MainThreadTaskQueue{};
    }

    void MainThreadScheduler::Post(void* context, void (*fn)(void*))
    {
        ZENGINE_VALIDATE_ASSERT(s_queue && s_queue->push({context, fn}), "MainThreadScheduler::Post — queue full; increase MAX_TASKS")
    }

    void MainThreadScheduler::Drain()
    {
        // Snapshot the current contiguous queue prefix. Work posted by a callback
        // is enqueued for the next frame instead of extending this drain forever.
        MainThreadTask batch[MAX_TASKS] = {};
        uint32_t       count            = 0;
        while (count < MAX_TASKS && s_queue->pop(batch[count]))
            ++count;

        for (uint32_t i = 0; i < count; ++i)
        {
            batch[i].Fn(batch[i].Context);
        }
    }

    void MainThreadScheduler::Shutdown()
    {
        if (s_queue)
        {
            s_queue->clear();
            s_queue = nullptr;
        }
    }

} // namespace ZEngine::Core
