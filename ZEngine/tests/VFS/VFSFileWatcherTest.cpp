#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSFileWatcher.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <chrono>
#include <mutex>
#include <thread>

using namespace ZEngine::Core::VFS;
using ZEngine::Core::Memory::MemoryManager;

namespace
{
    class MockPlatformWatcher final : public IVFSPlatformWatcher
    {
    public:
        WatchHandle AddWatch(const char* native_path, bool recursive) override
        {
            (void) native_path;
            (void) recursive;
            return m_next_handle++;
        }

        void RemoveWatch(WatchHandle handle) override
        {
            m_removed.push_back(handle);
        }

        void Poll(RawEventCallback cb, void* ctx) override
        {
            std::vector<VFSWatchEvent> drained;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                drained.swap(m_queue);
            }

            ++m_poll_count;
            for (const VFSWatchEvent& ev : drained)
            {
                cb(ctx, ev);
            }
        }

        void StartThread() override {}
        void StopThread() override {}

        void InjectEvent(const VFSWatchEvent& ev)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push_back(ev);
        }

        bool WasRemoved(WatchHandle handle) const
        {
            for (WatchHandle h : m_removed)
            {
                if (h == handle)
                {
                    return true;
                }
            }
            return false;
        }

        int PollCount() const
        {
            return m_poll_count;
        }

    private:
        std::mutex                 m_mutex;
        std::vector<VFSWatchEvent> m_queue;
        std::vector<WatchHandle>   m_removed;
        WatchHandle                m_next_handle = 1;
        int                        m_poll_count  = 0;
    };

    VFSWatchEvent MakeEvent(const char* path, WatchEventKind kind, bool is_directory = false)
    {
        VFSWatchEvent ev{};
        ZEngine::Helpers::secure_strcpy(ev.Path, sizeof(ev.Path), path);
        ev.Kind        = kind;
        ev.IsDirectory = is_directory;
        return ev;
    }
} // namespace

class VFSFileWatcherTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_manager.Initialize(ZMega(2), {});
    }

    void TearDown() override
    {
        m_manager.Shutdown();
    }

    MemoryManager m_manager;
};

TEST_F(VFSFileWatcherTest, DebounceBurstCollapses)
{
    MockPlatformWatcher mock;
    VFSFileWatcher      watcher(&mock, std::chrono::milliseconds{50});
    watcher.Initialize(&m_manager.MainArena);

    int fired = 0;
    watcher.Watch("/project", true, [&](const VFSWatchEvent&) { ++fired; });

    for (int i = 0; i < 10; ++i)
    {
        mock.InjectEvent(MakeEvent("/project/foo.glb", WatchEventKind::Modified));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    watcher.Tick();
    EXPECT_EQ(fired, 0);
    EXPECT_EQ(watcher.PendingCount(), 1u);

    std::this_thread::sleep_for(std::chrono::milliseconds{60});
    watcher.Tick();
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(watcher.PendingCount(), 0u);

    std::this_thread::sleep_for(std::chrono::milliseconds{60});
    watcher.Tick();
    EXPECT_EQ(fired, 1);
}

TEST_F(VFSFileWatcherTest, TwoDistinctFilesFireTwice)
{
    MockPlatformWatcher mock;
    VFSFileWatcher      watcher(&mock, std::chrono::milliseconds{50});
    watcher.Initialize(&m_manager.MainArena);

    int fired = 0;
    watcher.Watch("/project", true, [&](const VFSWatchEvent&) { ++fired; });

    mock.InjectEvent(MakeEvent("/project/a.glb", WatchEventKind::Modified));
    mock.InjectEvent(MakeEvent("/project/b.glb", WatchEventKind::Modified));

    watcher.Tick();
    EXPECT_EQ(watcher.PendingCount(), 2u);

    std::this_thread::sleep_for(std::chrono::milliseconds{60});
    watcher.Tick();
    EXPECT_EQ(fired, 2);
}

TEST_F(VFSFileWatcherTest, UnwatchStopsEvents)
{
    MockPlatformWatcher mock;
    VFSFileWatcher      watcher(&mock, std::chrono::milliseconds{10});
    watcher.Initialize(&m_manager.MainArena);

    int               fired = 0;
    const WatchHandle h     = watcher.Watch("/project", true, [&](const VFSWatchEvent&) { ++fired; });

    watcher.Unwatch(h);
    EXPECT_TRUE(mock.WasRemoved(h));

    mock.InjectEvent(MakeEvent("/project/foo.glb", WatchEventKind::Created));
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    watcher.Tick();
    EXPECT_EQ(fired, 0);
}

TEST_F(VFSFileWatcherTest, UnwatchDiscardsAlreadyPendingEvents)
{
    MockPlatformWatcher mock;
    VFSFileWatcher      watcher(&mock, std::chrono::milliseconds{50});
    watcher.Initialize(&m_manager.MainArena);

    int               fired = 0;
    const WatchHandle h     = watcher.Watch("/project", true, [&](const VFSWatchEvent&) { ++fired; });

    mock.InjectEvent(MakeEvent("/project/foo.glb", WatchEventKind::Modified));
    watcher.Tick();
    EXPECT_EQ(watcher.PendingCount(), 1u);

    watcher.Unwatch(h);
    EXPECT_EQ(watcher.PendingCount(), 0u);

    std::this_thread::sleep_for(std::chrono::milliseconds{60});
    watcher.Tick();
    EXPECT_EQ(fired, 0);
}

TEST_F(VFSFileWatcherTest, OverflowSetsKind)
{
    MockPlatformWatcher mock;
    VFSFileWatcher      watcher(&mock, std::chrono::milliseconds{10});
    watcher.Initialize(&m_manager.MainArena);

    WatchEventKind received = WatchEventKind::Created;
    watcher.Watch("/project", true, [&](const VFSWatchEvent& ev) { received = ev.Kind; });

    mock.InjectEvent(MakeEvent("", WatchEventKind::Overflow));

    watcher.Tick();
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    watcher.Tick();
    EXPECT_EQ(received, WatchEventKind::Overflow);
}

TEST_F(VFSFileWatcherTest, OverflowBroadcastsToEveryWatch)
{
    MockPlatformWatcher mock;
    VFSFileWatcher      watcher(&mock, std::chrono::milliseconds{10});
    watcher.Initialize(&m_manager.MainArena);

    int fired_a = 0, fired_b = 0;
    watcher.Watch("/projectA", false, [&](const VFSWatchEvent&) { ++fired_a; });
    watcher.Watch("/projectB", false, [&](const VFSWatchEvent&) { ++fired_b; });

    mock.InjectEvent(MakeEvent("", WatchEventKind::Overflow));

    watcher.Tick();
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    watcher.Tick();

    EXPECT_EQ(fired_a, 1);
    EXPECT_EQ(fired_b, 1);
}

TEST_F(VFSFileWatcherTest, RenamePreservesOldPath)
{
    MockPlatformWatcher mock;
    VFSFileWatcher      watcher(&mock, std::chrono::milliseconds{10});
    watcher.Initialize(&m_manager.MainArena);

    VFSWatchEvent received{};
    watcher.Watch("/project", true, [&](const VFSWatchEvent& ev) { received = ev; });

    VFSWatchEvent ev = MakeEvent("/project/new.glb", WatchEventKind::Renamed);
    ZEngine::Helpers::secure_strcpy(ev.OldPath, sizeof(ev.OldPath), "/project/old.glb");
    mock.InjectEvent(ev);

    watcher.Tick();
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    watcher.Tick();

    EXPECT_STREQ(received.OldPath, "/project/old.glb");
    EXPECT_STREQ(received.Path, "/project/new.glb");
    EXPECT_EQ(received.Kind, WatchEventKind::Renamed);
}

TEST_F(VFSFileWatcherTest, MultipleWatchesRouteCorrectly)
{
    MockPlatformWatcher mock;
    VFSFileWatcher      watcher(&mock, std::chrono::milliseconds{10});
    watcher.Initialize(&m_manager.MainArena);

    int fired_a = 0, fired_b = 0;
    watcher.Watch("/projectA", false, [&](const VFSWatchEvent&) { ++fired_a; });
    watcher.Watch("/projectB", false, [&](const VFSWatchEvent&) { ++fired_b; });

    mock.InjectEvent(MakeEvent("/projectA/x.png", WatchEventKind::Created));
    mock.InjectEvent(MakeEvent("/projectB/y.png", WatchEventKind::Created));

    watcher.Tick();
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    watcher.Tick();

    EXPECT_EQ(fired_a, 1);
    EXPECT_EQ(fired_b, 1);
}

TEST_F(VFSFileWatcherTest, RoutingRespectsPathBoundariesAndPrefersNestedRoot)
{
    MockPlatformWatcher mock;
    VFSFileWatcher      watcher(&mock, std::chrono::milliseconds{10});
    watcher.Initialize(&m_manager.MainArena);

    int fired_a = 0, fired_ab = 0, fired_nested = 0;
    watcher.Watch("/projectA", true, [&](const VFSWatchEvent&) { ++fired_a; });
    watcher.Watch("/projectAB", true, [&](const VFSWatchEvent&) { ++fired_ab; });
    watcher.Watch("/projectA/assets", true, [&](const VFSWatchEvent&) { ++fired_nested; });

    mock.InjectEvent(MakeEvent("/projectAB/x.png", WatchEventKind::Created));
    mock.InjectEvent(MakeEvent("/projectA/assets/y.png", WatchEventKind::Created));
    mock.InjectEvent(MakeEvent("/elsewhere/z.png", WatchEventKind::Created));

    watcher.Tick();
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    watcher.Tick();

    EXPECT_EQ(fired_ab, 1);
    EXPECT_EQ(fired_nested, 1);
    EXPECT_EQ(fired_a, 0);
}

TEST_F(VFSFileWatcherTest, NoTickNoFire)
{
    MockPlatformWatcher mock;
    VFSFileWatcher      watcher(&mock, std::chrono::milliseconds{10});
    watcher.Initialize(&m_manager.MainArena);

    int fired = 0;
    watcher.Watch("/project", true, [&](const VFSWatchEvent&) { ++fired; });

    mock.InjectEvent(MakeEvent("/project/foo.glb", WatchEventKind::Modified));
    std::this_thread::sleep_for(std::chrono::milliseconds{20});

    EXPECT_EQ(fired, 0);
    EXPECT_EQ(mock.PollCount(), 0);
    EXPECT_EQ(watcher.PendingCount(), 0u);
}

TEST_F(VFSFileWatcherTest, IsDirectoryFlagPropagated)
{
    MockPlatformWatcher mock;
    VFSFileWatcher      watcher(&mock, std::chrono::milliseconds{10});
    watcher.Initialize(&m_manager.MainArena);

    bool got_dir = false;
    watcher.Watch("/project", true, [&](const VFSWatchEvent& ev) { got_dir = ev.IsDirectory; });

    mock.InjectEvent(MakeEvent("/project/newfolder", WatchEventKind::Created, /*is_directory=*/true));

    watcher.Tick(); // absorb
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    watcher.Tick(); // emit

    EXPECT_TRUE(got_dir);
}

TEST_F(VFSFileWatcherTest, LatestEventWinsWithinWindow)
{
    MockPlatformWatcher mock;
    VFSFileWatcher      watcher(&mock, std::chrono::milliseconds{50});
    watcher.Initialize(&m_manager.MainArena);

    WatchEventKind received = WatchEventKind::Overflow;
    watcher.Watch("/project", true, [&](const VFSWatchEvent& ev) { received = ev.Kind; });

    mock.InjectEvent(MakeEvent("/project/foo.glb", WatchEventKind::Created));
    mock.InjectEvent(MakeEvent("/project/foo.glb", WatchEventKind::Modified));
    mock.InjectEvent(MakeEvent("/project/foo.glb", WatchEventKind::Deleted));

    watcher.Tick();
    std::this_thread::sleep_for(std::chrono::milliseconds{60});
    watcher.Tick();
    EXPECT_EQ(received, WatchEventKind::Deleted);
}

TEST_F(VFSFileWatcherTest, ContinuousActivityKeepsResettingTheTimer)
{
    MockPlatformWatcher mock;
    VFSFileWatcher      watcher(&mock, std::chrono::milliseconds{100});
    watcher.Initialize(&m_manager.MainArena);

    int fired = 0;
    watcher.Watch("/project", true, [&](const VFSWatchEvent&) { ++fired; });

    for (int i = 0; i < 5; ++i)
    {
        mock.InjectEvent(MakeEvent("/project/main.cpp", WatchEventKind::Modified));
        watcher.Tick();
    }
    watcher.Tick();
    EXPECT_EQ(fired, 0);

    // Editor pauses — wait well past the debounce window.
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    watcher.Tick();
    EXPECT_EQ(fired, 1);
}

TEST_F(VFSFileWatcherTest, EventOutsideAllRootsIsDropped)
{
    MockPlatformWatcher mock;
    VFSFileWatcher      watcher(&mock, std::chrono::milliseconds{10});
    watcher.Initialize(&m_manager.MainArena);

    int fired = 0;
    watcher.Watch("/project", true, [&](const VFSWatchEvent&) { ++fired; });

    mock.InjectEvent(MakeEvent("/somewhere/else/foo.glb", WatchEventKind::Modified));
    watcher.Tick();
    EXPECT_EQ(watcher.PendingCount(), 0u);

    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    watcher.Tick();
    EXPECT_EQ(fired, 0);
}

TEST_F(VFSFileWatcherTest, TrailingSlashRootStillMatches)
{
    MockPlatformWatcher mock;
    VFSFileWatcher      watcher(&mock, std::chrono::milliseconds{10});
    watcher.Initialize(&m_manager.MainArena);

    int fired = 0;
    watcher.Watch("/project/", true, [&](const VFSWatchEvent&) { ++fired; });

    mock.InjectEvent(MakeEvent("/project/foo.glb", WatchEventKind::Modified));

    watcher.Tick(); // absorb
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    watcher.Tick(); // emit

    EXPECT_EQ(fired, 1);
}
