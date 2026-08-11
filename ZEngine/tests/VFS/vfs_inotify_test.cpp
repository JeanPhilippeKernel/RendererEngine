#if defined(__linux__)
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/Platform/VFSInotifyWatcher.h>
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
    class VFSInotifyWatcherTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_manager.Initialize(ZMega(2), {});
            m_events.init(&m_manager.MainArena, 32);

            std::error_code ec;
            m_root = std::filesystem::temp_directory_path() / "zengine_vfs_inotify_tests";
            std::filesystem::remove_all(m_root, ec);
            std::filesystem::create_directories(m_root, ec);
        }

        void TearDown() override
        {
            std::error_code ec;
            std::filesystem::remove_all(m_root, ec);

            m_manager.Shutdown();
        }

        void Init(VFSInotifyWatcher& watcher)
        {
            watcher.Initialize(&m_manager.MainArena);
        }

        void WriteFile(const std::filesystem::path& path, const char* contents)
        {
            std::ofstream out(path);
            out << contents;
        }

        void Drain(VFSInotifyWatcher& watcher)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{120});

            m_events.clear();
            watcher.Poll([](void* ctx, const VFSWatchEvent& ev) { reinterpret_cast<VFSInotifyWatcherTest*>(ctx)->m_events.push(ev); }, this);
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

        size_t EventCount() const
        {
            return m_events.size();
        }

        MemoryManager         m_manager;
        Array<VFSWatchEvent>  m_events;
        std::filesystem::path m_root;
    };
} // namespace

TEST_F(VFSInotifyWatcherTest, ReportsFileCreation)
{
    VFSInotifyWatcher watcher;
    Init(watcher);
    ASSERT_TRUE(watcher.IsValid());

    const WatchHandle handle = watcher.AddWatch(m_root.c_str(), /*recursive=*/false);
    ASSERT_NE(handle, INVALID_WATCH_HANDLE);

    const auto file = m_root / "created.glb";
    WriteFile(file, "x");

    Drain(watcher);
    EXPECT_TRUE(Contains(file.c_str(), WatchEventKind::Created));
}

TEST_F(VFSInotifyWatcherTest, ReportsFileModification)
{
    VFSInotifyWatcher watcher;
    Init(watcher);

    const auto file = m_root / "existing.glb";
    WriteFile(file, "first");

    ASSERT_NE(watcher.AddWatch(m_root.c_str(), false), INVALID_WATCH_HANDLE);

    WriteFile(file, "second");

    Drain(watcher);
    EXPECT_TRUE(Contains(file.c_str(), WatchEventKind::Modified));
}

TEST_F(VFSInotifyWatcherTest, ReportsFileDeletion)
{
    VFSInotifyWatcher watcher;
    Init(watcher);

    const auto file = m_root / "doomed.glb";
    WriteFile(file, "x");

    ASSERT_NE(watcher.AddWatch(m_root.c_str(), false), INVALID_WATCH_HANDLE);

    std::error_code ec;
    std::filesystem::remove(file, ec);

    Drain(watcher);
    EXPECT_TRUE(Contains(file.c_str(), WatchEventKind::Deleted));
}

TEST_F(VFSInotifyWatcherTest, RenameWithinRootFusesIntoSingleEvent)
{
    VFSInotifyWatcher watcher;
    Init(watcher);

    const auto original = m_root / "old.glb";
    const auto renamed  = m_root / "new.glb";
    WriteFile(original, "x");

    ASSERT_NE(watcher.AddWatch(m_root.c_str(), false), INVALID_WATCH_HANDLE);

    std::error_code ec;
    std::filesystem::rename(original, renamed, ec);
    ASSERT_FALSE(ec);

    Drain(watcher);

    bool found = false;
    for (size_t i = 0; i < EventCount(); ++i)
    {
        if (m_events[i].Kind == WatchEventKind::Renamed)
        {
            EXPECT_STREQ(renamed.c_str(), m_events[i].Path);
            EXPECT_STREQ(original.c_str(), m_events[i].OldPath);
            found = true;
        }
    }
    EXPECT_TRUE(found) << "no Renamed event produced";
}

TEST_F(VFSInotifyWatcherTest, MoveOutOfRootBecomesDelete)
{
    VFSInotifyWatcher watcher;
    Init(watcher);

    const auto inside  = m_root / "leaving.glb";
    const auto outside = std::filesystem::temp_directory_path() / "zengine_vfs_inotify_outside.glb";
    WriteFile(inside, "x");

    ASSERT_NE(watcher.AddWatch(m_root.c_str(), false), INVALID_WATCH_HANDLE);

    std::error_code ec;
    std::filesystem::rename(inside, outside, ec);

    Drain(watcher);
    EXPECT_TRUE(Contains(inside.c_str(), WatchEventKind::Deleted));

    std::filesystem::remove(outside, ec);
}

TEST_F(VFSInotifyWatcherTest, RecursiveWatchCoversExistingSubdirectories)
{
    std::error_code ec;
    std::filesystem::create_directories(m_root / "assets" / "meshes", ec);

    VFSInotifyWatcher watcher;
    Init(watcher);
    ASSERT_NE(watcher.AddWatch(m_root.c_str(), /*recursive=*/true), INVALID_WATCH_HANDLE);
    EXPECT_EQ(watcher.DescriptorCount(), 3u);

    const auto nested = m_root / "assets" / "meshes" / "deep.glb";
    WriteFile(nested, "x");

    Drain(watcher);
    EXPECT_TRUE(Contains(nested.c_str(), WatchEventKind::Created));
}

TEST_F(VFSInotifyWatcherTest, NonRecursiveWatchIgnoresSubdirectories)
{
    std::error_code ec;
    std::filesystem::create_directories(m_root / "assets", ec);

    VFSInotifyWatcher watcher;
    Init(watcher);
    ASSERT_NE(watcher.AddWatch(m_root.c_str(), /*recursive=*/false), INVALID_WATCH_HANDLE);
    EXPECT_EQ(watcher.DescriptorCount(), 1u);

    const auto nested = m_root / "assets" / "hidden.glb";
    WriteFile(nested, "x");

    Drain(watcher);
    EXPECT_FALSE(ContainsPath(nested.c_str()));
}

TEST_F(VFSInotifyWatcherTest, NewDirectoryGetsWatchedAutomatically)
{
    VFSInotifyWatcher watcher;
    Init(watcher);
    ASSERT_NE(watcher.AddWatch(m_root.c_str(), /*recursive=*/true), INVALID_WATCH_HANDLE);
    EXPECT_EQ(watcher.DescriptorCount(), 1u);

    std::error_code ec;
    std::filesystem::create_directory(m_root / "late", ec);
    Drain(watcher);

    EXPECT_EQ(watcher.DescriptorCount(), 2u);

    const auto nested = m_root / "late" / "inside.glb";
    WriteFile(nested, "x");

    Drain(watcher);
    EXPECT_TRUE(Contains(nested.c_str(), WatchEventKind::Created));
}

TEST_F(VFSInotifyWatcherTest, RemoveWatchReleasesEveryDescriptorInTheSubtree)
{
    std::error_code ec;
    std::filesystem::create_directories(m_root / "a" / "b", ec);

    VFSInotifyWatcher watcher;
    Init(watcher);
    const WatchHandle handle = watcher.AddWatch(m_root.c_str(), /*recursive=*/true);
    ASSERT_NE(handle, INVALID_WATCH_HANDLE);
    EXPECT_EQ(watcher.DescriptorCount(), 3u);

    watcher.RemoveWatch(handle);
    EXPECT_EQ(watcher.DescriptorCount(), 0u);

    WriteFile(m_root / "ignored.glb", "x");
    Drain(watcher);
    EXPECT_EQ(EventCount(), 0u);
}

TEST_F(VFSInotifyWatcherTest, AddWatchOnMissingDirectoryFails)
{
    VFSInotifyWatcher watcher;
    Init(watcher);
    EXPECT_EQ(watcher.AddWatch((m_root / "does_not_exist").c_str(), false), INVALID_WATCH_HANDLE);
}

TEST_F(VFSInotifyWatcherTest, AddWatchBeforeInitializeFails)
{
    VFSInotifyWatcher watcher;
    EXPECT_FALSE(watcher.IsValid());
    EXPECT_EQ(watcher.AddWatch(m_root.c_str(), false), INVALID_WATCH_HANDLE);
}

TEST_F(VFSInotifyWatcherTest, BackgroundThreadDeliversEvents)
{
    VFSInotifyWatcher watcher;
    Init(watcher);
    ASSERT_NE(watcher.AddWatch(m_root.c_str(), /*recursive=*/true), INVALID_WATCH_HANDLE);

    watcher.StartThread();

    const auto file = m_root / "threaded.glb";
    WriteFile(file, "x");

    Drain(watcher);
    watcher.StopThread();

    EXPECT_TRUE(Contains(file.c_str(), WatchEventKind::Created));
}

TEST_F(VFSInotifyWatcherTest, StopThreadIsSafeWithoutStart)
{
    VFSInotifyWatcher watcher;
    Init(watcher);
    watcher.StopThread();
    SUCCEED();
}
#endif // __linux__
