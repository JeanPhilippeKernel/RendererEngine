#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>

using namespace ZEngine;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::Core::Memory;

namespace
{
    uuids::uuid MakeUUID()
    {
        std::random_device           rd;
        std::mt19937                 gen(rd());
        uuids::uuid_random_generator ugen{gen};
        return ugen();
    }

    Core::VFS::VFSPath P(const char* s)
    {
        return Core::VFS::VFSPath::Parse(s).Value();
    }

    void Reg(AssetRegistry& r, uuids::uuid id, Managers::AssetType t, const char* path)
    {
        Core::VFS::MetaFileData meta{};
        meta.AssetUUID = id;
        r.Register(id, t, P(path), meta);
    }
} // namespace

class AssetRegistryTest : public ::testing::Test
{
protected:
    MemoryManager m_manager;
    AssetRegistry m_registry;

    void          SetUp() override
    {
        m_manager.Initialize(ZMega(64), {});
        m_registry.Initialize(&m_manager.MainArena);
    }

    void TearDown() override
    {
        m_manager.Shutdown();
    }
};

// Test 1 — UUID lookup returns correct record
TEST_F(AssetRegistryTest, UUIDLookupReturnsCorrectRecord)
{
    uuids::uuid uuid = MakeUUID();
    Reg(m_registry, uuid, Managers::AssetType::MESH, "/project/mesh.glb");

    const AssetRecord* rec = m_registry.FindByUUID(uuid);
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(rec->UUID, uuid);
    EXPECT_EQ(rec->Type, Managers::AssetType::MESH);
    EXPECT_STREQ(rec->Path.CStr(), "/project/mesh.glb");
}

// Test 2 — VFSPath lookup returns correct record
TEST_F(AssetRegistryTest, PathLookupReturnsCorrectRecord)
{
    uuids::uuid             uuid = MakeUUID();
    Core::VFS::VFSPath      path = P("/project/textures/diffuse.png");
    Core::VFS::MetaFileData meta{};
    meta.AssetUUID = uuid;
    m_registry.Register(uuid, Managers::AssetType::TEXTURE, path, meta);

    const AssetRecord* rec = m_registry.FindByPath(path);
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(rec->UUID, uuid);
    EXPECT_EQ(rec->Type, Managers::AssetType::TEXTURE);
}

// Test 3 — Type query returns only matching records
TEST_F(AssetRegistryTest, TypeQueryReturnsOnlyMatchingType)
{
    for (int i = 0; i < 2; ++i)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "/p/mesh%d.glb", i);
        Reg(m_registry, MakeUUID(), Managers::AssetType::MESH, buf);
    }
    Reg(m_registry, MakeUUID(), Managers::AssetType::TEXTURE, "/p/tex.png");

    AssetRegistry::QueryResult meshes = m_registry.Query({.Type = Managers::AssetType::MESH}, &m_manager.MainArena);
    EXPECT_EQ(meshes.Count, 2u);

    AssetRegistry::QueryResult textures = m_registry.Query({.Type = Managers::AssetType::TEXTURE}, &m_manager.MainArena);
    EXPECT_EQ(textures.Count, 1u);
}

// Test 4 — Dependency registration triggers hot-reload cascade
TEST_F(AssetRegistryTest, DependencyRegistrationTriggersHotReload)
{
    uuids::uuid mesh = MakeUUID(), mat = MakeUUID(), tex = MakeUUID();
    Reg(m_registry, mesh, Managers::AssetType::MESH, "/p/mesh.glb");
    Reg(m_registry, mat, Managers::AssetType::MATERIAL, "/p/mat.glb");
    Reg(m_registry, tex, Managers::AssetType::TEXTURE, "/p/tex.png");

    // mesh → mat → tex
    ASSERT_TRUE(m_registry.AddDependency(mesh, mat));
    ASSERT_TRUE(m_registry.AddDependency(mat, tex));

    struct Capture
    {
        uuids::uuid data[16];
        uint32_t    count;
    };
    Capture cap{};
    m_registry.SetHotReloadCallback(&cap, [](void* ctx, std::span<const uuids::uuid> c) {
        auto* p  = static_cast<Capture*>(ctx);
        p->count = 0;
        for (auto& u : c)
            if (p->count < 16)
                p->data[p->count++] = u;
    });

    m_registry.OnAssetModified(P("/p/tex.png"));

    ASSERT_EQ(cap.count, 3u);
    EXPECT_EQ(cap.data[0], tex); // root first
    bool found_mat = false, found_mesh = false;
    for (uint32_t i = 1; i < cap.count; ++i)
    {
        if (cap.data[i] == mat)
            found_mat = true;
        if (cap.data[i] == mesh)
            found_mesh = true;
    }
    EXPECT_TRUE(found_mat);
    EXPECT_TRUE(found_mesh);
}

// Test 5 — Diamond dependency produces each node exactly once
TEST_F(AssetRegistryTest, CascadeDiamondNoDuplicates)
{
    uuids::uuid tex  = MakeUUID();
    uuids::uuid matA = MakeUUID();
    uuids::uuid matB = MakeUUID();
    uuids::uuid mesh = MakeUUID();

    Reg(m_registry, tex, Managers::AssetType::TEXTURE, "/p/tex.png");
    Reg(m_registry, matA, Managers::AssetType::MATERIAL, "/p/matA.glb");
    Reg(m_registry, matB, Managers::AssetType::MATERIAL, "/p/matB.glb");
    Reg(m_registry, mesh, Managers::AssetType::MESH, "/p/mesh.glb");

    m_registry.AddDependency(matA, tex);
    m_registry.AddDependency(matB, tex);
    m_registry.AddDependency(mesh, matA);
    m_registry.AddDependency(mesh, matB);

    struct Capture
    {
        uuids::uuid data[16];
        uint32_t    count;
    };
    Capture cap{};
    m_registry.SetHotReloadCallback(&cap, [](void* ctx, std::span<const uuids::uuid> c) {
        auto* p  = static_cast<Capture*>(ctx);
        p->count = 0;
        for (auto& u : c)
            if (p->count < 16)
                p->data[p->count++] = u;
    });

    m_registry.OnAssetModified(P("/p/tex.png"));

    ASSERT_EQ(cap.count, 4u);
    // No duplicates
    for (uint32_t i = 0; i < cap.count; ++i)
        for (uint32_t j = i + 1; j < cap.count; ++j)
            EXPECT_NE(cap.data[i], cap.data[j]);
}

// Test 6 — Duplicate registration returns correct error codes
TEST_F(AssetRegistryTest, DuplicateRegistrationFails)
{
    uuids::uuid uuid = MakeUUID();
    Reg(m_registry, uuid, Managers::AssetType::MESH, "/p/mesh.glb");

    // Same UUID, different path
    Core::VFS::MetaFileData meta{};
    meta.AssetUUID = uuid;
    auto r2        = m_registry.Register(uuid, Managers::AssetType::MESH, P("/p/other.glb"), meta);
    EXPECT_FALSE(r2.IsOk());
    EXPECT_EQ(r2.Error, RegisterError::DuplicateUUID);

    // Same path, different UUID
    uuids::uuid             uuid2 = MakeUUID();
    Core::VFS::MetaFileData meta2{};
    meta2.AssetUUID = uuid2;
    auto r3         = m_registry.Register(uuid2, Managers::AssetType::MESH, P("/p/mesh.glb"), meta2);
    EXPECT_FALSE(r3.IsOk());
    EXPECT_EQ(r3.Error, RegisterError::DuplicatePath);

    EXPECT_EQ(m_registry.RecordCount(), 1u);
}

// Test 7 — Remove purges all indices and dependency edges
TEST_F(AssetRegistryTest, RemovePurgesAllIndices)
{
    uuids::uuid mesh = MakeUUID(), mat = MakeUUID();
    Reg(m_registry, mesh, Managers::AssetType::MESH, "/p/mesh.glb");
    Reg(m_registry, mat, Managers::AssetType::MATERIAL, "/p/mat.glb");
    m_registry.AddDependency(mesh, mat);

    EXPECT_EQ(m_registry.RecordCount(), 2u);
    EXPECT_EQ(m_registry.EdgeCount(), 1u);

    m_registry.Remove(mat);

    EXPECT_EQ(m_registry.RecordCount(), 1u);
    EXPECT_EQ(m_registry.EdgeCount(), 0u);
    EXPECT_EQ(m_registry.FindByUUID(mat), nullptr);
    EXPECT_EQ(m_registry.FindByPath(P("/p/mat.glb")), nullptr);

    auto mats = m_registry.Query({.Type = Managers::AssetType::MATERIAL}, &m_manager.MainArena);
    EXPECT_EQ(mats.Count, 0u);
}

// Test 8 — Query by name substring
TEST_F(AssetRegistryTest, QueryByNameSubstring)
{
    Reg(m_registry, MakeUUID(), Managers::AssetType::MESH, "/p/tank_body.glb");
    Reg(m_registry, MakeUUID(), Managers::AssetType::MESH, "/p/tank_turret.glb");
    Reg(m_registry, MakeUUID(), Managers::AssetType::MESH, "/p/jeep.glb");
    Reg(m_registry, MakeUUID(), Managers::AssetType::TEXTURE, "/p/tank_albedo.png");

    auto all_tank = m_registry.Query({.NameLike = "tank"}, &m_manager.MainArena);
    EXPECT_EQ(all_tank.Count, 3u);

    auto mesh_tank = m_registry.Query({.Type = Managers::AssetType::MESH, .NameLike = "tank"}, &m_manager.MainArena);
    EXPECT_EQ(mesh_tank.Count, 2u);

    auto none = m_registry.Query({.NameLike = "helicopter"}, &m_manager.MainArena);
    EXPECT_EQ(none.Count, 0u);
}

// Test 9 — Query by extension (case-insensitive)
TEST_F(AssetRegistryTest, QueryByExtension)
{
    Reg(m_registry, MakeUUID(), Managers::AssetType::MESH, "/p/a.glb");
    Reg(m_registry, MakeUUID(), Managers::AssetType::MESH, "/p/b.gltf");
    Reg(m_registry, MakeUUID(), Managers::AssetType::TEXTURE, "/p/c.png");
    Reg(m_registry, MakeUUID(), Managers::AssetType::TEXTURE, "/p/d.PNG");

    EXPECT_EQ(m_registry.Query({.Ext = ".glb"}, &m_manager.MainArena).Count, 1u);
    EXPECT_EQ(m_registry.Query({.Ext = ".gltf"}, &m_manager.MainArena).Count, 1u);
    EXPECT_EQ(m_registry.Query({.Ext = ".png"}, &m_manager.MainArena).Count, 2u); // case-insensitive
}

// Test 10 — State transitions propagate; cascade marks all dependents Stale
TEST_F(AssetRegistryTest, StateTransitionAndCascadeMarksStale)
{
    uuids::uuid tex = MakeUUID(), mat = MakeUUID();
    Reg(m_registry, tex, Managers::AssetType::TEXTURE, "/p/tex.png");
    Reg(m_registry, mat, Managers::AssetType::MATERIAL, "/p/mat.glb");
    m_registry.AddDependency(mat, tex);

    EXPECT_EQ(m_registry.FindByUUID(tex)->State, AssetState::Registered);
    EXPECT_EQ(m_registry.FindByUUID(mat)->State, AssetState::Registered);

    m_registry.SetState(tex, AssetState::Loaded);
    m_registry.SetState(mat, AssetState::Loaded);

    int cb_count = 0;
    m_registry.SetHotReloadCallback(&cb_count, [](void* ctx, std::span<const uuids::uuid>) { ++(*static_cast<int*>(ctx)); });

    m_registry.OnAssetModified(P("/p/tex.png"));
    EXPECT_EQ(cb_count, 1);

    EXPECT_EQ(m_registry.FindByUUID(tex)->State, AssetState::Stale);
    EXPECT_EQ(m_registry.FindByUUID(mat)->State, AssetState::Stale);
}

// Test 11 — Registry initialises within the AssetManager budget (100 MB).
// Regression guard: AssetRecord was once ~7 KB (full MetaFileData embedded),
// making 4096 slots cost ~27 MB. Now AssetMetaSnapshot keeps it ~1.2 KB
// per record (~5 MB total). This test fails if AssetRecord grows beyond budget.
TEST(AssetRegistryBudget, InitialisesWithinAssetManagerBudget)
{
    constexpr uint64_t ASSET_MANAGER_BUDGET = ZMega(100);

    MemoryManager      mgr;
    mgr.Initialize(ASSET_MANAGER_BUDGET, {});

    // Carve the same sub-arenas AssetManager::Initialize carves (20 + 78 = 98 MB).
    ArenaAllocator thread_local_arena{};
    ArenaAllocator asset_arena{};
    mgr.MainArena.CreateSubArena(ZMega(20), &thread_local_arena);
    mgr.MainArena.CreateSubArena(ZMega(78), &asset_arena);

    AssetRegistry registry;
    registry.Initialize(&asset_arena);

    EXPECT_EQ(registry.RecordCount(), 0u);

    // Verify headroom: the remaining 2 MB in the parent arena must still be allocatable.
    // If registry had overflowed its 78 MB sub-arena, CreateSubArena would have asserted
    // before reaching this line.
    ArenaAllocator headroom{};
    mgr.MainArena.CreateSubArena(ZMega(2), &headroom);

    mgr.Shutdown();
}

// Test 12 — Overflow: requesting more than the available budget triggers an assertion.
// Uses EXPECT_DEATH because CreateSubArena calls ZENGINE_VALIDATE_ASSERT which calls
// CrashHandler::OnAssertionFailure → __builtin_trap on macOS (terminates the process).
TEST(AssetRegistryBudget, OverflowTriggersAssertion)
{
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";

    // Only 10 MB available — trying to carve 20 MB must trigger the assertion.
    EXPECT_DEATH(
        {
            MemoryManager mgr;
            mgr.Initialize(ZMega(10), {});
            ArenaAllocator a{};
            mgr.MainArena.CreateSubArena(ZMega(20), &a); // asserts: not enough space
            mgr.Shutdown();
        },
        ""); // empty pattern matches any crash output
}
