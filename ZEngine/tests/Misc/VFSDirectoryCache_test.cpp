#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSDirectoryCache.h>
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>

using namespace ZEngine;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::Core::Memory;
using ZEngine::Core::Containers::Array;
using ZEngine::Core::Containers::ArrayView;

class VFSDirectoryCacheTest : public ::testing::Test
{
protected:
    MemoryManager     m_manager;
    ArenaAllocator*   m_arena = nullptr;
    VFSDirectoryCache m_cache;

    void              SetUp() override
    {
        m_manager.Initialize({.BufferSize = ZMega(16)});
        m_arena = &m_manager.MainArena;
        m_cache.Initialize(m_arena);
    }

    void TearDown() override
    {
        m_manager.Shutdown();
    }

    VFSPath P(const char* raw)
    {
        auto result = VFSPath::Parse(raw);
        EXPECT_TRUE(result.Succeeded()) << "failed to parse path: " << raw;
        return result.Value();
    }

    VFSDirEntry Entry(const char* path, bool is_directory)
    {
        VFSDirEntry entry;
        entry.Path             = P(path);
        entry.IsDirectory      = is_directory;
        entry.Stat.IsDirectory = is_directory;
        return entry;
    }

    Array<VFSDirEntry> Listing(std::initializer_list<VFSDirEntry> items)
    {
        Array<VFSDirEntry> entries;
        entries.init(m_arena, items.size() == 0 ? 1 : items.size());
        for (const auto& item : items)
        {
            entries.push(item);
        }
        return entries;
    }
};

TEST_F(VFSDirectoryCacheTest, GetListing_EmptyCache_ReturnsEmptySpan)
{
    auto view = m_cache.GetListing(P("/not/scanned/yet"));
    EXPECT_EQ(view.size(), 0u);
}

TEST_F(VFSDirectoryCacheTest, SetListing_then_GetListing)
{
    m_cache.SetListing(P("/assets"), Listing({Entry("/assets/a.png", false), Entry("/assets/textures", true)}));

    auto view = m_cache.GetListing(P("/assets"));
    EXPECT_EQ(view.size(), 2u);
}

TEST_F(VFSDirectoryCacheTest, IsStale_BeforeSet_True)
{
    EXPECT_TRUE(m_cache.IsStale(P("/never/set")));
}

TEST_F(VFSDirectoryCacheTest, IsStale_AfterSet_False)
{
    m_cache.SetListing(P("/d"), Listing({}));
    EXPECT_FALSE(m_cache.IsStale(P("/d")));
}

TEST_F(VFSDirectoryCacheTest, Invalidate_MarksStale_KeepsData)
{
    m_cache.SetListing(P("/d"), Listing({Entry("/d/file.txt", false)}));

    m_cache.Invalidate(P("/d"));

    EXPECT_TRUE(m_cache.IsStale(P("/d")));
    EXPECT_EQ(m_cache.GetListing(P("/d")).size(), 1u);
}

TEST_F(VFSDirectoryCacheTest, SetListing_Overwrites_Existing)
{
    m_cache.SetListing(P("/d"), Listing({Entry("/d/old.txt", false)}));
    m_cache.SetListing(P("/d"), Listing({Entry("/d/a.txt", false), Entry("/d/b.txt", false)}));

    EXPECT_EQ(m_cache.GetListing(P("/d")).size(), 2u);
    EXPECT_EQ(m_cache.Size(), 1u);
}

TEST_F(VFSDirectoryCacheTest, Clear_EmptiesAllEntries)
{
    m_cache.SetListing(P("/a"), Listing({Entry("/a/x", false)}));
    m_cache.SetListing(P("/b"), Listing({Entry("/b/x", false)}));
    m_cache.SetListing(P("/c"), Listing({Entry("/c/x", false)}));
    ASSERT_EQ(m_cache.Size(), 3u);

    m_cache.Clear();
    EXPECT_EQ(m_cache.Size(), 0u);
}

TEST_F(VFSDirectoryCacheTest, ForEachDir_VisitsAllCachedDirectories)
{
    m_cache.SetListing(P("/a"), Listing({Entry("/a/x", false)}));
    m_cache.SetListing(P("/b"), Listing({Entry("/b/x", false), Entry("/b/y", false)}));

    size_t dir_count   = 0;
    size_t entry_count = 0;
    m_cache.ForEachDir([&](const VFSPath& /*dir*/, ArrayView<const VFSDirEntry> entries) {
        ++dir_count;
        entry_count += entries.size();
    });

    EXPECT_EQ(dir_count, 2u);
    EXPECT_EQ(entry_count, 3u);
}

TEST_F(VFSDirectoryCacheTest, ConcurrentReadWrite_NoDeadlock)
{
    VFSPath                  dirs[4]     = {P("/dir0"), P("/dir1"), P("/dir2"), P("/dir3")};

    constexpr int            kIterations = 200;
    std::vector<std::thread> threads;
    threads.reserve(8);

    for (int w = 0; w < 4; ++w)
    {
        threads.emplace_back([&, w] {
            for (int i = 0; i < kIterations; ++i)
            {
                m_cache.SetListing(dirs[w], Array<VFSDirEntry>{});
            }
        });
    }

    for (int r = 0; r < 4; ++r)
    {
        threads.emplace_back([&, r] {
            for (int i = 0; i < kIterations; ++i)
            {
                volatile size_t n = m_cache.GetListing(dirs[r]).size();
                (void) n;
            }
        });
    }

    for (auto& thread : threads)
    {
        thread.join();
    }

    EXPECT_EQ(m_cache.Size(), 4u);
}
