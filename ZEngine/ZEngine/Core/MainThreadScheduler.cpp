#include <ZEngine/Core/MainThreadScheduler.h>
#include <ZEngine/ZEngineDef.h>
#include <new>

namespace ZEngine::Core
{
    // MPSC (multi-producer, single-consumer) slot-claim protocol:
    //   Post  : fetch_add claims a slot → write Context/Fn → store(ready=true, release)
    //   Drain : exchange(write_cursor, 0) claims all pending → per-slot acquire spin → execute → reset
    //
    // The acquire spin on ready terminates immediately in practice: the producer sets
    // ready=true in the same Post() call, and Drain() runs at the next frame boundary.
    struct Slot
    {
        void* Context     = nullptr;
        void (*Fn)(void*) = nullptr;
        PaddedAtomic<bool> ready{};
    };

    static Slot*                  s_slots        = nullptr;
    static PaddedAtomic<uint32_t> s_write_cursor = {};

    void                          MainThreadScheduler::Initialize(Memory::ArenaAllocator* arena)
    {
        void* mem = arena->Allocate(MAX_TASKS * sizeof(Slot), alignof(Slot));
        s_slots   = static_cast<Slot*>(mem);
        for (uint32_t i = 0; i < MAX_TASKS; ++i)
            new (&s_slots[i]) Slot{};
        s_write_cursor.value.store(0, std::memory_order_relaxed);
    }

    void MainThreadScheduler::Post(void* context, void (*fn)(void*))
    {
        uint32_t idx = s_write_cursor.value.fetch_add(1, std::memory_order_relaxed);
        ZENGINE_VALIDATE_ASSERT(idx < MAX_TASKS, "MainThreadScheduler::Post — slot array full; increase MAX_TASKS")
        s_slots[idx].Context = context;
        s_slots[idx].Fn      = fn;
        s_slots[idx].ready.value.store(true, std::memory_order_release);
    }

    void MainThreadScheduler::Drain()
    {
        uint32_t count = s_write_cursor.value.exchange(0, std::memory_order_acq_rel);
        for (uint32_t i = 0; i < count; ++i)
        {
            while (!s_slots[i].ready.value.load(std::memory_order_acquire))
                ;
            s_slots[i].Fn(s_slots[i].Context);
            s_slots[i].ready.value.store(false, std::memory_order_relaxed);
        }
    }

    void MainThreadScheduler::Shutdown()
    {
        s_write_cursor.value.store(0, std::memory_order_relaxed);
    }

} // namespace ZEngine::Core
