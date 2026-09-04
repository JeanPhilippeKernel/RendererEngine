#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/RenderHandle.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <gtest/gtest.h>
#include <uuid.h>
#include <algorithm>
#include <random>
#include <vector>

using namespace ZEngine;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::Rendering;
using namespace ZEngine::Managers;

static uuids::uuid MakeUUID()
{
    std::random_device           rd;
    std::mt19937                 gen(rd());
    uuids::uuid_random_generator g{gen};
    return g();
}

// ============================================================
// RenderHandle tests — no GPU, no device
// ============================================================

TEST(RenderHandle, DefaultConstructedIsInvalid)
{
    BufferHandle h;
    EXPECT_FALSE(h.IsValid());
    EXPECT_EQ(h.Generation, 0u);
}

TEST(RenderHandle, NonZeroGenerationIsValid)
{
    BufferHandle h{0, 1};
    EXPECT_TRUE(h.IsValid());
}

TEST(RenderHandle, EqualityRequiresBothFieldsToMatch)
{
    BufferHandle a{3, 7};
    BufferHandle b{3, 7};
    BufferHandle c{3, 8}; // same index, different generation
    BufferHandle d{4, 7}; // different index, same generation

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
}

TEST(RenderHandle, DifferentTagTypesAreIncompatible)
{
    // Compile-time check: BufferHandle and SamplerHandle are distinct types.
    static_assert(!std::is_same_v<BufferHandle, SamplerHandle>, "BufferHandle and SamplerHandle must be distinct types");
    static_assert(!std::is_same_v<SamplerHandle, PipelineHandle>, "SamplerHandle and PipelineHandle must be distinct types");

    // Runtime sanity: same {index, generation} in different handle types are not interchangeable.
    BufferHandle  buf{1, 2};
    SamplerHandle smp{1, 2};
    EXPECT_EQ(buf.Index, smp.Index);
    EXPECT_EQ(buf.Generation, smp.Generation);
    // They cannot be compared (different types) — confirmed by static_assert above.
    SUCCEED();
}

TEST(RenderHandle, TriviallyCopiable)
{
    static_assert(std::is_trivially_copyable_v<BufferHandle>, "BufferHandle must be trivially copyable");
    static_assert(std::is_trivially_copyable_v<SamplerHandle>, "SamplerHandle must be trivially copyable");
    static_assert(std::is_trivially_copyable_v<PipelineHandle>, "PipelineHandle must be trivially copyable");
    SUCCEED();
}

TEST(RenderHandle, SizeFitsInEightBytes)
{
    static_assert(sizeof(BufferHandle) == 8, "BufferHandle must fit in 8 bytes");
    static_assert(sizeof(SamplerHandle) == 8, "SamplerHandle must fit in 8 bytes");
    static_assert(sizeof(PipelineHandle) == 8, "PipelineHandle must fit in 8 bytes");
    SUCCEED();
}

// ============================================================
// AssetRegistry OnReady / OnStale callback wiring
// ============================================================

class AssetRegistryCallbackTest : public ::testing::Test
{
protected:
    MemoryManager m_manager;
    AssetRegistry m_registry;

    void          SetUp() override
    {
        // AssetIndex pre-allocates 8192 AssetRecord slots (~800 bytes each = ~6.5 MB)
        // plus UUID/path hashmaps. 32 MB gives comfortable headroom.
        m_manager.Initialize(ZMega(32), {});
        m_registry.Initialize(&m_manager.MainArena);
    }

    void TearDown() override
    {
        m_manager.Shutdown();
    }
};

TEST_F(AssetRegistryCallbackTest, OnReadyFiredWhenStateTransitionsToLoaded)
{
    uuids::uuid fired_uuid;
    AssetHandle fired_handle = 0;
    int         call_count   = 0;

    m_registry.SetOnReadyCallback(nullptr, [](void*, const uuids::uuid& uuid, AssetHandle handle) {
        // Can't capture, use global for test — stash via out-params via ctx trick below
        (void) uuid;
        (void) handle;
    });

    // Use ctx pointer to capture results
    struct Ctx
    {
        uuids::uuid uuid;
        AssetHandle handle;
        int         count;
    };
    Ctx ctx{};
    m_registry.SetOnReadyCallback(&ctx, [](void* raw, const uuids::uuid& uuid, AssetHandle handle) {
        auto* c   = static_cast<Ctx*>(raw);
        c->uuid   = uuid;
        c->handle = handle;
        c->count++;
    });

    // Register an asset
    uuids::uuid             test_uuid = MakeUUID();
    Core::VFS::MetaFileData meta{};
    meta.AssetUUID          = test_uuid;

    // Register as Loaded (simulates IngestMesh path)
    AssetHandle slot_handle = 0xDEAD;
    m_registry.RegisterLoaded(test_uuid, AssetType::MESH, {}, meta, slot_handle);

    // RegisterLoaded sets state directly — callback fires only through SetState.
    // Call SetState explicitly as IngestMesh does.
    m_registry.SetState(test_uuid, AssetState::Loaded);

    EXPECT_EQ(ctx.count, 1);
    EXPECT_EQ(ctx.uuid, test_uuid);
    EXPECT_EQ(ctx.handle, slot_handle);
}

TEST_F(AssetRegistryCallbackTest, OnReadyNotFiredForNonLoadedTransitions)
{
    struct Ctx
    {
        int count = 0;
    };
    Ctx ctx{};

    m_registry.SetOnReadyCallback(&ctx, [](void* raw, const uuids::uuid&, AssetHandle) { static_cast<Ctx*>(raw)->count++; });

    uuids::uuid             test_uuid = MakeUUID();
    Core::VFS::MetaFileData meta{};
    meta.AssetUUID = test_uuid;
    m_registry.Register(test_uuid, AssetType::MESH, {}, meta);

    m_registry.SetState(test_uuid, AssetState::Importing);
    m_registry.SetState(test_uuid, AssetState::Failed);

    EXPECT_EQ(ctx.count, 0);
}

TEST_F(AssetRegistryCallbackTest, OnReadyFiresIndependentlyOfHotReloadCallback)
{
    struct Ctx
    {
        int ready_count  = 0;
        int reload_count = 0;
    };
    Ctx ctx{};

    m_registry.SetOnReadyCallback(&ctx, [](void* raw, const uuids::uuid&, AssetHandle) { static_cast<Ctx*>(raw)->ready_count++; });
    m_registry.SetHotReloadCallback(&ctx, [](void* raw, std::span<const uuids::uuid>) { static_cast<Ctx*>(raw)->reload_count++; });

    uuids::uuid             uuid = MakeUUID();
    Core::VFS::MetaFileData meta{};
    meta.AssetUUID = uuid;
    m_registry.RegisterLoaded(uuid, AssetType::MESH, {}, meta, 1u);
    m_registry.SetState(uuid, AssetState::Loaded);

    EXPECT_EQ(ctx.ready_count, 1) << "OnReady must fire once";
    EXPECT_EQ(ctx.reload_count, 1) << "HotReload callback must also fire";
}

TEST_F(AssetRegistryCallbackTest, OnStaleFiredByOnAssetModified)
{
    struct Ctx
    {
        uuids::uuid uuid;
        int         count = 0;
    };
    Ctx ctx{};

    m_registry.SetOnStaleCallback(&ctx, [](void* raw, const uuids::uuid& uuid) {
        auto* c = static_cast<Ctx*>(raw);
        c->uuid = uuid;
        c->count++;
    });

    // Register with a real VFS path so OnAssetModified can find it
    uuids::uuid             uuid = MakeUUID();
    Core::VFS::MetaFileData meta{};
    meta.AssetUUID   = uuid;

    auto path_result = Core::VFS::VFSPath::Parse("/Assets/test.glb");
    ASSERT_TRUE(path_result.Succeeded());

    m_registry.Register(uuid, AssetType::MESH, path_result.Value(), meta);
    m_registry.OnAssetModified(path_result.Value());

    EXPECT_EQ(ctx.count, 1);
    EXPECT_EQ(ctx.uuid, uuid);
}

TEST_F(AssetRegistryCallbackTest, OnStaleNotFiredWhenNoCallback)
{
    // Verify no crash when OnStale callback is null and OnAssetModified fires.
    uuids::uuid             uuid = MakeUUID();
    Core::VFS::MetaFileData meta{};
    meta.AssetUUID   = uuid;

    auto path_result = Core::VFS::VFSPath::Parse("/Assets/nocallback.glb");
    ASSERT_TRUE(path_result.Succeeded());
    m_registry.Register(uuid, AssetType::MESH, path_result.Value(), meta);

    // No SetOnStaleCallback call — must not crash
    EXPECT_NO_FATAL_FAILURE(m_registry.OnAssetModified(path_result.Value()));
}

TEST_F(AssetRegistryCallbackTest, OnStaleFiredForEachUUIDInCascade)
{
    struct Ctx
    {
        std::vector<uuids::uuid> uuids;
    };
    Ctx ctx{};

    m_registry.SetOnStaleCallback(&ctx, [](void* raw, const uuids::uuid& uuid) { static_cast<Ctx*>(raw)->uuids.push_back(uuid); });

    // Register a mesh and a material that depends on it
    uuids::uuid             mesh_uuid = MakeUUID();
    uuids::uuid             mat_uuid  = MakeUUID();

    Core::VFS::MetaFileData mesh_meta{};
    mesh_meta.AssetUUID = mesh_uuid;
    Core::VFS::MetaFileData mat_meta{};
    mat_meta.AssetUUID = mat_uuid;

    auto mesh_path     = Core::VFS::VFSPath::Parse("/Assets/mesh.glb");
    ASSERT_TRUE(mesh_path.Succeeded());
    auto mat_path = Core::VFS::VFSPath::Parse("/Assets/mat.zematerial");
    ASSERT_TRUE(mat_path.Succeeded());

    m_registry.Register(mesh_uuid, AssetType::MESH, mesh_path.Value(), mesh_meta);
    m_registry.Register(mat_uuid, AssetType::MATERIAL, mat_path.Value(), mat_meta);
    m_registry.AddDependency(mat_uuid, mesh_uuid);

    m_registry.OnAssetModified(mesh_path.Value());

    // Both mesh and its dependent material should be stale
    EXPECT_EQ(ctx.uuids.size(), 2u);
    EXPECT_NE(std::find(ctx.uuids.begin(), ctx.uuids.end(), mesh_uuid), ctx.uuids.end());
    EXPECT_NE(std::find(ctx.uuids.begin(), ctx.uuids.end(), mat_uuid), ctx.uuids.end());
}
