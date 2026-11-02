#include <ZEngine/Rendering/Renderers/RenderRecordingWorkers.h>
#include <gtest/gtest.h>
#include <future>

using ZEngine::Rendering::Renderers::RenderRecordingWorkers;

namespace
{
    struct Gate
    {
        std::mutex              Mutex{};
        std::condition_variable CV{};
        bool                    Started  = false;
        bool                    Released = false;
        static void             Block(void* context)
        {
            auto&            gate = *static_cast<Gate*>(context);
            std::unique_lock lock(gate.Mutex);
            gate.Started = true;
            gate.CV.notify_all();
            gate.CV.wait(lock, [&] { return gate.Released; });
        }
        bool WaitForStart()
        {
            std::unique_lock lock(Mutex);
            return CV.wait_for(lock, std::chrono::seconds(2), [&] { return Started; });
        }
        void Release()
        {
            std::lock_guard lock(Mutex);
            Released = true;
            CV.notify_all();
        }
    };

    struct ReleaseGate
    {
        Gate& Value;
        ~ReleaseGate()
        {
            Value.Release();
        }
    };

    struct Probe
    {
        std::thread::id Thread{};
        uint32_t        Calls = 0;
        static void     Run(void* context)
        {
            auto& probe  = *static_cast<Probe*>(context);
            probe.Thread = std::this_thread::get_id();
            ++probe.Calls;
        }
    };
} // namespace

TEST(RenderRecordingWorkersTest, CompletesBeforeBlockedSaturatedBackgroundWorkerIsReleased)
{
    Gate                         gate{};
    Probe                        background_probe{};
    ZEngine::Helpers::ThreadPool background(1);
    ReleaseGate                  release{gate};
    ASSERT_TRUE(background.SubmitToWorker(0, &gate, &Gate::Block));
    ASSERT_TRUE(gate.WaitForStart());
    for (uint32_t i = 0; i < ZEngine::Helpers::ThreadPool::MAX_TASKS_PER_WORKER; ++i)
        ASSERT_TRUE(background.Submit(&background_probe, &Probe::Run));
    EXPECT_FALSE(background.Submit(&background_probe, &Probe::Run));
    EXPECT_EQ(background_probe.Calls, 0u); // no caller-thread I/O fallback

    RenderRecordingWorkers recording(2);
    Probe                  probes[2]{};
    ZEngine::Helpers::Task tasks[] = {
        {&probes[0], &Probe::Run},
        {&probes[1], &Probe::Run}
    };
    auto       finished                = std::async(std::launch::async, [&] { recording.Record(tasks, 2); });
    const bool completed_while_blocked = finished.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    gate.Release(); // release even when the regression fails, before joining
    finished.get();
    EXPECT_TRUE(completed_while_blocked);
    EXPECT_EQ(probes[0].Calls, 1u);
    EXPECT_EQ(probes[1].Calls, 1u);
    EXPECT_NE(probes[0].Thread, probes[1].Thread);
}

TEST(RenderRecordingWorkersTest, PreservesAffinityAcrossSparseBatchesAndRepeatedLifetimes)
{
    for (uint32_t cycle = 0; cycle < 3; ++cycle)
    {
        RenderRecordingWorkers recording(3);
        Probe                  probes[3]{};
        ZEngine::Helpers::Task tasks[] = {
            {&probes[0], &Probe::Run},
            {&probes[1], &Probe::Run},
            {&probes[2], &Probe::Run}
        };
        recording.Record(tasks, 3);
        const std::thread::id owners[] = {probes[0].Thread, probes[1].Thread, probes[2].Thread};
        for (uint32_t batch = 0; batch < 200; ++batch)
        {
            tasks[1] = batch % 2 ? ZEngine::Helpers::Task{&probes[1], &Probe::Run} : ZEngine::Helpers::Task{};
            recording.Record(tasks, 3);
            for (uint32_t i = 0; i < 3; ++i)
            {
                EXPECT_EQ(probes[i].Thread, owners[i]);
                EXPECT_NE(probes[i].Thread, std::this_thread::get_id());
            }
        }
        EXPECT_EQ(probes[0].Calls, 201u);
        EXPECT_EQ(probes[1].Calls, 101u);
        EXPECT_EQ(probes[2].Calls, 201u);
        recording.Record(nullptr, 0);
    }
}

TEST(RenderRecordingWorkersTest, KeepsCallerContextsAliveUntilEveryTaskCompletes)
{
    Gate                   gate{};
    RenderRecordingWorkers recording(2);
    ZEngine::Helpers::Task tasks[] = {
        {&gate, &Gate::Block},
        {}
    };
    auto        finished = std::async(std::launch::async, [&] { recording.Record(tasks, 2); });
    ReleaseGate release{gate};
    EXPECT_TRUE(gate.WaitForStart());
    EXPECT_EQ(finished.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
    gate.Release();
    finished.get();
    recording.Shutdown();
    recording.Shutdown();
}

TEST(RenderRecordingWorkersTest, SequentialFallbackOnlyAfterWorkersHaveJoined)
{
    RenderRecordingWorkers recording(2);
    Probe                  probes[2]{};
    ZEngine::Helpers::Task tasks[] = {
        {&probes[0], &Probe::Run},
        {&probes[1], &Probe::Run}
    };
    recording.Record(tasks, 2);
    recording.Shutdown();
    recording.Record(tasks, 2);
    for (auto& probe : probes)
    {
        EXPECT_EQ(probe.Calls, 2u);
        EXPECT_EQ(probe.Thread, std::this_thread::get_id());
    }
}
