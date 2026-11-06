#include <ZEngine/Cook/CookBudgetAllocator.h>
#include <ZEngine/Cook/CookCodecAllocationRoute.h>
#include <ZEngine/Cook/CookPageAllocator.h>
#include <ZEngine/Cook/CookScratchAllocator.h>
#include <ZEngine/Core/Serialization/YAMLAllocation.h>
#include <ZEngine/ECS/SceneYAMLReader.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <yaml.h>
#include <algorithm>
#include <limits>
#include <thread>
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    class CookYAMLAllocationTest : public testing::Test
    {
    protected:
        struct FailingAllocator final : ICookAllocator
        {
            ICookAllocator*       Parent = nullptr;
            size_t                Calls  = 0;
            size_t                FailAt = 0;

            VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
            {
                if (++Calls == FailAt)
                {
                    return VFS::VFSResult<void*>::Fail(VFS::VFSError::SizeLimitExceeded);
                }
                return Parent->Allocate(bytes, alignment, category);
            }
            void Free(void* pointer) noexcept override
            {
                Parent->Free(pointer);
            }
            CookMemoryStatistics Statistics() const noexcept override
            {
                return Parent->Statistics();
            }
            VFS::VFSResult<void> TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
            {
                return Parent->TryCharge(bytes, category, output);
            }
        };

        Tests::FixedAllocator<ZKilo(512), 1> Parent    = {};
        CookScratchAllocator                 Scratch   = {};
        FailingAllocator                     Allocator = {};

        void                                 SetUp() override
        {
            // Keep the retained slab unchanged; parser fixed state lives
            // outside that slab and needs independent ledger headroom.
            ASSERT_TRUE(Parent.Ledger.Initialize(ZMega(1)).Succeeded());
            ASSERT_TRUE(Scratch.Initialize(Parent, ZKilo(512), CookMemoryCategory::Codecs).Succeeded());
            Allocator.Parent = &Scratch;
        }
        void TearDown() override
        {
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
            Scratch.Release();
        }

        bool Read(ECS::SceneYAML::Node& output, cstring source = "root: [one, two, {three: four}]\n")
        {
            return ECS::ReadSceneYAML(source, {}, &output, nullptr);
        }
    };

    TEST_F(CookYAMLAllocationTest, SharedParserUsesBudgetedStorageAndReleasesEveryBlock)
    {
        CookWorkspace workspace = {};
        ASSERT_TRUE(workspace.Initialize(Scratch, ZKilo(64), CookMemoryCategory::Codecs).Succeeded());
        ECS::SceneYAML::Storage storage = {};
        ASSERT_TRUE(storage.InitializeBorrowed(workspace.Arena()->m_memory, workspace.Arena()->m_total_size));
        EXPECT_TRUE(storage.Allocator()->m_is_borrowed);
        auto output = storage.Destination();
        for (size_t repeat = 0; repeat < 32; ++repeat)
        {
            CookCodecAllocationRoute                 route(Allocator);
            Core::Serialization::YAMLAllocationScope scope(route);
            ASSERT_TRUE(Read(output));
            EXPECT_EQ(output["root"].size(), 3u);
            EXPECT_EQ(route.Error(), VFS::VFSError::OK);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 1u); // Only retained tree backing.
        }
        EXPECT_GT(Allocator.Calls, 32u);
        EXPECT_EQ(Parent.Allocations, 1u);
    }

    TEST_F(CookYAMLAllocationTest, BorrowedTreeRejectsInvalidInitializationWithoutReplacingExistingState)
    {
        CookWorkspace workspace = {};
        ASSERT_TRUE(workspace.Initialize(Scratch, ZKilo(64), CookMemoryCategory::Codecs).Succeeded());
        ECS::SceneYAML::Storage storage = {};
        auto*                   memory  = workspace.Arena()->m_memory;
        EXPECT_FALSE(storage.InitializeBorrowed(nullptr, ZKilo(64)));
        EXPECT_FALSE(storage.InitializeBorrowed(memory, 0));
        EXPECT_FALSE(storage.InitializeBorrowed(memory, ZKilo(64), 0));
        EXPECT_FALSE(storage.InitializeBorrowed(memory, ZMega(256) + 1));
        EXPECT_FALSE(storage.Good());
        EXPECT_EQ(storage.ByteCapacity(), 0u);
        ASSERT_TRUE(storage.InitializeBorrowed(memory, ZKilo(64)));
        const auto retained = storage.Scalar("retained");
        const auto used     = storage.BytesUsed();
        EXPECT_FALSE(storage.InitializeBorrowed(memory, ZKilo(32)));
        EXPECT_FALSE(storage.Initialize(ZKilo(32)));
        EXPECT_EQ(storage.Allocator()->m_memory, memory);
        EXPECT_EQ(storage.ByteCapacity(), ZKilo(64));
        EXPECT_EQ(storage.BytesUsed(), used);
        EXPECT_EQ(retained.Scalar(), "retained");
    }

    TEST_F(CookYAMLAllocationTest, BorrowedTreeExhaustionRollsBackAndCanReuseTheSameBudgetedBlock)
    {
        CookWorkspace workspace = {};
        ASSERT_TRUE(workspace.Initialize(Scratch, 4096, CookMemoryCategory::Codecs).Succeeded());
        ECS::SceneYAML::Storage storage = {};
        ASSERT_TRUE(storage.InitializeBorrowed(workspace.Arena()->m_memory, workspace.Arena()->m_total_size));
        auto       output       = storage.Scalar("retained");
        const auto used         = storage.BytesUsed();
        char       source[4004] = {};
        source[0]               = '[';
        std::fill_n(source + 1, 4000, 'a');
        source[4001] = ']';
        CookCodecAllocationRoute                 route(Allocator);
        Core::Serialization::YAMLAllocationScope scope(route);
        EXPECT_FALSE(Read(output, source));
        EXPECT_EQ(route.Error(), VFS::VFSError::OK);
        EXPECT_EQ(output.Scalar(), "retained");
        EXPECT_EQ(storage.BytesUsed(), used);
        EXPECT_TRUE(storage.Good());
        EXPECT_TRUE(Read(output, "[one, two]"));
        EXPECT_EQ(output.size(), 2u);
        EXPECT_EQ(Scratch.OutstandingAllocations(), 1u);
    }

    TEST_F(CookYAMLAllocationTest, EveryParserAllocationFailurePreservesOutputAndRewindsTree)
    {
        ECS::SceneYAML::Storage storage = {};
        ASSERT_TRUE(storage.Initialize(ZMega(1)));
        auto output = storage.Scalar("unchanged");
        {
            CookCodecAllocationRoute                 route(Allocator);
            Core::Serialization::YAMLAllocationScope scope(route);
            auto                                     candidate = storage.Destination();
            ASSERT_TRUE(Read(candidate));
        }
        const auto total = Allocator.Calls;
        ASSERT_GT(total, 0u);
        for (size_t failure = 1; failure <= total; ++failure)
        {
            SCOPED_TRACE(failure);
            Allocator.Calls                                = 0;
            Allocator.FailAt                               = failure;
            const auto                               bytes = storage.BytesUsed();
            CookCodecAllocationRoute                 route(Allocator);
            Core::Serialization::YAMLAllocationScope scope(route);
            EXPECT_FALSE(Read(output));
            EXPECT_EQ(route.Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(output.Scalar(), "unchanged");
            EXPECT_EQ(storage.BytesUsed(), bytes);
            EXPECT_TRUE(storage.Good());
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        }
        Allocator.Calls = Allocator.FailAt = 0;
        CookCodecAllocationRoute                 route(Allocator);
        Core::Serialization::YAMLAllocationScope scope(route);
        EXPECT_TRUE(Read(output));
    }

    TEST_F(CookYAMLAllocationTest, RoutedTreeInitializationFailureDoesNotPublishStorageAndCanRetry)
    {
        CookCodecAllocationRoute failed_route(Allocator), route(Allocator);
        ECS::SceneYAML::Storage  storage = {};
        Allocator.FailAt                 = Allocator.Calls + 1;
        EXPECT_FALSE(storage.Initialize(ZKilo(64), 1048576, 65536, 4096, &failed_route));
        EXPECT_EQ(failed_route.Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_FALSE(storage.Good());
        EXPECT_EQ(storage.ByteCapacity(), 0u);
        EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        Allocator.FailAt = 0;
        ASSERT_TRUE(storage.Initialize(ZKilo(64), 1048576, 65536, 4096, &route));
        EXPECT_TRUE(storage.Allocator()->m_is_borrowed);
        const auto calls = Allocator.Calls;
        EXPECT_FALSE(storage.Initialize(ZKilo(64), 1048576, 65536, 4096, &route));
        EXPECT_EQ(Allocator.Calls, calls);
        EXPECT_EQ(storage.ByteCapacity(), ZKilo(64));
    }

    TEST_F(CookYAMLAllocationTest, CheckpointsRestoreFrozenStateAndPermitScratchReuseWithoutAnotherAllocation)
    {
        CookCodecAllocationRoute route(Allocator);
        ECS::SceneYAML::Storage  storage = {};
        ASSERT_TRUE(storage.Initialize(ZKilo(64), 1048576, 65536, 4096, &route));
        const auto empty    = storage.Mark();
        const auto retained = storage.Scalar("immutable");
        storage.Freeze();
        const auto frozen = storage.Mark();
        EXPECT_FALSE(storage.Scalar("rejected").IsDefined());
        storage.Rollback(frozen);
        EXPECT_TRUE(storage.Good());
        EXPECT_EQ(retained.Scalar(), "immutable");
        EXPECT_FALSE(storage.Scalar("still frozen").IsDefined());
        storage.Rollback(empty);
        EXPECT_TRUE(storage.Good());
        EXPECT_EQ(storage.BytesUsed(), 0u);
        EXPECT_EQ(storage.Scalar("reused").Scalar(), "reused");
        EXPECT_EQ(Allocator.Calls, 1u);
    }

    TEST_F(CookYAMLAllocationTest, GrowingTreeReusesItsStableBudgetedBlocksAcrossMigrationStyleRewinds)
    {
        CookPageAllocator native = {};
        ASSERT_TRUE(native.Initialize(ZKilo(256)).Succeeded());
        CookBudgetAllocator budget = {};
        ASSERT_TRUE(budget.Initialize(native, ZKilo(256)).Succeeded());
        Allocator.Parent = &budget;
        CookCodecAllocationRoute route(Allocator);
        ECS::SceneYAML::Storage  storage = {};
        ASSERT_TRUE(storage.Initialize(ZKilo(256), 4096, 1024, 4096, &route));
        const auto empty = storage.Mark();
        size_t     calls = 0, bytes = 0;
        uint64_t   mappings = 0;
        for (size_t iteration = 0; iteration < 8; ++iteration)
        {
            storage.Rollback(empty);
            auto root     = storage.Make(ECS::SceneYAML::NodeType::Sequence);
            auto retained = storage.Scalar("retained across growth");
            ASSERT_TRUE(storage.Append(root, retained));
            for (uint32_t index = 0; index < 500; ++index)
            {
                ASSERT_TRUE(storage.Append(root, storage.UInt(index)));
            }
            EXPECT_EQ(retained.Scalar(), "retained across growth");
            EXPECT_EQ(root.size(), 501u);
            EXPECT_EQ(root[500].Scalar(), "499");
            EXPECT_EQ(storage.ByteCapacity(), ZKilo(256));
            storage.Freeze();
            if (!iteration)
            {
                calls    = Allocator.Calls;
                bytes    = budget.Statistics().LiveBytes;
                mappings = native.AllocationCount();
                EXPECT_GT(calls, 1u);
                EXPECT_LT(bytes, ZKilo(256));
            }
            EXPECT_EQ(Allocator.Calls, calls);
            EXPECT_EQ(budget.Statistics().LiveBytes, bytes);
            EXPECT_EQ(native.AllocationCount(), mappings);
            EXPECT_EQ(route.Error(), VFS::VFSError::OK);
        }
    }

    TEST_F(CookYAMLAllocationTest, RoutedGrowthRefusalIsDistinctFromTheDecodedCapacityLimit)
    {
        for (bool refuse_growth : {false, true})
        {
            SCOPED_TRACE(refuse_growth);
            CookCodecAllocationRoute route(Allocator);
            ECS::SceneYAML::Storage  storage = {};
            ASSERT_TRUE(storage.Initialize(refuse_growth ? ZKilo(256) : ZKilo(32), 4096, 2048, 4096, &route));
            const auto retained = storage.Scalar("retained");
            auto       root     = storage.Make(ECS::SceneYAML::NodeType::Sequence);
            Allocator.FailAt    = refuse_growth ? Allocator.Calls + 1 : 0;
            for (uint32_t index = 0; index < 1000 && storage.Good(); ++index)
            {
                static_cast<void>(storage.Append(root, storage.UInt(index)));
            }
            EXPECT_FALSE(storage.Good());
            EXPECT_EQ(storage.Failure(), refuse_growth ? ECS::SceneDiagnosticCode::AllocationFailed : ECS::SceneDiagnosticCode::LimitExceeded);
            EXPECT_EQ(route.Error(), refuse_growth ? VFS::VFSError::SizeLimitExceeded : VFS::VFSError::OK);
            EXPECT_EQ(retained.Scalar(), "retained");
        }
    }

    TEST_F(CookYAMLAllocationTest, ScannerGrowthAndReallocationFailuresRemainBudgeted)
    {
        // A long quoted scalar forces the upstream scanner's yaml_realloc path.
        char source[8192] = {};
        source[0]         = '"';
        std::fill_n(source + 1, sizeof(source) - 3, 'a');
        source[sizeof(source) - 2]      = '"';
        ECS::SceneYAML::Storage storage = {};
        ASSERT_TRUE(storage.Initialize(ZMega(1)));
        const auto baseline = storage.Mark();
        auto       output   = storage.Destination();
        {
            CookCodecAllocationRoute                 route(Allocator);
            Core::Serialization::YAMLAllocationScope scope(route);
            // Scalar exceeds the scene scalar cap, but libyaml must still clean
            // up its grown scanner string and the returned event on rejection.
            EXPECT_FALSE(Read(output, source));
            EXPECT_EQ(route.Error(), VFS::VFSError::OK);
        }
        const auto total = Allocator.Calls;
        for (size_t failure = 1; failure <= total; ++failure)
        {
            SCOPED_TRACE(failure);
            Allocator.Calls  = 0;
            Allocator.FailAt = failure;
            CookCodecAllocationRoute                 route(Allocator);
            Core::Serialization::YAMLAllocationScope scope(route);
            EXPECT_FALSE(Read(output, source));
            EXPECT_EQ(route.Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(storage.BytesUsed(), baseline.Arena.CurrentOffset);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        }
    }

    TEST_F(CookYAMLAllocationTest, DuplicateStringsAndDocumentOwnersUseTheSameCheckedRoute)
    {
        // libyaml's document API duplicates its tags through yaml_strdup,
        // separately from parser/scanner yaml_malloc allocations.
        size_t total = 0;
        for (size_t failure = 0; failure <= total; ++failure)
        {
            Allocator.Calls  = 0;
            Allocator.FailAt = failure;
            CookCodecAllocationRoute                 route(Allocator);
            Core::Serialization::YAMLAllocationScope scope(route);
            yaml_document_t                          document    = {};
            const bool                               initialized = yaml_document_initialize(&document, nullptr, nullptr, nullptr, 1, 1) != 0;
            const int                                scalar      = initialized ? yaml_document_add_scalar(&document, nullptr, reinterpret_cast<const yaml_char_t*>("value"), -1, YAML_PLAIN_SCALAR_STYLE) : 0;
            if (!failure)
            {
                ASSERT_TRUE(initialized);
                ASSERT_GT(scalar, 0);
                total = Allocator.Calls;
                EXPECT_EQ(route.Error(), VFS::VFSError::OK);
            }
            else
            {
                EXPECT_FALSE(initialized && scalar);
                EXPECT_EQ(route.Error(), VFS::VFSError::SizeLimitExceeded);
            }
            if (initialized)
            {
                yaml_document_delete(&document);
            }
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        }
    }

    TEST_F(CookYAMLAllocationTest, NestedScopesDispatchExistingBlocksToTheirOriginalOwner)
    {
        CookCodecAllocationRoute                 outer(Allocator), inner(Allocator);
        Core::Serialization::YAMLAllocationScope outer_scope(outer);
        auto*                                    first = static_cast<char*>(ZEngineYAMLAllocate(8));
        ASSERT_NE(first, nullptr);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(first) % alignof(std::max_align_t), 0u);
        ASSERT_EQ(Helpers::secure_memcpy(first, 8, "payload", 8), Helpers::MEMORY_OP_SUCCESS);
        {
            Core::Serialization::YAMLAllocationScope inner_scope(inner);
            Allocator.FailAt = Allocator.Calls + 1;
            EXPECT_EQ(ZEngineYAMLReallocate(first, 128), nullptr);
            EXPECT_EQ(outer.Error(), VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(inner.Error(), VFS::VFSError::OK);
            EXPECT_STREQ(first, "payload");
            ZEngineYAMLFree(first);
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
            Allocator.FailAt = 0;
            auto* second     = ZEngineYAMLDuplicate("inner");
            ASSERT_NE(second, nullptr);
            EXPECT_STREQ(second, "inner");
            ZEngineYAMLFree(second);
        }
        const auto calls = Allocator.Calls;
        EXPECT_EQ(ZEngineYAMLAllocate(1), nullptr); // Outer refusal is sticky.
        EXPECT_EQ(Allocator.Calls, calls);
    }

    TEST_F(CookYAMLAllocationTest, EveryEmitterAllocationFailureReleasesEventsAndBuffers)
    {
        const auto emit = [] {
            yaml_emitter_t emitter = {};
            if (!yaml_emitter_initialize(&emitter))
            {
                return false;
            }
            struct Lease
            {
                yaml_emitter_t& Emitter;
                ~Lease()
                {
                    yaml_emitter_delete(&Emitter);
                }
            } lease{emitter};
            unsigned char output[4096] = {};
            size_t        written      = 0;
            yaml_emitter_set_output_string(&emitter, output, sizeof(output), &written);
            yaml_event_t event = {};
            // yaml_emitter_emit takes ownership even when emitting fails.
            return yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING) && yaml_emitter_emit(&emitter, &event) && yaml_document_start_event_initialize(&event, nullptr, nullptr, nullptr, 1) && yaml_emitter_emit(&emitter, &event) && yaml_scalar_event_initialize(&event, nullptr, nullptr, reinterpret_cast<const yaml_char_t*>("value"), -1, 1, 1, YAML_PLAIN_SCALAR_STYLE) && yaml_emitter_emit(&emitter, &event) && yaml_document_end_event_initialize(&event, 1) && yaml_emitter_emit(&emitter, &event) && yaml_stream_end_event_initialize(&event) &&
                   yaml_emitter_emit(&emitter, &event) && written > 0;
        };
        size_t total = 0;
        for (size_t failure = 0; failure <= total; ++failure)
        {
            SCOPED_TRACE(failure);
            Allocator.Calls  = 0;
            Allocator.FailAt = failure;
            CookCodecAllocationRoute                 route(Allocator);
            Core::Serialization::YAMLAllocationScope scope(route);
            if (!failure)
            {
                EXPECT_TRUE(emit());
                total = Allocator.Calls;
                EXPECT_GT(total, 0u);
            }
            else
            {
                EXPECT_FALSE(emit());
                EXPECT_EQ(route.Error(), VFS::VFSError::SizeLimitExceeded);
            }
            EXPECT_EQ(Scratch.OutstandingAllocations(), 0u);
        }
    }

    TEST_F(CookYAMLAllocationTest, DefaultHeapBlocksAreNotRedirectedByAnInstalledRoute)
    {
        auto* text = ZEngineYAMLDuplicate("default");
        ASSERT_NE(text, nullptr);
        {
            CookCodecAllocationRoute                 route(Allocator);
            Core::Serialization::YAMLAllocationScope scope(route);
            auto*                                    grown = static_cast<char*>(ZEngineYAMLReallocate(text, 64));
            ASSERT_NE(grown, nullptr);
            EXPECT_STREQ(grown, "default");
            ZEngineYAMLFree(grown);
        }
        EXPECT_EQ(Allocator.Calls, 0u);
    }

    TEST_F(CookYAMLAllocationTest, ZeroSizeNullAndOverflowRequestsAreSafe)
    {
        CookCodecAllocationRoute                 route(Allocator);
        Core::Serialization::YAMLAllocationScope scope(route);
        auto*                                    bytes = ZEngineYAMLAllocate(0);
        ASSERT_NE(bytes, nullptr);
        bytes = ZEngineYAMLReallocate(bytes, 0);
        ASSERT_NE(bytes, nullptr); // libyaml treats zero-size allocations as one.
        ZEngineYAMLFree(bytes);
        ZEngineYAMLFree(nullptr);
        EXPECT_EQ(ZEngineYAMLDuplicate(nullptr), nullptr);
        const auto calls = Allocator.Calls;
        EXPECT_EQ(ZEngineYAMLAllocate(std::numeric_limits<size_t>::max()), nullptr);
        EXPECT_EQ(Allocator.Calls, calls);
    }

    TEST_F(CookYAMLAllocationTest, RoutesAreThreadLocalAndDefaultAllocationDoesNotConsumeCookBudget)
    {
        CookCodecAllocationRoute                 route(Allocator);
        Core::Serialization::YAMLAllocationScope scope(route);
        std::thread                              other([] {
            auto* text = ZEngineYAMLDuplicate("other thread");
            EXPECT_NE(text, nullptr);
            ZEngineYAMLFree(text);
        });
        other.join();
        EXPECT_EQ(Allocator.Calls, 0u);
        auto* text = ZEngineYAMLDuplicate("cook thread");
        ASSERT_NE(text, nullptr);
        EXPECT_EQ(Allocator.Calls, 1u);
        ZEngineYAMLFree(text);
    }
} // namespace ZEngine::Cook
