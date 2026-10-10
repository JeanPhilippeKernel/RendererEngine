#if defined(__APPLE__)
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/Platform/VFSFSEventsWatcher.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace ZEngine::Core::VFS;
using ZEngine::Core::Containers::Array;
using ZEngine::Core::Memory::MemoryManager;
using ZEngine::Helpers::secure_strcmp;

namespace
{
    class VFSFSEventsWatcherTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_manager.Initialize(ZMega(2), {});
            m_events.init(&m_manager.MainArena, 32);

            std::error_code ec;
            m_root = std::filesystem::temp_directory_path() / "zengine_vfs_fsevents_tests";
            std::filesystem::remove_all(m_root, ec);
            std::filesystem::create_directories(m_root, ec);
            m_root = std::filesystem::canonical(m_root, ec);
        }

        void TearDown() override
        {
            std::error_code ec;
            std::filesystem::remove_all(m_root, ec);

            m_manager.Shutdown();
        }

        void Init(VFSFSEventsWatcher& watcher)
        {
            watcher.Initialize(&m_manager.MainArena);
        }

        void WriteFile(const std::filesystem::path& path, const char* contents)
        {
            std::ofstream out(path);
            out << contents;
        }

        void Drain(VFSFSEventsWatcher& watcher, int timeout_ms = 3000)
        {
            // Poll in 50ms increments, accumulating events, until at least one
            // arrives or the deadline passes. Do NOT clear between polls — on slow
            // CI runners FSEvents may batch events across multiple Poll calls.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{timeout_ms};
            do
            {
                std::this_thread::sleep_for(std::chrono::milliseconds{50});
                watcher.Poll([](void* ctx, const VFSWatchEvent& ev) { reinterpret_cast<VFSFSEventsWatcherTest*>(ctx)->m_events.push(ev); }, this);
            } while (m_events.empty() && std::chrono::steady_clock::now() < deadline);
        }

        bool Contains(const char* path, WatchEventKind kind) const
        {
            for (size_t i = 0; i < m_events.size(); ++i)
            {
                if (m_events[i].Kind == kind && secure_strcmp(m_events[i].Path, path) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        bool ContainsPath(const char* path) const
        {
            for (size_t i = 0; i < m_events.size(); ++i)
            {
                if (secure_strcmp(m_events[i].Path, path) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        void ClearEvents()
        {
            m_events.clear();
        }

        size_t EventCount() const
        {
            return m_events.size();
        }

        MemoryManager         m_manager;
        Array<VFSWatchEvent>  m_events;
        std::filesystem::path m_root;
    };
} // namespace

TEST_F(VFSFSEventsWatcherTest, ReportsFileCreation)
{
    VFSFSEventsWatcher watcher;
    Init(watcher);
    ASSERT_TRUE(watcher.IsValid());

    const WatchHandle handle = watcher.AddWatch(m_root.c_str(), /*recursive=*/true);
    ASSERT_NE(handle, INVALID_WATCH_HANDLE);
    EXPECT_EQ(watcher.WatchCount(), 1u);

    watcher.StartThread();

    const auto file = m_root / "created.glb";
    WriteFile(file, "x");

    Drain(watcher);
    watcher.StopThread();

    EXPECT_TRUE(ContainsPath(file.c_str()));
}

TEST_F(VFSFSEventsWatcherTest, ReportsFileModification)
{
    VFSFSEventsWatcher watcher;
    Init(watcher);

    const auto file = m_root / "existing.glb";
    WriteFile(file, "first");

    ASSERT_NE(watcher.AddWatch(m_root.c_str(), true), INVALID_WATCH_HANDLE);
    watcher.StartThread();
    Drain(watcher);
    ClearEvents();

    WriteFile(file, "second");

    Drain(watcher);
    watcher.StopThread();

    EXPECT_TRUE(ContainsPath(file.c_str()));
}

TEST_F(VFSFSEventsWatcherTest, DeletionIsClassifiedByStat)
{
    VFSFSEventsWatcher watcher;
    Init(watcher);

    const auto file = m_root / "doomed.glb";
    WriteFile(file, "x");

    ASSERT_NE(watcher.AddWatch(m_root.c_str(), true), INVALID_WATCH_HANDLE);
    watcher.StartThread();
    Drain(watcher);
    ClearEvents();

    std::error_code ec;
    std::filesystem::remove(file, ec);

    Drain(watcher);
    watcher.StopThread();

    EXPECT_TRUE(Contains(file.c_str(), WatchEventKind::Deleted));
}

TEST_F(VFSFSEventsWatcherTest, RenameArrivesAsTwoSeparateEvents)
{
    VFSFSEventsWatcher watcher;
    Init(watcher);

    const auto original = m_root / "old.glb";
    const auto renamed  = m_root / "new.glb";
    WriteFile(original, "x");

    ASSERT_NE(watcher.AddWatch(m_root.c_str(), true), INVALID_WATCH_HANDLE);
    watcher.StartThread();
    Drain(watcher);
    ClearEvents();

    std::error_code ec;
    std::filesystem::rename(original, renamed, ec);
    ASSERT_FALSE(ec);

    Drain(watcher);
    watcher.StopThread();

    EXPECT_TRUE(Contains(original.c_str(), WatchEventKind::Deleted));
    EXPECT_TRUE(ContainsPath(renamed.c_str()));

    for (size_t i = 0; i < EventCount(); ++i)
    {
        EXPECT_NE(m_events[i].Kind, WatchEventKind::Renamed) << "FSEvents cannot pair renames";
    }
}

TEST_F(VFSFSEventsWatcherTest, SubtreeIsCoveredIncludingNewDirectories)
{
    std::error_code ec;
    std::filesystem::create_directories(m_root / "assets" / "meshes", ec);

    VFSFSEventsWatcher watcher;
    Init(watcher);
    ASSERT_NE(watcher.AddWatch(m_root.c_str(), /*recursive=*/true), INVALID_WATCH_HANDLE);
    EXPECT_EQ(watcher.WatchCount(), 1u);

    watcher.StartThread();

    const auto nested = m_root / "assets" / "meshes" / "deep.glb";
    WriteFile(nested, "x");
    Drain(watcher);
    EXPECT_TRUE(ContainsPath(nested.c_str()));

    std::filesystem::create_directory(m_root / "late", ec);
    Drain(watcher);

    const auto late_file = m_root / "late" / "inside.glb";
    WriteFile(late_file, "x");
    Drain(watcher);
    watcher.StopThread();

    EXPECT_TRUE(ContainsPath(late_file.c_str()));
}

TEST_F(VFSFSEventsWatcherTest, RemoveWatchStopsEvents)
{
    VFSFSEventsWatcher watcher;
    Init(watcher);

    const WatchHandle handle = watcher.AddWatch(m_root.c_str(), true);
    ASSERT_NE(handle, INVALID_WATCH_HANDLE);
    watcher.StartThread();
    Drain(watcher);

    watcher.RemoveWatch(handle);
    EXPECT_EQ(watcher.WatchCount(), 0u);
    ClearEvents();

    WriteFile(m_root / "ignored.glb", "x");
    Drain(watcher);
    watcher.StopThread();

    EXPECT_EQ(EventCount(), 0u);
}

TEST_F(VFSFSEventsWatcherTest, TwoRootsBothStayLiveAcrossStreamRebuild)
{
    std::error_code ec;
    const auto      first  = m_root / "first";
    const auto      second = m_root / "second";
    std::filesystem::create_directories(first, ec);
    std::filesystem::create_directories(second, ec);

    VFSFSEventsWatcher watcher;
    Init(watcher);
    ASSERT_NE(watcher.AddWatch(first.c_str(), true), INVALID_WATCH_HANDLE);
    ASSERT_NE(watcher.AddWatch(second.c_str(), true), INVALID_WATCH_HANDLE);
    EXPECT_EQ(watcher.WatchCount(), 2u);

    watcher.StartThread();

    const auto a = first / "a.glb";
    const auto b = second / "b.glb";
    WriteFile(a, "x");
    WriteFile(b, "x");

    Drain(watcher);
    watcher.StopThread();

    EXPECT_TRUE(ContainsPath(a.c_str()));
    EXPECT_TRUE(ContainsPath(b.c_str()));
}

TEST_F(VFSFSEventsWatcherTest, AddWatchBeforeInitializeFails)
{
    VFSFSEventsWatcher watcher;
    EXPECT_FALSE(watcher.IsValid());
    EXPECT_EQ(watcher.AddWatch(m_root.c_str(), true), INVALID_WATCH_HANDLE);
}

TEST_F(VFSFSEventsWatcherTest, StopThreadIsSafeWithoutStart)
{
    VFSFSEventsWatcher watcher;
    Init(watcher);
    watcher.StopThread();
    SUCCEED();
}
#endif // __APPLE__
