#include <ZEngine/Cook/CookAdapter.h>
#include <ZEngine/Cook/CookCallbackStorage.h>
#include <ZEngine/Cook/CookCallerMemory.h>
#include <ZEngine/Cook/SceneCooker.h>
#include <ZEngine/ECS/SceneCompatibility.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <new>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    class CookCallerMemoryTest : public testing::Test
    {
    protected:
        Core::Memory::ArenaAllocator      Arena       = {};
        ECS::SceneComponentSchemaRegistry Schemas     = {};
        CookAdapterRegistry               Adapters    = {};
        VFS::VFSPath                      Sources[3]  = {};
        SceneCookRequest                  Request     = {};
        SceneCookContext                  Context     = {};
        SceneCookResult                   Result      = {};
        CookDiagnostics                   Diagnostics = {};

        void                              SetUp() override
        {
            Arena.Initialize(ZMega(4), 0, "CallerMemoryTest");
            ASSERT_NE(Arena.m_memory, nullptr);
            Schemas.Initialize(&Arena);
            const CookAdapterFns adapters[] = {CookEnvironmentAdapter(), CookMaterialAdapter(), CookMeshAdapter(), CookTextureAdapter()};
            ASSERT_TRUE(Adapters.Initialize(adapters).Succeeded());
            Request.Sources  = Sources;
            Context.Schemas  = &Schemas;
            Context.Adapters = &Adapters;
        }

        template <typename T>
        T* Store(Core::Memory::ArenaAllocator& arena)
        {
            void* storage = arena.Allocate(sizeof(T), alignof(T));
            EXPECT_NE(storage, nullptr);
            return storage ? new (storage) T{} : nullptr;
        }

        uint64_t ValueBytes() const noexcept
        {
            return sizeof(Request) + sizeof(Context) + sizeof(Sources) + sizeof(Result) + sizeof(Diagnostics);
        }
    };

    TEST_F(CookCallerMemoryTest, CompleteSourceSpanAndEveryCallerValueCountWithoutAllocationOrMutation)
    {
        const auto offset  = Arena.m_current_offset;
        const auto pages   = Arena.m_committed_size;
        Result.ObjectCount = 77;
        Diagnostics.Count  = 1;
        for (size_t repeat = 0; repeat < 128; ++repeat)
        {
            const auto bytes = QueryCookCallerMemory(Request, Context, &Result, &Diagnostics);
            ASSERT_TRUE(bytes.Succeeded());
            EXPECT_EQ(bytes.Value(), ValueBytes());
        }
        EXPECT_EQ(Arena.m_current_offset, offset);
        EXPECT_EQ(Arena.m_committed_size, pages);
        EXPECT_EQ(Result.ObjectCount, 77u);
        EXPECT_EQ(Diagnostics.Count, 1u);
    }

    TEST_F(CookCallerMemoryTest, SourcePathsBorrowedFromRequestOrContextDoNotCountTwice)
    {
        for (const VFS::VFSPath* source : {&Request.OutputRoot, &Context.ScratchRoot})
        {
            Request.Sources  = {source, 1};
            const auto bytes = QueryCookCallerMemory(Request, Context, &Result, &Diagnostics);
            ASSERT_TRUE(bytes.Succeeded());
            EXPECT_EQ(bytes.Value(), ValueBytes() - sizeof(Sources));
        }
    }

    TEST_F(CookCallerMemoryTest, EveryFiveValueAddressOrderHasTheSameAllocationFreeUnion)
    {
        Core::Memory::ArenaAllocator separate = {};
        separate.Initialize(ZMega(2), 0);
        const auto                             slot_bytes = std::max({sizeof(SceneCookRequest), sizeof(SceneCookContext), sizeof(VFS::VFSPath), sizeof(SceneCookResult), sizeof(CookDiagnostics)});
        Core::Containers::FixedArray<void*, 5> slots      = {};
        for (auto& slot : slots)
        {
            slot = separate.Allocate(slot_bytes, DEFAULT_ALIGNMENT);
            ASSERT_NE(slot, nullptr);
        }
        const auto                                offset       = separate.m_current_offset;
        const auto                                pages        = separate.m_committed_size;
        Core::Containers::FixedArray<uint32_t, 5> order        = {0, 1, 2, 3, 4};
        uint32_t                                  permutations = 0;
        do
        {
            auto* request     = new (slots[order[0]]) SceneCookRequest{};
            auto* context     = new (slots[order[1]]) SceneCookContext{Context};
            auto* source      = new (slots[order[2]]) VFS::VFSPath{};
            auto* result      = new (slots[order[3]]) SceneCookResult{};
            auto* diagnostics = new (slots[order[4]]) CookDiagnostics{};
            request->Sources  = {source, 1};
            const auto bytes  = QueryCookCallerMemory(*request, *context, result, diagnostics);
            ASSERT_TRUE(bytes.Succeeded());
            EXPECT_EQ(bytes.Value(), sizeof(*request) + sizeof(*context) + sizeof(*source) + sizeof(*result) + sizeof(*diagnostics));
            request->~SceneCookRequest();
            context->~SceneCookContext();
            source->~VFSPath();
            result->~SceneCookResult();
            diagnostics->~CookDiagnostics();
            ++permutations;
        } while (std::next_permutation(order.begin(), order.end()));
        EXPECT_EQ(permutations, 120u);
        EXPECT_EQ(separate.m_current_offset, offset);
        EXPECT_EQ(separate.m_committed_size, pages);
    }

    TEST_F(CookCallerMemoryTest, NativeRegistryFamilyAlreadyCoversCallerValuesAndSourcePaths)
    {
        struct CallerValues
        {
            SceneCookRequest Request     = {};
            SceneCookContext Context     = {};
            VFS::VFSPath     Sources[3]  = {};
            SceneCookResult  Result      = {};
            CookDiagnostics  Diagnostics = {};
        };
        auto* values = Store<CallerValues>(Arena);
        ASSERT_NE(values, nullptr);
        values->Request         = Request;
        values->Request.Sources = values->Sources;
        values->Context         = Context;
        const auto offset       = Arena.m_current_offset;
        const auto bytes        = QueryCookCallerMemory(values->Request, values->Context, &values->Result, &values->Diagnostics);
        ASSERT_TRUE(bytes.Succeeded());
        EXPECT_EQ(bytes.Value(), 0u);
        EXPECT_EQ(Arena.m_current_offset, offset);
        values->~CallerValues();
    }

    TEST_F(CookCallerMemoryTest, DistinctNativeCallbackAndMigrationFamiliesCoverOnlyTheirResidentValues)
    {
        Core::Memory::ArenaAllocator callback_arena = {}, migration_arena = {};
        callback_arena.Initialize(ZMega(2), 0);
        migration_arena.Initialize(ZMega(2), 0);
        CookArenaCallbackStorage    callbacks(callback_arena);
        ECS::SceneMigrationRegistry migrations = {};
        ASSERT_TRUE(migrations.Initialize(&migration_arena));
        Context.Callbacks  = &callbacks;
        Context.Migrations = &migrations;
        auto* result       = Store<SceneCookResult>(callback_arena);
        auto* diagnostics  = Store<CookDiagnostics>(migration_arena);
        ASSERT_NE(result, nullptr);
        ASSERT_NE(diagnostics, nullptr);
        const auto bytes = QueryCookCallerMemory(Request, Context, result, diagnostics);
        ASSERT_TRUE(bytes.Succeeded());
        EXPECT_EQ(bytes.Value(), sizeof(Request) + sizeof(Context) + sizeof(Sources));
        diagnostics->~CookDiagnostics();
        result->~SceneCookResult();
    }

    TEST_F(CookCallerMemoryTest, SourceSpanInAnUnrelatedArenaIsNotMistakenForChargedStorage)
    {
        Core::Memory::ArenaAllocator separate = {};
        separate.Initialize(ZMega(1), 0);
        auto* source = Store<VFS::VFSPath>(separate);
        ASSERT_NE(source, nullptr);
        Request.Sources  = {source, 1};
        const auto bytes = QueryCookCallerMemory(Request, Context, &Result, &Diagnostics);
        ASSERT_TRUE(bytes.Succeeded());
        EXPECT_EQ(bytes.Value(), ValueBytes() - sizeof(Sources) + sizeof(*source));
        source->~VFSPath();
    }

    TEST_F(CookCallerMemoryTest, InvalidCallerValuesOrUnknownServiceBackingFailClosed)
    {
        EXPECT_EQ(QueryCookCallerMemory(Request, Context, nullptr, &Diagnostics).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(QueryCookCallerMemory(Request, Context, &Result, nullptr).Error(), VFS::VFSError::InvalidPath);
        Request.Sources = {};
        EXPECT_EQ(QueryCookCallerMemory(Request, Context, &Result, &Diagnostics).Error(), VFS::VFSError::InvalidPath);
        Request.Sources                                       = Sources;
        alignas(DEFAULT_ALIGNMENT) uint8_t storage[ZKilo(32)] = {};
        Core::Memory::ArenaAllocator       borrowed           = {};
        ASSERT_TRUE(borrowed.InitializeBorrowed(storage, sizeof(storage)));
        ECS::SceneComponentSchemaRegistry schemas = {};
        schemas.Initialize(&borrowed);
        Context.Schemas   = &schemas;
        const auto offset = borrowed.m_current_offset;
        EXPECT_EQ(QueryCookCallerMemory(Request, Context, &Result, &Diagnostics).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(borrowed.m_current_offset, offset);
    }
} // namespace ZEngine::Cook
