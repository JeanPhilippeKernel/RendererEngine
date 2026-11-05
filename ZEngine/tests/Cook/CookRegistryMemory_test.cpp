#include <ZEngine/Cook/CookAdapter.h>
#include <ZEngine/Cook/CookRegistryMemory.h>
#include <ZEngine/Cook/SceneCooker.h>
#include <ZEngine/ECS/SceneCompatibility.h>
#include <gtest/gtest.h>
#include <new>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    class CookRegistryMemoryTest : public testing::Test
    {
    protected:
        Core::Memory::ArenaAllocator      Arena    = {};
        ECS::SceneComponentSchemaRegistry Schemas  = {};
        CookAdapterRegistry               Adapters = {};
        SceneCookContext                  Context  = {};

        void                              SetUp() override
        {
            Arena.Initialize(ZMega(4), 0, "RegistryMemoryTest");
            ASSERT_NE(Arena.m_memory, nullptr);
            Schemas.Initialize(&Arena);
            const CookAdapterFns adapters[] = {CookEnvironmentAdapter(), CookMaterialAdapter(), CookMeshAdapter(), CookTextureAdapter()};
            ASSERT_TRUE(Adapters.Initialize(adapters).Succeeded());
            Context.Schemas  = &Schemas;
            Context.Adapters = &Adapters;
        }

        template <typename T>
        T* Store(Core::Memory::ArenaAllocator& arena)
        {
            void* memory = arena.Allocate(sizeof(T), alignof(T));
            EXPECT_NE(memory, nullptr);
            return memory ? new (memory) T{} : nullptr;
        }
    };

    TEST_F(CookRegistryMemoryTest, NativeRegistryCountsCommittedPagesTrackerAndFixedOwnersButNotVirtualReservation)
    {
        Core::Memory::ArenaRetainedStorage backing = {};
        ASSERT_TRUE(Arena.RetainedStorage(&backing));
        const auto profile = QueryCookRegistryMemory(Context);
        ASSERT_TRUE(profile.Succeeded());
        EXPECT_EQ(profile.Value().StateBytes, sizeof(Schemas) + sizeof(Adapters) + sizeof(Arena));
        EXPECT_EQ(profile.Value().StorageBytes, backing.CommittedBytes + backing.TrackingBytes);
        EXPECT_EQ(profile.Value().ReservedAddressBytes, backing.ReservedAddressBytes);
        EXPECT_LT(profile.Value().StorageBytes, profile.Value().ReservedAddressBytes);
        const auto offset = Arena.m_current_offset;
        const auto pages  = Arena.m_committed_size;
        for (size_t repeat = 0; repeat < 128; ++repeat)
        {
            const auto repeated = QueryCookRegistryMemory(Context);
            ASSERT_TRUE(repeated.Succeeded());
            EXPECT_EQ(repeated.Value().StateBytes, profile.Value().StateBytes);
            EXPECT_EQ(repeated.Value().StorageBytes, profile.Value().StorageBytes);
        }
        EXPECT_EQ(Arena.m_current_offset, offset);
        EXPECT_EQ(Arena.m_committed_size, pages);
    }

    TEST_F(CookRegistryMemoryTest, SchemaAndMigrationRegistriesSharingOneArenaDoNotChargeBackingTwice)
    {
        ECS::SceneMigrationRegistry migrations = {};
        ASSERT_TRUE(migrations.Initialize(&Arena));
        Context.Migrations = &migrations;
        const auto profile = QueryCookRegistryMemory(Context);
        ASSERT_TRUE(profile.Succeeded());
        EXPECT_EQ(profile.Value().StateBytes, sizeof(Schemas) + sizeof(Adapters) + sizeof(migrations) + sizeof(Arena));
        EXPECT_EQ(profile.Value().StorageBytes, Arena.m_committed_size + Arena.AllocationTrackingBytes());
        EXPECT_EQ(profile.Value().ReservedAddressBytes, Arena.m_reserved_size);
    }

    TEST_F(CookRegistryMemoryTest, DifferentSubArenaViewsDeduplicateTheirRootPagesAndTracker)
    {
        Core::Memory::ArenaAllocator root = {}, schema_arena = {}, migration_arena = {};
        root.Initialize(ZMega(4), 0);
        root.CreateSubArena(ZKilo(64), &schema_arena);
        root.CreateSubArena(ZKilo(64), &migration_arena);
        ECS::SceneComponentSchemaRegistry schemas    = {};
        ECS::SceneMigrationRegistry       migrations = {};
        schemas.Initialize(&schema_arena);
        ASSERT_TRUE(migrations.Initialize(&migration_arena));
        Context.Schemas    = &schemas;
        Context.Migrations = &migrations;
        const auto profile = QueryCookRegistryMemory(Context);
        ASSERT_TRUE(profile.Succeeded());
        EXPECT_EQ(root.m_committed_size, 0u);
        EXPECT_EQ(profile.Value().StorageBytes, schema_arena.m_committed_size + migration_arena.m_committed_size + root.AllocationTrackingBytes());
        EXPECT_EQ(profile.Value().StateBytes, sizeof(schemas) + sizeof(Adapters) + sizeof(migrations) + 3 * sizeof(root));
        EXPECT_EQ(profile.Value().ReservedAddressBytes, root.m_reserved_size);
    }

    TEST_F(CookRegistryMemoryTest, IndependentRegistryArenasIncludeBothBackingDomains)
    {
        Core::Memory::ArenaAllocator separate = {};
        separate.Initialize(ZMega(2), 0);
        ECS::SceneMigrationRegistry migrations = {};
        ASSERT_TRUE(migrations.Initialize(&separate));
        Context.Migrations = &migrations;
        const auto profile = QueryCookRegistryMemory(Context);
        ASSERT_TRUE(profile.Succeeded());
        EXPECT_EQ(profile.Value().StateBytes, sizeof(Schemas) + sizeof(Adapters) + sizeof(migrations) + sizeof(Arena) + sizeof(separate));
        EXPECT_EQ(profile.Value().StorageBytes, Arena.m_committed_size + Arena.AllocationTrackingBytes() + separate.m_committed_size + separate.AllocationTrackingBytes());
        EXPECT_EQ(profile.Value().ReservedAddressBytes, Arena.m_reserved_size + separate.m_reserved_size);
    }

    TEST_F(CookRegistryMemoryTest, RootAndChildRegistryViewsShareBackingRegardlessOfWhichRegistryUsesTheRoot)
    {
        for (const bool schema_uses_root : {false, true})
        {
            SCOPED_TRACE(schema_uses_root);
            Core::Memory::ArenaAllocator root = {}, child = {};
            root.Initialize(ZMega(4), 0);
            root.CreateSubArena(ZKilo(64), &child);
            ECS::SceneComponentSchemaRegistry schemas    = {};
            ECS::SceneMigrationRegistry       migrations = {};
            schemas.Initialize(schema_uses_root ? &root : &child);
            ASSERT_TRUE(migrations.Initialize(schema_uses_root ? &child : &root));
            Context.Schemas                            = &schemas;
            Context.Migrations                         = &migrations;
            Core::Memory::ArenaRetainedStorage backing = {};
            ASSERT_TRUE(root.RetainedStorage(&backing));
            const auto profile = QueryCookRegistryMemory(Context);
            ASSERT_TRUE(profile.Succeeded());
            EXPECT_EQ(profile.Value().StateBytes, sizeof(schemas) + sizeof(Adapters) + sizeof(migrations) + sizeof(root) + sizeof(child));
            EXPECT_EQ(profile.Value().StorageBytes, backing.CommittedBytes + backing.TrackingBytes);
            EXPECT_EQ(profile.Value().ReservedAddressBytes, backing.ReservedAddressBytes);
        }
    }

    TEST_F(CookRegistryMemoryTest, ArenaResidentRegistryObjectsAndSubArenaViewsDoNotAddInlineCharges)
    {
        auto* child      = Store<Core::Memory::ArenaAllocator>(Arena);
        auto* schemas    = Store<ECS::SceneComponentSchemaRegistry>(Arena);
        auto* migrations = Store<ECS::SceneMigrationRegistry>(Arena);
        auto* adapters   = Store<CookAdapterRegistry>(Arena);
        ASSERT_NE(child, nullptr);
        ASSERT_NE(schemas, nullptr);
        ASSERT_NE(migrations, nullptr);
        ASSERT_NE(adapters, nullptr);
        Arena.CreateSubArena(ZKilo(64), child);
        schemas->Initialize(child);
        ASSERT_TRUE(migrations->Initialize(child));
        const CookAdapterFns entries[] = {CookEnvironmentAdapter(), CookMaterialAdapter(), CookMeshAdapter(), CookTextureAdapter()};
        ASSERT_TRUE(adapters->Initialize(entries).Succeeded());
        Context.Schemas                            = schemas;
        Context.Migrations                         = migrations;
        Context.Adapters                           = adapters;
        Core::Memory::ArenaRetainedStorage backing = {};
        ASSERT_TRUE(Arena.RetainedStorage(&backing));
        const auto offset = Arena.m_current_offset;
        for (size_t repeat = 0; repeat < 128; ++repeat)
        {
            const auto profile = QueryCookRegistryMemory(Context);
            ASSERT_TRUE(profile.Succeeded());
            EXPECT_EQ(profile.Value().StateBytes, sizeof(Arena));
            EXPECT_EQ(profile.Value().StorageBytes, backing.CommittedBytes + backing.TrackingBytes);
            EXPECT_EQ(profile.Value().ReservedAddressBytes, backing.ReservedAddressBytes);
        }
        EXPECT_EQ(Arena.m_current_offset, offset);
        adapters->~CookAdapterRegistry();
        migrations->~SceneMigrationRegistry();
        schemas->~SceneComponentSchemaRegistry();
        child->~ArenaAllocator();
    }

    TEST_F(CookRegistryMemoryTest, IndependentRootOwnerInsideTheOtherRegistryFamilyIsAlreadyCovered)
    {
        auto* separate = Store<Core::Memory::ArenaAllocator>(Arena);
        ASSERT_NE(separate, nullptr);
        separate->Initialize(ZMega(2), 0);
        {
            ECS::SceneMigrationRegistry migrations = {};
            ASSERT_TRUE(migrations.Initialize(separate));
            Context.Migrations = &migrations;
            const auto profile = QueryCookRegistryMemory(Context);
            ASSERT_TRUE(profile.Succeeded());
            EXPECT_EQ(profile.Value().StateBytes, sizeof(Schemas) + sizeof(Adapters) + sizeof(migrations) + sizeof(Arena));
            Core::Memory::ArenaRetainedStorage first = {}, second = {};
            ASSERT_TRUE(Arena.RetainedStorage(&first));
            ASSERT_TRUE(separate->RetainedStorage(&second));
            EXPECT_EQ(second.Owner, separate);
            EXPECT_EQ(profile.Value().StorageBytes, first.CommittedBytes + first.TrackingBytes + second.CommittedBytes + second.TrackingBytes);
            EXPECT_EQ(profile.Value().ReservedAddressBytes, first.ReservedAddressBytes + second.ReservedAddressBytes);
        }
        separate->~ArenaAllocator();
    }

    TEST_F(CookRegistryMemoryTest, InvalidServicesAndUnknownBorrowedBackingFailBeforeAnyArenaMutation)
    {
        EXPECT_EQ(QueryCookRegistryMemory({}).Error(), VFS::VFSError::InvalidPath);
        ECS::SceneMigrationRegistry migrations = {};
        Context.Migrations                     = &migrations;
        EXPECT_EQ(QueryCookRegistryMemory(Context).Error(), VFS::VFSError::InvalidPath);
        alignas(DEFAULT_ALIGNMENT) Core::Containers::FixedArray<uint8_t, ZKilo(64)> storage  = {};
        Core::Memory::ArenaAllocator                                                borrowed = {};
        ASSERT_TRUE(borrowed.InitializeBorrowed(storage.data(), storage.size()));
        ECS::SceneComponentSchemaRegistry schemas = {};
        schemas.Initialize(&borrowed);
        Context.Migrations = nullptr;
        Context.Schemas    = &schemas;
        const auto offset  = borrowed.m_current_offset;
        EXPECT_EQ(QueryCookRegistryMemory(Context).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(borrowed.m_current_offset, offset);
        Context.Schemas = &Schemas;
        ASSERT_TRUE(migrations.Initialize(&borrowed));
        Context.Migrations = &migrations;
        EXPECT_EQ(QueryCookRegistryMemory(Context).Error(), VFS::VFSError::Unsupported);
    }
} // namespace ZEngine::Cook
