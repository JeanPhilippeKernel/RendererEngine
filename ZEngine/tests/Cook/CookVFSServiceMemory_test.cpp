#include <ZEngine/Cook/CookAdapter.h>
#include <ZEngine/Cook/CookCallbackStorage.h>
#include <ZEngine/Cook/CookCallerMemory.h>
#include <ZEngine/Cook/CookPageAllocator.h>
#include <ZEngine/Cook/CookRegistryMemory.h>
#include <ZEngine/Cook/CookServiceMemoryInternal.h>
#include <ZEngine/Cook/CookVFSServiceMemory.h>
#include <ZEngine/Cook/SceneCooker.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/ECS/SceneCompatibility.h>
#include <gtest/gtest.h>
#include <new>
#include "../TemporaryDirectory.h"

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    class CookVFSServiceMemoryTest : public testing::Test
    {
    protected:
        Core::Memory::ArenaAllocator      SchemaArena = {};
        Core::Memory::ArenaAllocator      VFSArena    = {};
        ECS::SceneComponentSchemaRegistry Schemas     = {};
        CookAdapterRegistry               Adapters    = {};
        VFS::VFSContext                   Files       = {};
        SceneCookContext                  Context     = {};

        void                              SetUp() override
        {
            SchemaArena.Initialize(ZMega(4), 0);
            VFSArena.Initialize(ZMega(4), 0);
            ASSERT_NE(SchemaArena.m_memory, nullptr);
            ASSERT_NE(VFSArena.m_memory, nullptr);
            Schemas.Initialize(&SchemaArena);
            Files.Initialize(&VFSArena);
            const CookAdapterFns entries[] = {CookEnvironmentAdapter(), CookMaterialAdapter(), CookMeshAdapter(), CookTextureAdapter()};
            ASSERT_TRUE(Adapters.Initialize(entries).Succeeded());
            Context.Schemas  = &Schemas;
            Context.Adapters = &Adapters;
            Context.Output   = &Files;
            Context.Scratch  = &Files;
        }

        template <typename T>
        T* Store(Core::Memory::ArenaAllocator& arena)
        {
            void* storage = arena.Allocate(sizeof(T), alignof(T));
            EXPECT_NE(storage, nullptr);
            return storage ? new (storage) T{} : nullptr;
        }

        struct DeclaredContext final : VFS::VFSContext
        {
            VFS::VFSContextStorage                 Storage = {};
            mutable size_t                         Queries = 0;

            VFS::VFSResult<VFS::VFSContextStorage> NativeStorage() const noexcept override
            {
                ++Queries;
                return VFS::VFSResult<VFS::VFSContextStorage>::Ok(Storage);
            }
        };
    };

    TEST_F(CookVFSServiceMemoryTest, SharedFacadeCountsCompleteCommittedArenaAndOwnerButNotVirtualReservation)
    {
        const auto storage = Files.NativeStorage();
        ASSERT_TRUE(storage.Succeeded());
        EXPECT_EQ(storage.Value().Object, &Files);
        EXPECT_EQ(storage.Value().ObjectBytes, sizeof(Files));
        EXPECT_EQ(storage.Value().Arena, &VFSArena);
        Core::Memory::ArenaRetainedStorage backing = {};
        ASSERT_TRUE(VFSArena.RetainedStorage(&backing));
        const auto offset = VFSArena.m_current_offset;
        for (size_t repeat = 0; repeat < 128; ++repeat)
        {
            const auto requirements = QueryCookVFSServiceMemory(Context);
            ASSERT_TRUE(requirements.Succeeded());
            EXPECT_EQ(requirements.Value().StateBytes, sizeof(Files) + sizeof(VFSArena));
            EXPECT_EQ(requirements.Value().StorageBytes, backing.CommittedBytes + backing.TrackingBytes);
            EXPECT_EQ(requirements.Value().ReservedAddressBytes, backing.ReservedAddressBytes);
        }
        EXPECT_EQ(VFSArena.m_current_offset, offset);
        EXPECT_EQ(VFSArena.m_committed_size, backing.CommittedBytes);
        EXPECT_GT(backing.CommittedBytes, 0u);
        EXPECT_LT(backing.CommittedBytes + backing.TrackingBytes, backing.ReservedAddressBytes);
    }

    TEST_F(CookVFSServiceMemoryTest, NativeCallbackProjectionReusesTheSnapshotWithoutRequeryingEitherFacade)
    {
        DeclaredContext first = {}, second = {};
        first.Storage   = {&first, sizeof(first), &VFSArena};
        second.Storage  = {&second, sizeof(second), &VFSArena};
        Context.Output  = &first;
        Context.Scratch = &second;
        CookArenaCallbackStorage owner(VFSArena);
        Context.Callbacks           = &owner;
        CookPageAllocator allocator = {};
        ASSERT_TRUE(allocator.Initialize(ZMega(16)).Succeeded());
        const auto standalone = QueryCookCallbackMemory(Context, allocator);
        ASSERT_TRUE(standalone.Succeeded());
        first.Queries = second.Queries           = 0;
        Detail::CookNativeServiceMemory services = {};
        ASSERT_TRUE(services.Initialize(Context, owner.StorageArena()).Succeeded());
        const auto offset = VFSArena.m_current_offset;
        for (size_t repeat = 0; repeat < 128; ++repeat)
        {
            const auto shared = services.CallbackRequirements(Context, allocator);
            ASSERT_TRUE(shared.Succeeded());
            EXPECT_EQ(shared.Value().StateBytes, standalone.Value().StateBytes);
            EXPECT_EQ(shared.Value().StorageBytes, standalone.Value().StorageBytes);
            EXPECT_EQ(shared.Value().ReservedAddressBytes, standalone.Value().ReservedAddressBytes);
            EXPECT_TRUE(services.RegistryRequirements(Context).Succeeded());
            EXPECT_TRUE(services.VFSRequirements().Succeeded());
        }
        EXPECT_EQ(first.Queries, 1u);
        EXPECT_EQ(second.Queries, 1u);
        EXPECT_EQ(VFSArena.m_current_offset, offset);
        EXPECT_EQ(allocator.AllocationCount(), 0u);
    }

    TEST_F(CookVFSServiceMemoryTest, EachDistinctFacadeIsQueriedOnceWithinOneProfile)
    {
        DeclaredContext first = {}, second = {};
        first.Storage   = {&first, sizeof(first), &VFSArena};
        second.Storage  = {&second, sizeof(second), &VFSArena};
        Context.Output  = &first;
        Context.Scratch = &first;
        ASSERT_TRUE(QueryCookVFSServiceMemory(Context).Succeeded());
        EXPECT_EQ(first.Queries, 1u);
        Context.Scratch         = &second;
        const auto requirements = QueryCookVFSServiceMemory(Context);
        ASSERT_TRUE(requirements.Succeeded());
        EXPECT_EQ(first.Queries, 2u);
        EXPECT_EQ(second.Queries, 1u);
        EXPECT_EQ(requirements.Value().StateBytes, sizeof(first) + sizeof(second) + sizeof(VFSArena));
        EXPECT_EQ(requirements.Value().StorageBytes, VFSArena.m_committed_size + VFSArena.AllocationTrackingBytes());
    }

    TEST_F(CookVFSServiceMemoryTest, OneNativeSnapshotServesRegistryVFSAndCallerQueriesWithoutReinitialization)
    {
        DeclaredContext first = {}, second = {};
        first.Storage            = {&first, sizeof(first), &VFSArena};
        second.Storage           = {&second, sizeof(second), &VFSArena};
        Context.Output           = &first;
        Context.Scratch          = &second;
        SceneCookRequest request = {
            .Sources = {&Context.ScratchRoot, 1}
        };
        SceneCookResult result      = {};
        CookDiagnostics diagnostics = {};
        const auto      registry    = QueryCookRegistryMemory(Context);
        const auto      vfs         = QueryCookVFSServiceMemory(Context);
        const auto      caller      = QueryCookCallerMemory(request, Context, &result, &diagnostics);
        ASSERT_TRUE(registry.Succeeded());
        ASSERT_TRUE(vfs.Succeeded());
        ASSERT_TRUE(caller.Succeeded());
        first.Queries = second.Queries                = 0;
        const auto                      schema_offset = SchemaArena.m_current_offset;
        const auto                      vfs_offset    = VFSArena.m_current_offset;
        const auto                      schema_pages  = SchemaArena.m_committed_size;
        const auto                      vfs_pages     = VFSArena.m_committed_size;
        Detail::CookNativeServiceMemory memory        = {};
        ASSERT_TRUE(memory.Initialize(Context, nullptr).Succeeded());
        for (size_t repeat = 0; repeat < 128; ++repeat)
        {
            const auto registries = memory.RegistryRequirements(Context);
            const auto facades    = memory.VFSRequirements();
            const auto values     = memory.CallerRequirements(request, Context, &result, &diagnostics);
            ASSERT_TRUE(registries.Succeeded());
            ASSERT_TRUE(facades.Succeeded());
            ASSERT_TRUE(values.Succeeded());
            EXPECT_EQ(registries.Value().StateBytes, registry.Value().StateBytes);
            EXPECT_EQ(registries.Value().StorageBytes, registry.Value().StorageBytes);
            EXPECT_EQ(registries.Value().ReservedAddressBytes, registry.Value().ReservedAddressBytes);
            EXPECT_EQ(facades.Value().StateBytes, vfs.Value().StateBytes);
            EXPECT_EQ(facades.Value().StorageBytes, vfs.Value().StorageBytes);
            EXPECT_EQ(facades.Value().ReservedAddressBytes, vfs.Value().ReservedAddressBytes);
            EXPECT_EQ(values.Value(), caller.Value());
        }
        EXPECT_EQ(first.Queries, 1u);
        EXPECT_EQ(second.Queries, 1u);
        EXPECT_EQ(SchemaArena.m_current_offset, schema_offset);
        EXPECT_EQ(VFSArena.m_current_offset, vfs_offset);
        EXPECT_EQ(SchemaArena.m_committed_size, schema_pages);
        EXPECT_EQ(VFSArena.m_committed_size, vfs_pages);
        EXPECT_EQ(memory.CallerRequirements(request, Context, nullptr, &diagnostics).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(memory.CallerRequirements(request, Context, &result, nullptr).Error(), VFS::VFSError::InvalidPath);
    }

    TEST_F(CookVFSServiceMemoryTest, SchemaSharedArenaIsNotChargedAgainForTwoIndependentFacades)
    {
        VFS::VFSContext first = {}, second = {};
        first.Initialize(&SchemaArena);
        second.Initialize(&SchemaArena);
        Context.Output          = &first;
        Context.Scratch         = &second;
        const auto requirements = QueryCookVFSServiceMemory(Context);
        const auto registry     = QueryCookRegistryMemory(Context);
        ASSERT_TRUE(requirements.Succeeded());
        ASSERT_TRUE(registry.Succeeded());
        EXPECT_EQ(requirements.Value().StateBytes, sizeof(first) + sizeof(second));
        EXPECT_EQ(requirements.Value().StorageBytes, 0u);
        EXPECT_EQ(requirements.Value().ReservedAddressBytes, 0u);
        EXPECT_EQ(registry.Value().StorageBytes, SchemaArena.m_committed_size + SchemaArena.AllocationTrackingBytes());
    }

    TEST_F(CookVFSServiceMemoryTest, IndependentOutputAndScratchNativeFamiliesIncludeBothBackingDomains)
    {
        Core::Memory::ArenaAllocator separate = {};
        separate.Initialize(ZMega(4), 0);
        VFS::VFSContext scratch = {};
        scratch.Initialize(&separate);
        Context.Scratch         = &scratch;
        const auto requirements = QueryCookVFSServiceMemory(Context);
        ASSERT_TRUE(requirements.Succeeded());
        EXPECT_EQ(requirements.Value().StateBytes, sizeof(Files) + sizeof(scratch) + sizeof(VFSArena) + sizeof(separate));
        EXPECT_EQ(requirements.Value().StorageBytes, VFSArena.m_committed_size + VFSArena.AllocationTrackingBytes() + separate.m_committed_size + separate.AllocationTrackingBytes());
        EXPECT_EQ(requirements.Value().ReservedAddressBytes, VFSArena.m_reserved_size + separate.m_reserved_size);
    }

    TEST_F(CookVFSServiceMemoryTest, CallbackFamilyCoversFacadeBackingWhileVFSFamilyCoversResidentSchemaObject)
    {
        auto* schemas = Store<ECS::SceneComponentSchemaRegistry>(VFSArena);
        ASSERT_NE(schemas, nullptr);
        schemas->Initialize(&SchemaArena);
        CookArenaCallbackStorage callbacks(VFSArena);
        Context.Schemas             = schemas;
        Context.Callbacks           = &callbacks;
        const auto        vfs       = QueryCookVFSServiceMemory(Context);
        const auto        registry  = QueryCookRegistryMemory(Context);
        CookPageAllocator allocator = {};
        const auto        callback  = QueryCookCallbackMemory(Context, allocator);
        ASSERT_TRUE(vfs.Succeeded());
        ASSERT_TRUE(registry.Succeeded());
        ASSERT_TRUE(callback.Succeeded());
        EXPECT_EQ(vfs.Value().StateBytes, sizeof(Files));
        EXPECT_EQ(vfs.Value().StorageBytes, 0u);
        EXPECT_EQ(registry.Value().StateBytes, sizeof(Adapters) + sizeof(SchemaArena));
        EXPECT_EQ(callback.Value().StateBytes, sizeof(callbacks) + sizeof(VFSArena));
        EXPECT_EQ(callback.Value().StorageBytes, VFSArena.m_committed_size + VFSArena.AllocationTrackingBytes());
        schemas->~SceneComponentSchemaRegistry();
    }

    TEST_F(CookVFSServiceMemoryTest, ArenaResidentFacadeHasNoDuplicateInlineCharge)
    {
        auto* files = Store<VFS::VFSContext>(SchemaArena);
        ASSERT_NE(files, nullptr);
        files->Initialize(&VFSArena);
        Context.Output          = files;
        Context.Scratch         = files;
        const auto requirements = QueryCookVFSServiceMemory(Context);
        ASSERT_TRUE(requirements.Succeeded());
        EXPECT_EQ(requirements.Value().StateBytes, sizeof(VFSArena));
        EXPECT_EQ(requirements.Value().StorageBytes, VFSArena.m_committed_size + VFSArena.AllocationTrackingBytes());
        files->Shutdown();
        files->~VFSContext();
    }

    TEST_F(CookVFSServiceMemoryTest, ExplicitFacadeInlineObjectCoversNestedCallerValuesAndArenaOwner)
    {
        struct CompleteContext final : VFS::VFSContext
        {
            Core::Memory::ArenaAllocator           Arena       = {};
            SceneCookRequest                       Request     = {};
            SceneCookContext                       Context     = {};
            SceneCookResult                        Result      = {};
            CookDiagnostics                        Diagnostics = {};

            VFS::VFSResult<VFS::VFSContextStorage> NativeStorage() const noexcept override
            {
                return VFS::VFSResult<VFS::VFSContextStorage>::Ok({this, sizeof(*this), &Arena});
            }
        } complete = {};
        complete.Arena.Initialize(ZMega(4), 0);
        complete.Initialize(&complete.Arena);
        complete.Context         = Context;
        complete.Context.Output  = &complete;
        complete.Context.Scratch = &complete;
        complete.Request.Sources = {&complete.Request.OutputRoot, 1};
        const auto requirements  = QueryCookVFSServiceMemory(complete.Context);
        const auto caller        = QueryCookCallerMemory(complete.Request, complete.Context, &complete.Result, &complete.Diagnostics);
        ASSERT_TRUE(requirements.Succeeded());
        ASSERT_TRUE(caller.Succeeded());
        EXPECT_EQ(requirements.Value().StateBytes, sizeof(complete));
        EXPECT_EQ(caller.Value(), 0u);
        complete.Shutdown();
    }

    TEST_F(CookVFSServiceMemoryTest, UnknownDerivedUninitializedBorrowedAndActiveImportContextsFailClosed)
    {
        struct UnknownContext final : VFS::VFSContext
        {
        } unknown = {};
        unknown.Initialize(&VFSArena);
        Context.Output = &unknown;
        EXPECT_EQ(QueryCookVFSServiceMemory(Context).Error(), VFS::VFSError::Unsupported);
        VFS::VFSContext uninitialized = {};
        Context.Output                = &uninitialized;
        EXPECT_EQ(uninitialized.NativeStorage().Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(QueryCookVFSServiceMemory(Context).Error(), VFS::VFSError::Unsupported);
        alignas(DEFAULT_ALIGNMENT) uint8_t buffer[ZKilo(256)] = {};
        Core::Memory::ArenaAllocator       borrowed           = {};
        ASSERT_TRUE(borrowed.InitializeBorrowed(buffer, sizeof(buffer)));
        VFS::VFSContext files = {};
        files.Initialize(&borrowed);
        Context.Output = &files;
        EXPECT_EQ(files.NativeStorage().Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(QueryCookVFSServiceMemory(Context).Error(), VFS::VFSError::Unsupported);
        Context.Output         = &Files;
        const auto publication = Files.BeginImportPublication();
        EXPECT_EQ(QueryCookVFSServiceMemory(Context).Error(), VFS::VFSError::Unsupported);
        Files.EndImportPublication(publication);
        EXPECT_TRUE(QueryCookVFSServiceMemory(Context).Succeeded());
    }

    TEST_F(CookVFSServiceMemoryTest, AttachedNativeWatcherIsRefusedUntilItsThreadAndExternalStateAreDetached)
    {
        ZEngine::Tests::TemporaryDirectory directory = {};
        std::error_code                    error     = {};
        ASSERT_TRUE(directory.Create("zengine_cook_facade_watcher", error));
        const auto native = directory.Path().generic_u8string();
        ASSERT_TRUE(QueryCookVFSServiceMemory(Context).Succeeded());
        Files.InitWatcher(reinterpret_cast<cstring>(native.c_str()), nullptr, nullptr);
        EXPECT_EQ(QueryCookVFSServiceMemory(Context).Error(), VFS::VFSError::Unsupported);
        Files.ShutdownWatcher();
        // Watcher arena allocations may be retained, but the complete native
        // family now accounts those pages and no live external graph remains.
        EXPECT_TRUE(QueryCookVFSServiceMemory(Context).Succeeded());
    }

    TEST_F(CookVFSServiceMemoryTest, MalformedCustomStorageCannotDescribeUnrelatedOrOverflowingObjectRanges)
    {
        DeclaredContext custom = {};
        Context.Output         = &custom;
        for (const VFS::VFSContextStorage storage : {
             VFS::VFSContextStorage{},
             VFS::VFSContextStorage{&custom, 0, &VFSArena},
             VFS::VFSContextStorage{&custom, sizeof(custom), nullptr},
             VFS::VFSContextStorage{&Files, sizeof(Files), &VFSArena},
             VFS::VFSContextStorage{&custom, SIZE_MAX, &VFSArena},
        })
        {
            custom.Storage = storage;
            EXPECT_EQ(QueryCookVFSServiceMemory(Context).Error(), VFS::VFSError::Unsupported);
        }
    }

    TEST_F(CookVFSServiceMemoryTest, SharedCompositeFacadeObjectDeduplicatesAdjustedBaseViewsAndRejectsConflictingProfiles)
    {
        struct Composite
        {
            DeclaredContext First  = {};
            DeclaredContext Second = {};
        } composite              = {};
        composite.First.Storage  = {&composite, sizeof(composite), &VFSArena};
        composite.Second.Storage = composite.First.Storage;
        Context.Output           = &composite.First;
        Context.Scratch          = &composite.Second;
        const auto requirements  = QueryCookVFSServiceMemory(Context);
        ASSERT_TRUE(requirements.Succeeded());
        EXPECT_EQ(requirements.Value().StateBytes, sizeof(composite) + sizeof(VFSArena));
        EXPECT_EQ(requirements.Value().StorageBytes, VFSArena.m_committed_size + VFSArena.AllocationTrackingBytes());
        --composite.Second.Storage.ObjectBytes;
        EXPECT_EQ(QueryCookVFSServiceMemory(Context).Error(), VFS::VFSError::Corrupted);
        composite.Second.Storage       = composite.First.Storage;
        composite.Second.Storage.Arena = &SchemaArena;
        EXPECT_EQ(QueryCookVFSServiceMemory(Context).Error(), VFS::VFSError::Corrupted);
        composite.Second.Storage = {&composite.Second, sizeof(composite.Second), &VFSArena};
        Context.Output           = &composite.Second;
        Context.Scratch          = &composite.First;
        const auto nested        = QueryCookVFSServiceMemory(Context);
        ASSERT_TRUE(nested.Succeeded());
        EXPECT_EQ(nested.Value().StateBytes, requirements.Value().StateBytes);
    }

    TEST_F(CookVFSServiceMemoryTest, AbsentOptionalFacadesAddNoStorageAndInvalidRegistriesStillFail)
    {
        Context.Output          = nullptr;
        Context.Scratch         = nullptr;
        const auto requirements = QueryCookVFSServiceMemory(Context);
        ASSERT_TRUE(requirements.Succeeded());
        EXPECT_EQ(requirements.Value().StateBytes, 0u);
        EXPECT_EQ(requirements.Value().StorageBytes, 0u);
        EXPECT_EQ(requirements.Value().ReservedAddressBytes, 0u);
        EXPECT_EQ(QueryCookVFSServiceMemory({}).Error(), VFS::VFSError::InvalidPath);
    }
} // namespace ZEngine::Cook
