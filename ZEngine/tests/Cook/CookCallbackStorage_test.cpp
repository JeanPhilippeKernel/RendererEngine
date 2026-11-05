#include <ZEngine/Cook/CookAdapter.h>
#include <ZEngine/Cook/CookCallbackStorage.h>
#include <ZEngine/Cook/CookPageAllocator.h>
#include <ZEngine/Cook/CookRegistryMemory.h>
#include <ZEngine/Cook/CookServiceMemoryInternal.h>
#include <ZEngine/Cook/SceneCooker.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/ECS/SceneCompatibility.h>
#include <gtest/gtest.h>
#include <new>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    class CookCallbackStorageTest : public testing::Test
    {
    protected:
        struct Owner final : ICookCallbackStorage
        {
            const void*                         Root        = nullptr;
            const Core::Memory::ArenaAllocator* NativeArena = nullptr;
            mutable size_t                      Queries     = 0;
            mutable const ICookAllocator*       Covered     = nullptr;
            VFS::VFSError                       Error       = VFS::VFSError::OK;

            bool                                Contains(const void* context) const noexcept override
            {
                return Root && context == Root;
            }
            const Core::Memory::ArenaAllocator* StorageArena() const noexcept override
            {
                return NativeArena;
            }
            VFS::VFSResult<CookServiceMemoryRequirements> MemoryRequirements(const SceneCookContext&, const ICookAllocator& covered) const noexcept override
            {
                ++Queries;
                Covered = &covered;
                return Error == VFS::VFSError::OK ? VFS::VFSResult<CookServiceMemoryRequirements>::Ok({.StateBytes = sizeof(*this) + sizeof(uint32_t)}) : VFS::VFSResult<CookServiceMemoryRequirements>::Fail(Error);
            }
        };

        Core::Memory::ArenaAllocator      Arena     = {};
        ECS::SceneComponentSchemaRegistry Schemas   = {};
        CookAdapterRegistry               Adapters  = {};
        CookPageAllocator                 Allocator = {};
        SceneCookContext                  Context   = {};

        void                              SetUp() override
        {
            Arena.Initialize(ZMega(4), 0);
            ASSERT_NE(Arena.m_memory, nullptr);
            Schemas.Initialize(&Arena);
            ASSERT_TRUE(ECS::RegisterBuiltInSceneComponentSchemas(Schemas));
            SetAdapters(Adapters, nullptr);
            ASSERT_TRUE(Allocator.Initialize(ZMega(16)).Succeeded());
            Context.Schemas  = &Schemas;
            Context.Adapters = &Adapters;
        }

        void SetAdapters(CookAdapterRegistry& registry, const void* context)
        {
            CookAdapterFns entries[] = {CookEnvironmentAdapter(), CookMaterialAdapter(), CookMeshAdapter(), CookTextureAdapter()};
            entries[3].Context       = context;
            ASSERT_TRUE(registry.Initialize(entries).Succeeded());
        }

        void SetSchemas(ECS::SceneComponentSchemaRegistry& registry, void* codec, void* reference)
        {
            registry.Initialize(&Arena);
            for (cstring key : {"NameComponent", "ParentComponent"})
            {
                const auto* original = Schemas.FindByKey(key);
                ASSERT_NE(original, nullptr);
                ECS::SceneComponentSchemaDesc entry = {.Key = original->Key, .Version = original->Version, .RuntimeType = original->RuntimeType, .Fields = original->Fields, .FieldCount = original->FieldCount, .Codecs = original->Codecs, .References = original->References};
                if (key[0] == 'N')
                    entry.Codecs.Context = codec;
                else
                    entry.References.Context = reference;
                ASSERT_TRUE(registry.Register(entry, nullptr));
            }
        }

        void SetMigrations(ECS::SceneMigrationRegistry& registry, void* context)
        {
            ASSERT_TRUE(registry.Initialize(&Arena));
            ASSERT_TRUE(registry.RegisterDocumentMigration(1, 2, context, [](void*, const ECS::SceneYAML::Node&, ECS::SceneYAML::Node*, ECS::SceneDiagnostics*) { return false; }, nullptr));
        }
    };

    TEST_F(CookCallbackStorageTest, AllNullBuiltInsNeedNoStorageOwnerAndNeverAllocate)
    {
        const auto cursor = Arena.m_current_offset;
        for (size_t repeat = 0; repeat < 128; ++repeat)
        {
            const auto bytes = QueryCookCallbackMemory(Context, Allocator);
            ASSERT_TRUE(bytes.Succeeded());
            EXPECT_EQ(bytes.Value().StateBytes, 0u);
            EXPECT_EQ(bytes.Value().StorageBytes, 0u);
            EXPECT_EQ(bytes.Value().ReservedAddressBytes, 0u);
        }
        EXPECT_EQ(Arena.m_current_offset, cursor);
        EXPECT_EQ(Allocator.AllocationCount(), 0u);
    }

    TEST_F(CookCallbackStorageTest, EveryCodecReferenceMigrationAndAdapterRootRequiresDeclaredOwnership)
    {
        uint32_t root = 7;
        for (size_t kind = 0; kind < 4; ++kind)
        {
            SCOPED_TRACE(kind);
            ECS::SceneComponentSchemaRegistry schemas    = {};
            ECS::SceneMigrationRegistry       migrations = {};
            CookAdapterRegistry               adapters   = {};
            SetSchemas(schemas, kind == 0 ? &root : nullptr, kind == 1 ? &root : nullptr);
            SetMigrations(migrations, kind == 2 ? &root : nullptr);
            SetAdapters(adapters, kind == 3 ? &root : nullptr);
            auto context       = Context;
            context.Schemas    = &schemas;
            context.Migrations = &migrations;
            context.Adapters   = &adapters;
            EXPECT_EQ(QueryCookCallbackMemory(context, Allocator).Error(), VFS::VFSError::Unsupported);
            Owner wrong       = {};
            context.Callbacks = &wrong;
            EXPECT_EQ(QueryCookCallbackMemory(context, Allocator).Error(), VFS::VFSError::Unsupported);
            EXPECT_EQ(wrong.Queries, 0u);
            Owner correct     = {};
            correct.Root      = &root;
            context.Callbacks = &correct;
            ASSERT_TRUE(QueryCookCallbackMemory(context, Allocator).Succeeded());
            EXPECT_EQ(correct.Queries, 1u);
            EXPECT_EQ(correct.Covered, &Allocator);
        }
        EXPECT_EQ(Allocator.AllocationCount(), 0u);
    }

    TEST_F(CookCallbackStorageTest, SharedContextGraphIsProfiledExactlyOnceAcrossAllRegistries)
    {
        uint32_t                          root       = 3;
        ECS::SceneComponentSchemaRegistry schemas    = {};
        ECS::SceneMigrationRegistry       migrations = {};
        CookAdapterRegistry               adapters   = {};
        SetSchemas(schemas, &root, &root);
        SetMigrations(migrations, &root);
        SetAdapters(adapters, &root);
        Owner owner        = {};
        owner.Root         = &root;
        Context.Schemas    = &schemas;
        Context.Migrations = &migrations;
        Context.Adapters   = &adapters;
        Context.Callbacks  = &owner;
        const auto profile = QueryCookCallbackMemory(Context, Allocator);
        ASSERT_TRUE(profile.Succeeded());
        EXPECT_EQ(profile.Value().StateBytes, sizeof(owner) + sizeof(root));
        EXPECT_EQ(profile.Value().StorageBytes, 0u);
        EXPECT_EQ(owner.Queries, 1u);
        EXPECT_EQ(owner.Covered, &Allocator);
        owner.Error = VFS::VFSError::SizeLimitExceeded;
        EXPECT_EQ(QueryCookCallbackMemory(Context, Allocator).Error(), owner.Error);
        EXPECT_EQ(owner.Queries, 2u);
    }

    TEST_F(CookCallbackStorageTest, SharedSnapshotNeverBypassesTheCustomOwnerContractEvenWithANativeArena)
    {
        uint32_t            root     = 7;
        CookAdapterRegistry adapters = {};
        SetAdapters(adapters, &root);
        Owner owner                              = {};
        owner.Root                               = &root;
        owner.NativeArena                        = &Arena;
        Context.Adapters                         = &adapters;
        Context.Callbacks                        = &owner;
        Detail::CookNativeServiceMemory services = {};
        ASSERT_TRUE(services.Initialize(Context, owner.StorageArena()).Succeeded());
        auto profile = services.CallbackRequirements(Context, Allocator);
        ASSERT_TRUE(profile.Succeeded());
        EXPECT_EQ(profile.Value().StateBytes, sizeof(owner) + sizeof(root));
        EXPECT_EQ(profile.Value().StorageBytes, 0u);
        EXPECT_EQ(owner.Queries, 1u);
        EXPECT_EQ(owner.Covered, &Allocator);
        owner.Error = VFS::VFSError::SizeLimitExceeded;
        EXPECT_EQ(services.CallbackRequirements(Context, Allocator).Error(), owner.Error);
        EXPECT_EQ(owner.Queries, 2u);
        owner.Root = nullptr;
        EXPECT_EQ(services.CallbackRequirements(Context, Allocator).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(owner.Queries, 2u);
        EXPECT_EQ(Allocator.AllocationCount(), 0u);
    }

    TEST_F(CookCallbackStorageTest, SharedSnapshotChecksNativeRootCoverageBeforeUsingItsArenaProfile)
    {
        uint32_t            foreign  = 7;
        CookAdapterRegistry adapters = {};
        SetAdapters(adapters, &foreign);
        CookArenaCallbackStorage owner(Arena);
        Context.Adapters                         = &adapters;
        Context.Callbacks                        = &owner;
        Detail::CookNativeServiceMemory services = {};
        ASSERT_TRUE(services.Initialize(Context, owner.StorageArena()).Succeeded());
        EXPECT_EQ(services.CallbackRequirements(Context, Allocator).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(Allocator.AllocationCount(), 0u);
    }

    TEST_F(CookCallbackStorageTest, UnknownOwnerFootprintsAndInvalidServicesFailClosed)
    {
        struct Unknown final : ICookCallbackStorage
        {
            bool Contains(const void*) const noexcept override
            {
                return true;
            }
        } owner           = {};
        Context.Callbacks = &owner;
        EXPECT_EQ(QueryCookCallbackMemory(Context, Allocator).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(QueryCookCallbackMemory({}, Allocator).Error(), VFS::VFSError::InvalidPath);
        ECS::SceneMigrationRegistry migrations = {};
        Context.Migrations                     = &migrations;
        EXPECT_EQ(QueryCookCallbackMemory(Context, Allocator).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Allocator.AllocationCount(), 0u);
    }

    TEST_F(CookCallbackStorageTest, IndependentNativeArenaCountsCompleteIdleBackingAndOwnerState)
    {
        Core::Memory::ArenaAllocator separate = {};
        separate.Initialize(ZMega(2), 0);
        auto* root = separate.AllocateNoZero(1, 1);
        ASSERT_NE(root, nullptr);
        CookArenaCallbackStorage owner(separate);
        CookAdapterRegistry      adapters = {};
        SetAdapters(adapters, root);
        Context.Adapters                           = &adapters;
        Context.Callbacks                          = &owner;
        Core::Memory::ArenaRetainedStorage backing = {};
        ASSERT_TRUE(separate.RetainedStorage(&backing));
        const auto offset = separate.m_current_offset;
        for (size_t repeat = 0; repeat < 128; ++repeat)
        {
            const auto profile = QueryCookCallbackMemory(Context, Allocator);
            ASSERT_TRUE(profile.Succeeded());
            EXPECT_EQ(profile.Value().StateBytes, sizeof(owner) + sizeof(separate));
            EXPECT_EQ(profile.Value().StorageBytes, backing.CommittedBytes + backing.TrackingBytes);
            EXPECT_EQ(profile.Value().ReservedAddressBytes, backing.ReservedAddressBytes);
        }
        EXPECT_EQ(separate.m_current_offset, offset);
        separate.Clear();
        const auto reused = owner.MemoryRequirements(Context, Allocator);
        ASSERT_TRUE(reused.Succeeded());
        EXPECT_EQ(reused.Value().StorageBytes, backing.CommittedBytes + backing.TrackingBytes);
        EXPECT_EQ(Allocator.AllocationCount(), 0u);
    }

    TEST_F(CookCallbackStorageTest, SchemaSharedBackingAndArenaResidentOwnerAreNotChargedTwice)
    {
        void* memory = Arena.Allocate(sizeof(CookArenaCallbackStorage), alignof(CookArenaCallbackStorage));
        ASSERT_NE(memory, nullptr);
        auto*               owner    = new (memory) CookArenaCallbackStorage(Arena);
        CookAdapterRegistry adapters = {};
        SetAdapters(adapters, memory);
        Context.Adapters   = &adapters;
        Context.Callbacks  = owner;
        const auto profile = QueryCookCallbackMemory(Context, Allocator);
        ASSERT_TRUE(profile.Succeeded());
        EXPECT_EQ(profile.Value().StateBytes, 0u);
        EXPECT_EQ(profile.Value().StorageBytes, 0u);
        EXPECT_EQ(profile.Value().ReservedAddressBytes, 0u);
        owner->~CookArenaCallbackStorage();
    }

    TEST_F(CookCallbackStorageTest, OneNativeGraphCoversDistinctRootsAcrossEveryRegistryWithoutPerRootCharges)
    {
        Core::Memory::ArenaAllocator separate = {};
        separate.Initialize(ZMega(2), 0);
        void* roots[4] = {};
        for (auto& root : roots)
        {
            root = separate.AllocateNoZero(separate.m_mem_page_size, 1);
            ASSERT_NE(root, nullptr);
        }
        ECS::SceneComponentSchemaRegistry schemas    = {};
        ECS::SceneMigrationRegistry       migrations = {};
        CookAdapterRegistry               adapters   = {};
        SetSchemas(schemas, roots[0], roots[1]);
        SetMigrations(migrations, roots[2]);
        SetAdapters(adapters, roots[3]);
        CookArenaCallbackStorage owner(separate);
        Context.Schemas                            = &schemas;
        Context.Migrations                         = &migrations;
        Context.Adapters                           = &adapters;
        Context.Callbacks                          = &owner;
        Core::Memory::ArenaRetainedStorage backing = {};
        ASSERT_TRUE(separate.RetainedStorage(&backing));
        const auto profile = QueryCookCallbackMemory(Context, Allocator);
        ASSERT_TRUE(profile.Succeeded());
        EXPECT_EQ(profile.Value().StateBytes, sizeof(owner) + sizeof(separate));
        EXPECT_EQ(profile.Value().StorageBytes, 4 * separate.m_mem_page_size + backing.TrackingBytes);
        Detail::CookNativeServiceMemory services = {};
        ASSERT_TRUE(services.Initialize(Context, owner.StorageArena()).Succeeded());
        const auto offset = separate.m_current_offset;
        for (size_t repeat = 0; repeat < 128; ++repeat)
        {
            const auto shared = services.CallbackRequirements(Context, Allocator);
            ASSERT_TRUE(shared.Succeeded());
            EXPECT_EQ(shared.Value().StateBytes, profile.Value().StateBytes);
            EXPECT_EQ(shared.Value().StorageBytes, profile.Value().StorageBytes);
            EXPECT_EQ(shared.Value().ReservedAddressBytes, profile.Value().ReservedAddressBytes);
        }
        EXPECT_EQ(separate.m_current_offset, offset);
        EXPECT_EQ(Allocator.AllocationCount(), 0u);
    }

    TEST_F(CookCallbackStorageTest, SiblingContextViewsDeduplicateMigrationFamilyAndRetainDistinctViewState)
    {
        Core::Memory::ArenaAllocator root = {}, migration_arena = {}, context_arena = {};
        root.Initialize(ZMega(2), 0);
        root.CreateSubArena(ZKilo(64), &migration_arena);
        root.CreateSubArena(ZKilo(64), &context_arena);
        auto* state = context_arena.AllocateNoZero(1, 1);
        ASSERT_NE(state, nullptr);
        ECS::SceneMigrationRegistry migrations = {};
        ASSERT_TRUE(migrations.Initialize(&migration_arena));
        ASSERT_TRUE(migrations.RegisterDocumentMigration(1, 2, state, [](void*, const ECS::SceneYAML::Node&, ECS::SceneYAML::Node*, ECS::SceneDiagnostics*) { return false; }, nullptr));
        CookArenaCallbackStorage owner(context_arena);
        Context.Migrations = &migrations;
        Context.Callbacks  = &owner;
        const auto profile = QueryCookCallbackMemory(Context, Allocator);
        ASSERT_TRUE(profile.Succeeded());
        EXPECT_EQ(profile.Value().StateBytes, sizeof(owner) + sizeof(context_arena));
        EXPECT_EQ(profile.Value().StorageBytes, 0u);
        EXPECT_EQ(profile.Value().ReservedAddressBytes, 0u);
        CookArenaCallbackStorage existing(migration_arena);
        EXPECT_TRUE(existing.Contains(state));
        const auto same = existing.MemoryRequirements(Context, Allocator);
        ASSERT_TRUE(same.Succeeded());
        EXPECT_EQ(same.Value().StateBytes, sizeof(existing));
        EXPECT_EQ(same.Value().StorageBytes, 0u);
    }

    TEST_F(CookCallbackStorageTest, RegistryObjectsInsideAnIndependentCallbackFamilyAreCoveredOnlyByItsBacking)
    {
        Core::Memory::ArenaAllocator separate = {};
        separate.Initialize(ZMega(2), 0);
        void* memory = separate.Allocate(sizeof(ECS::SceneComponentSchemaRegistry), alignof(ECS::SceneComponentSchemaRegistry));
        ASSERT_NE(memory, nullptr);
        auto* schemas = new (memory) ECS::SceneComponentSchemaRegistry{};
        schemas->Initialize(&Arena);
        CookArenaCallbackStorage owner(separate);
        Context.Schemas       = schemas;
        Context.Callbacks     = &owner;
        const auto registries = QueryCookRegistryMemory(Context);
        const auto callbacks  = QueryCookCallbackMemory(Context, Allocator);
        ASSERT_TRUE(registries.Succeeded());
        ASSERT_TRUE(callbacks.Succeeded());
        EXPECT_EQ(registries.Value().StateBytes, sizeof(Adapters) + sizeof(Arena));
        EXPECT_EQ(callbacks.Value().StateBytes, sizeof(owner) + sizeof(separate));
        Core::Memory::ArenaRetainedStorage first = {}, second = {};
        ASSERT_TRUE(Arena.RetainedStorage(&first));
        ASSERT_TRUE(separate.RetainedStorage(&second));
        EXPECT_EQ(registries.Value().StorageBytes + callbacks.Value().StorageBytes, first.CommittedBytes + first.TrackingBytes + second.CommittedBytes + second.TrackingBytes);
        schemas->~SceneComponentSchemaRegistry();
    }

    TEST_F(CookCallbackStorageTest, CallbackViewObjectsResidentInTheirSharedFamilyDoNotAddStateCharges)
    {
        void* memory = Arena.Allocate(sizeof(Core::Memory::ArenaAllocator), alignof(Core::Memory::ArenaAllocator));
        ASSERT_NE(memory, nullptr);
        auto* child = new (memory) Core::Memory::ArenaAllocator{};
        Arena.CreateSubArena(ZKilo(64), child);
        auto* root = child->AllocateNoZero(1, 1);
        ASSERT_NE(root, nullptr);
        CookArenaCallbackStorage owner(*child);
        CookAdapterRegistry      adapters = {};
        SetAdapters(adapters, root);
        Context.Adapters   = &adapters;
        Context.Callbacks  = &owner;
        const auto profile = QueryCookCallbackMemory(Context, Allocator);
        ASSERT_TRUE(profile.Succeeded());
        EXPECT_EQ(profile.Value().StateBytes, sizeof(owner));
        EXPECT_EQ(profile.Value().StorageBytes, 0u);
        child->~ArenaAllocator();
    }

    TEST_F(CookCallbackStorageTest, NativeOwnerRejectsForeignUncommittedAndUnknownBorrowedStorage)
    {
        CookArenaCallbackStorage owner(Arena), empty = {};
        EXPECT_FALSE(owner.Contains(nullptr));
        EXPECT_FALSE(owner.Contains(&owner));
        EXPECT_FALSE(owner.Contains(Arena.m_memory + Arena.m_reserved_size - 1));
        EXPECT_FALSE(empty.Contains(Arena.m_memory));
        EXPECT_EQ(empty.MemoryRequirements(Context, Allocator).Error(), VFS::VFSError::Unsupported);
        alignas(16) uint8_t          bytes[64] = {};
        Core::Memory::ArenaAllocator borrowed  = {};
        ASSERT_TRUE(borrowed.InitializeBorrowed(bytes, sizeof(bytes)));
        CookArenaCallbackStorage unknown(borrowed);
        EXPECT_FALSE(unknown.Contains(bytes));
        EXPECT_EQ(unknown.MemoryRequirements(Context, Allocator).Error(), VFS::VFSError::Unsupported);
    }
} // namespace ZEngine::Cook
