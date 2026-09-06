#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Core/VFS/VFSScanner.h>
#include <ZEngine/ZEngineDef.h>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <thread>

using namespace ZEngine;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::Core::Memory;
using ZEngine::Core::Containers::Array;
using ZEngine::Core::Containers::ArrayView;
using ZEngine::Core::Containers::UnorderedHashMap;

namespace
{

    struct MockVFSContext : IVFSContext
    {
        UnorderedHashMap<uint64_t, Array<VFSDirEntry>> Tree;
        ArenaAllocator*                                TreeArena = nullptr;
        std::atomic<int>                               Concurrent{0};
        std::atomic<int>                               MaxConcurrent{0};
        std::atomic<int>                               ListCalls{0};
        std::chrono::milliseconds                      ListDelay{0};

        void                                           Initialize(ArenaAllocator* arena)
        {
            TreeArena = arena;
            Tree.init(arena, 256);
        }

        void AddChild(const char* dir_path, const char* child_path, bool is_dir)
        {
            const uint64_t      key = VFSPath::Parse(dir_path).Value().Hash();
            Array<VFSDirEntry>& arr = Tree[key];
            if (arr.m_allocator == nullptr) // freshly default-constructed by operator[]
            {
                arr.init(TreeArena, 8);
            }

            VFSDirEntry entry;
            entry.Path             = VFSPath::Parse(child_path).Value();
            entry.IsDirectory      = is_dir;
            entry.Stat.IsDirectory = is_dir;
            arr.push(entry);
        }

        VFSResult<Array<VFSDirEntry>> List(const VFSPath& absolute_dir, ArenaAllocator* out_arena) override
        {
            using ResultT = VFSResult<Array<VFSDirEntry>>;
            ListCalls.fetch_add(1);

            const int current = Concurrent.fetch_add(1) + 1;
            int       prev    = MaxConcurrent.load();
            while (current > prev && !MaxConcurrent.compare_exchange_weak(prev, current))
            {
                // prev refreshed by compare_exchange_weak; retry
            }

            if (ListDelay.count() > 0)
            {
                std::this_thread::sleep_for(ListDelay);
            }

            Array<VFSDirEntry> entries;
            entries.init(out_arena, 8);

            const Array<VFSDirEntry>* listing = Tree.find(absolute_dir.Hash());
            if (listing)
            {
                for (size_t i = 0; i < listing->size(); ++i)
                {
                    entries.push((*listing)[i]);
                }
            }

            Concurrent.fetch_sub(1);
            return ResultT::Ok(std::move(entries));
        }

        // ---- unused stubs ----
        VFSResult<IVFSFile*> Open(const VFSPath&, VFSOpenFlags) override
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::Unsupported);
        }
        VFSResult<VFSFileStat> Stat(const VFSPath&) override
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::Unsupported);
        }
        VFSResult<bool> Exists(const VFSPath&) override
        {
            return VFSResult<bool>::Fail(VFSError::Unsupported);
        }
        VFSResult<void> Mount(IVFSBackend*, const VFSPath&, int) override
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        VFSResult<void> Unmount(const VFSPath&) override
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        VFSResult<void> CreateDir(const VFSPath&) override
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        VFSResult<void> Remove(const VFSPath&) override
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        VFSResult<void> RemoveAll(const VFSPath&) override
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        VFSResult<void> Rename(const VFSPath&, const VFSPath&) override
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        void Close(IVFSFile* const) override {}
        void Shutdown() override {}
    };

    void BuildTree(MockVFSContext& mock, const char* root, int breadth, int depth)
    {
        if (depth <= 0)
        {
            return;
        }
        for (int i = 0; i < breadth; ++i)
        {
            char child[VFS_MAX_PATH];
            std::snprintf(child, sizeof(child), "%s/d%d", root, i);
            mock.AddChild(root, child, true);
            BuildTree(mock, child, breadth, depth - 1);
        }
    }
} // namespace

class VFSScannerTest : public ::testing::Test
{
protected:
    std::atomic<bool> m_completed{false};
    ScanStats         m_stats{};

    MemoryManager     m_cache_mgr;
    MemoryManager     m_scan_mgr;
    ArenaAllocator*   m_scan_arena = nullptr;
    VFSDirectoryCache m_cache;
    MockVFSContext    m_mock;
    VFSScanner        m_scanner;

    void              SetUp() override
    {
        m_cache_mgr.Initialize(ZMega(16), {});
        m_scan_mgr.Initialize(ZMega(64), {});
        m_scan_arena = &m_scan_mgr.MainArena;
        m_cache.Initialize(&m_cache_mgr.MainArena);
        m_scanner.Initialize(m_scan_arena); // slot arenas reuse this arena's page size
        m_mock.Initialize(m_scan_arena);    // mock tree lives here (only written during setup)

        m_scanner.SetOnScanComplete(this, [](void* ctx, ScanStats stats) {
            auto* self    = static_cast<VFSScannerTest*>(ctx);
            self->m_stats = stats;
            self->m_completed.store(true);
        });
    }

    VFSPath P(const char* raw)
    {
        return VFSPath::Parse(raw).Value();
    }

    bool WaitForCompletion(int timeout_ms)
    {
        const auto start = std::chrono::steady_clock::now();
        while (!m_completed.load())
        {
            if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeout_ms))
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    bool WaitUntilNotScanning(int timeout_ms)
    {
        const auto start = std::chrono::steady_clock::now();
        while (m_scanner.IsScanning())
        {
            if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeout_ms))
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
};

TEST_F(VFSScannerTest, Scan_EmptyRoot_FiresComplete)
{
    m_scanner.Scan(&m_mock, P("/"), &m_cache);

    ASSERT_TRUE(WaitForCompletion(2000));
    EXPECT_EQ(m_stats.FilesFound, 0u);
    EXPECT_EQ(m_stats.DirsFound, 0u);
}

TEST_F(VFSScannerTest, Scan_TwoLevels_PopulatesCache)
{
    m_mock.AddChild("/root", "/root/sub", true);
    m_mock.AddChild("/root/sub", "/root/sub/a.txt", false);
    m_mock.AddChild("/root/sub", "/root/sub/b.png", false);

    m_scanner.Scan(&m_mock, P("/root"), &m_cache);
    ASSERT_TRUE(WaitForCompletion(2000));

    EXPECT_EQ(m_stats.FilesFound, 2u);
    EXPECT_EQ(m_stats.DirsFound, 1u);
    EXPECT_EQ(m_cache.GetListing(P("/root")).size(), 1u);
    EXPECT_EQ(m_cache.GetListing(P("/root/sub")).size(), 2u);
}

TEST_F(VFSScannerTest, Cancel_StopsWalk_NoCallback)
{
    m_mock.ListDelay = std::chrono::milliseconds(3);
    BuildTree(m_mock, "/root", 6, 3); // 259 List calls with delay → still running at 10ms

    m_scanner.Scan(&m_mock, P("/root"), &m_cache);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    m_scanner.Cancel();

    ASSERT_TRUE(WaitUntilNotScanning(2000));
    EXPECT_FALSE(m_scanner.IsScanning());
    EXPECT_FALSE(m_completed.load()); // completion callback must NOT fire on cancel
}

TEST_F(VFSScannerTest, ConcurrentDirLists_LimitedTo4)
{
    m_mock.ListDelay = std::chrono::milliseconds(5);
    BuildTree(m_mock, "/root", 20, 1); // root + 20 subdirs, all listed with a delay

    m_scanner.Scan(&m_mock, P("/root"), &m_cache);
    ASSERT_TRUE(WaitForCompletion(5000));

    EXPECT_LE(m_mock.MaxConcurrent.load(), VFSScanner::MaxConcurrentDirLists);
}

TEST_F(VFSScannerTest, Rescan_After_Cancel_StartsFresh)
{
    m_mock.ListDelay = std::chrono::milliseconds(3);
    BuildTree(m_mock, "/root", 6, 3);

    m_scanner.Scan(&m_mock, P("/root"), &m_cache);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    m_scanner.Cancel();
    ASSERT_TRUE(WaitUntilNotScanning(2000));

    m_completed.store(false);

    MockVFSContext fresh;
    fresh.Initialize(m_scan_arena);
    fresh.AddChild("/proj", "/proj/sub", true);
    fresh.AddChild("/proj", "/proj/root.txt", false);
    fresh.AddChild("/proj/sub", "/proj/sub/a.txt", false);

    m_scanner.Scan(&fresh, P("/proj"), &m_cache);
    ASSERT_TRUE(WaitForCompletion(2000));

    EXPECT_EQ(m_stats.FilesFound, 2u); // /proj/root.txt + /proj/sub/a.txt
    EXPECT_EQ(m_stats.DirsFound, 1u);  // /proj/sub
}

// --- #755 regression: scanning a self-describing asset with no .meta yet
// must register it under its own embedded UUID, not an unrelated random one.

namespace
{
    uuids::uuid MakeUUID()
    {
        std::random_device           rd;
        std::mt19937                 gen(rd());
        uuids::uuid_random_generator ugen{gen};
        return ugen();
    }

    // Matches AssetCodec::AssetMeshFileHeader's on-disk layout: magic, version,
    // uuid written as three separate sequential writes, tightly packed.
    Array<uint8_t> MakeZemeshBytes(ArenaAllocator* arena, const uuids::uuid& id)
    {
        Array<uint8_t> bytes;
        bytes.init(arena, 24, 24);
        uint32_t magic   = ZEMESH_MAGIC;
        uint32_t version = ASSET_FILE_VERSION;
        std::memcpy(bytes.data(), &magic, sizeof(magic));
        std::memcpy(bytes.data() + sizeof(magic), &version, sizeof(version));
        std::memcpy(bytes.data() + sizeof(magic) + sizeof(version), &id, sizeof(id));
        return bytes;
    }
} // namespace

class VFSScannerMetaUUIDTest : public ::testing::Test
{
protected:
    std::atomic<bool> m_completed{false};

    MemoryManager     m_manager;
    ArenaAllocator*   m_arena = nullptr;
    VFSDirectoryCache m_cache;
    VFSMemoryBackend  m_backend;
    VFSContext        m_ctx;
    AssetRegistry     m_registry;
    VFSScanner        m_scanner;

    void              SetUp() override
    {
        m_manager.Initialize(ZMega(16), {});
        m_arena = &m_manager.MainArena;
        m_cache.Initialize(m_arena);
        m_backend.Initialize(m_arena);
        m_ctx.Initialize(m_arena, 8);
        ASSERT_TRUE(m_ctx.Mount(&m_backend, VFSPath::Root(), 0).Succeeded());
        m_registry.Initialize(m_arena);
        m_scanner.Initialize(m_arena);
        m_scanner.SetAssetRegistry(&m_registry);
        m_scanner.SetOnScanComplete(this, [](void* ctx, ScanStats) { static_cast<VFSScannerMetaUUIDTest*>(ctx)->m_completed.store(true); });
    }

    VFSPath P(const char* raw)
    {
        return VFSPath::Parse(raw).Value();
    }

    bool WaitForCompletion(int timeout_ms)
    {
        const auto start = std::chrono::steady_clock::now();
        while (!m_completed.load())
        {
            if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeout_ms))
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
};

TEST_F(VFSScannerMetaUUIDTest, ZemeshWithNoMeta_RegistersUnderEmbeddedUUID)
{
    uuids::uuid    embedded = MakeUUID();
    Array<uint8_t> bytes    = MakeZemeshBytes(m_arena, embedded);
    ASSERT_TRUE(m_backend.WriteFile(P("/mesh.zemesh"), ArrayView<const uint8_t>(bytes.data(), bytes.size())).Succeeded());

    m_scanner.Scan(&m_ctx, VFSPath::Root(), &m_cache);
    ASSERT_TRUE(WaitForCompletion(2000));

    const AssetRecord* rec = m_registry.FindByUUID(embedded);
    ASSERT_NE(rec, nullptr) << "registry should have the mesh's own embedded UUID, not a freshly-minted random one";
    EXPECT_EQ(rec->Type, Managers::AssetType::MESH);
}

TEST_F(VFSScannerMetaUUIDTest, ZematerialWithNoMeta_RegistersUnderEmbeddedUUID)
{
    uuids::uuid embedded = MakeUUID();
    std::string json     = std::string("{\"uuid\":\"") + uuids::to_string(embedded) + "\"}";
    ASSERT_TRUE(m_backend.WriteFile(P("/mat.zematerial"), ArrayView<const uint8_t>(reinterpret_cast<const uint8_t*>(json.data()), json.size())).Succeeded());

    m_scanner.Scan(&m_ctx, VFSPath::Root(), &m_cache);
    ASSERT_TRUE(WaitForCompletion(2000));

    const AssetRecord* rec = m_registry.FindByUUID(embedded);
    ASSERT_NE(rec, nullptr) << "registry should have the material's own embedded UUID, not a freshly-minted random one";
    EXPECT_EQ(rec->Type, Managers::AssetType::MATERIAL);
}

TEST_F(VFSScannerMetaUUIDTest, ExistingMeta_IsNeverReplaced)
{
    // GetOrCreate's "never replace an existing UUID" policy must survive the
    // new pre-seed step untouched.
    uuids::uuid    embedded     = MakeUUID();
    uuids::uuid    pre_existing = MakeUUID();
    Array<uint8_t> bytes        = MakeZemeshBytes(m_arena, embedded);
    ASSERT_TRUE(m_backend.WriteFile(P("/mesh.zemesh"), ArrayView<const uint8_t>(bytes.data(), bytes.size())).Succeeded());

    MetaFileData seed = {};
    seed.AssetUUID    = pre_existing;
    ASSERT_TRUE(MetaFileIO::Write(m_ctx, P("/mesh.zemesh"), seed).Succeeded());

    m_scanner.Scan(&m_ctx, VFSPath::Root(), &m_cache);
    ASSERT_TRUE(WaitForCompletion(2000));

    EXPECT_EQ(m_registry.FindByUUID(embedded), nullptr) << "must not have registered under the embedded UUID once a .meta already existed";
    const AssetRecord* rec = m_registry.FindByUUID(pre_existing);
    ASSERT_NE(rec, nullptr) << "must keep the pre-existing .meta's UUID";
}
