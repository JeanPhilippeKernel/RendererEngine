#if defined(_WIN32)
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/Platform/VFSRDCWatcher.h>
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
    struct ExpectedPath
    {
        char Value[MAX_FILE_PATH_COUNT] = {};

        explicit ExpectedPath(const std::filesystem::path& path)
        {
            const int written = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, Value, sizeof(Value) - 1, nullptr, nullptr);
            if (written <= 0)
            {
                Value[0] = '\0';
            }

            for (char* c = Value; *c != '\0'; ++c)
            {
                if (*c == '\\')
                {
                    *c = '/';
                }
            }
        }

        const char* Get() const
        {
            return Value;
        }
    };

    class VFSRDCWatcherTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_manager.Initialize(ZMega(2), {});
            m_events.init(&m_manager.MainArena, 32);

            std::error_code ec;
            m_root = std::filesystem::temp_directory_path() / "zengine_vfs_rdc_tests";
            std::filesystem::remove_all(m_root, ec);
            std::filesystem::create_directories(m_root, ec);

            const ExpectedPath root{m_root};
            ZEngine::Helpers::secure_strcpy(m_root_native, sizeof(m_root_native), root.Get());
        }

        void TearDown() override
        {
            std::error_code ec;
            std::filesystem::remove_all(m_root, ec);

            m_manager.Shutdown();
        }

        void Init(VFSRDCWatcher& watcher)
        {
            watcher.Initialize(&m_manager.MainArena);
        }

        void WriteFile(const std::filesystem::path& path, const char* contents)
        {
            std::ofstream out(path);
            out << contents;
        }

        const char* RootPath() const
        {
            return m_root_native;
        }

        void Drain(VFSRDCWatcher& watcher)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{200});

            m_events.clear();
            watcher.Poll([](void* ctx, const VFSWatchEvent& ev) { reinterpret_cast<VFSRDCWatcherTest*>(ctx)->m_events.push(ev); }, this);
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
        char                  m_root_native[MAX_FILE_PATH_COUNT] = {};
    };
} // namespace

TEST_F(VFSRDCWatcherTest, ReportsFileCreation)
{
    VFSRDCWatcher watcher;
    Init(watcher);
    ASSERT_TRUE(watcher.IsValid());

    const WatchHandle handle = watcher.AddWatch(RootPath(), /*recursive=*/false);
    ASSERT_NE(handle, INVALID_WATCH_HANDLE);
    EXPECT_EQ(watcher.WatchCount(), 1u);

    const auto         file = m_root / "created.glb";
    const ExpectedPath expected{file};
    WriteFile(file, "x");

    Drain(watcher);
    EXPECT_TRUE(Contains(expected.Get(), WatchEventKind::Created));
}

TEST_F(VFSRDCWatcherTest, ReportsFileModification)
{
    VFSRDCWatcher watcher;
    Init(watcher);

    const auto         file = m_root / "existing.glb";
    const ExpectedPath expected{file};
    WriteFile(file, "first");

    ASSERT_NE(watcher.AddWatch(RootPath(), false), INVALID_WATCH_HANDLE);

    WriteFile(file, "second");

    Drain(watcher);
    EXPECT_TRUE(Contains(expected.Get(), WatchEventKind::Modified));
}

TEST_F(VFSRDCWatcherTest, ReportsFileDeletion)
{
    VFSRDCWatcher watcher;
    Init(watcher);

    const auto         file = m_root / "doomed.glb";
    const ExpectedPath expected{file};
    WriteFile(file, "x");

    ASSERT_NE(watcher.AddWatch(RootPath(), false), INVALID_WATCH_HANDLE);

    std::error_code ec;
    std::filesystem::remove(file, ec);

    Drain(watcher);
    EXPECT_TRUE(Contains(expected.Get(), WatchEventKind::Deleted));
}

TEST_F(VFSRDCWatcherTest, RenameFusesIntoSingleEvent)
{
    VFSRDCWatcher watcher;
    Init(watcher);

    const auto         original = m_root / "old.glb";
    const auto         renamed  = m_root / "new.glb";
    const ExpectedPath expected_original{original};
    const ExpectedPath expected_renamed{renamed};
    WriteFile(original, "x");

    ASSERT_NE(watcher.AddWatch(RootPath(), false), INVALID_WATCH_HANDLE);

    std::error_code ec;
    std::filesystem::rename(original, renamed, ec);
    ASSERT_FALSE(ec);

    Drain(watcher);

    bool found = false;
    for (size_t i = 0; i < EventCount(); ++i)
    {
        if (m_events[i].Kind == WatchEventKind::Renamed)
        {
            EXPECT_STREQ(expected_renamed.Get(), m_events[i].Path);
            EXPECT_STREQ(expected_original.Get(), m_events[i].OldPath);
            found = true;
        }
    }
    EXPECT_TRUE(found) << "no Renamed event produced";
}

TEST_F(VFSRDCWatcherTest, RecursiveWatchIsASingleHandleAndSeesNestedFiles)
{
    std::error_code ec;
    std::filesystem::create_directories(m_root / "assets" / "meshes", ec);

    VFSRDCWatcher watcher;
    Init(watcher);
    ASSERT_NE(watcher.AddWatch(RootPath(), /*recursive=*/true), INVALID_WATCH_HANDLE);
    EXPECT_EQ(watcher.WatchCount(), 1u);

    const auto         nested = m_root / "assets" / "meshes" / "deep.glb";
    const ExpectedPath expected{nested};
    WriteFile(nested, "x");

    Drain(watcher);
    EXPECT_TRUE(Contains(expected.Get(), WatchEventKind::Created));
}

TEST_F(VFSRDCWatcherTest, NonRecursiveWatchIgnoresSubdirectories)
{
    std::error_code ec;
    std::filesystem::create_directories(m_root / "assets", ec);

    VFSRDCWatcher watcher;
    Init(watcher);
    ASSERT_NE(watcher.AddWatch(RootPath(), /*recursive=*/false), INVALID_WATCH_HANDLE);

    const auto         nested = m_root / "assets" / "hidden.glb";
    const ExpectedPath expected{nested};
    WriteFile(nested, "x");

    Drain(watcher);
    EXPECT_FALSE(ContainsPath(expected.Get()));
}

TEST_F(VFSRDCWatcherTest, NewDirectoryIsCoveredWithoutReregistering)
{
    VFSRDCWatcher watcher;
    Init(watcher);
    ASSERT_NE(watcher.AddWatch(RootPath(), /*recursive=*/true), INVALID_WATCH_HANDLE);

    std::error_code ec;
    std::filesystem::create_directory(m_root / "late", ec);
    Drain(watcher);

    const auto         nested = m_root / "late" / "inside.glb";
    const ExpectedPath expected{nested};
    WriteFile(nested, "x");

    Drain(watcher);
    EXPECT_TRUE(Contains(expected.Get(), WatchEventKind::Created));
}

TEST_F(VFSRDCWatcherTest, RemoveWatchStopsEvents)
{
    VFSRDCWatcher watcher;
    Init(watcher);

    const WatchHandle handle = watcher.AddWatch(RootPath(), /*recursive=*/true);
    ASSERT_NE(handle, INVALID_WATCH_HANDLE);
    EXPECT_EQ(watcher.WatchCount(), 1u);

    watcher.RemoveWatch(handle);
    EXPECT_EQ(watcher.WatchCount(), 0u);

    WriteFile(m_root / "ignored.glb", "x");
    Drain(watcher);
    EXPECT_EQ(EventCount(), 0u);
}

TEST_F(VFSRDCWatcherTest, AddWatchOnMissingDirectoryFails)
{
    VFSRDCWatcher watcher;
    Init(watcher);

    const ExpectedPath missing{m_root / "does_not_exist"};
    EXPECT_EQ(watcher.AddWatch(missing.Get(), false), INVALID_WATCH_HANDLE);
}

TEST_F(VFSRDCWatcherTest, AddWatchBeforeInitializeFails)
{
    VFSRDCWatcher watcher;
    EXPECT_FALSE(watcher.IsValid());
    EXPECT_EQ(watcher.AddWatch(RootPath(), false), INVALID_WATCH_HANDLE);
}

TEST_F(VFSRDCWatcherTest, BackgroundThreadDeliversEvents)
{
    VFSRDCWatcher watcher;
    Init(watcher);
    ASSERT_NE(watcher.AddWatch(RootPath(), /*recursive=*/true), INVALID_WATCH_HANDLE);

    watcher.StartThread();

    const auto         file = m_root / "threaded.glb";
    const ExpectedPath expected{file};
    WriteFile(file, "x");

    Drain(watcher);
    watcher.StopThread();

    EXPECT_TRUE(Contains(expected.Get(), WatchEventKind::Created));
}

TEST_F(VFSRDCWatcherTest, StopThreadIsSafeWithoutStart)
{
    VFSRDCWatcher watcher;
    Init(watcher);
    watcher.StopThread();
    SUCCEED();
}
#endif // _WIN32
