#include <ZEngine/Core/Containers/HeapSort.h>
#include <ZEngine/Core/Serialization/YAMLAllocation.h>
#include <ZEngine/ECS/SceneYAMLReader.h>
#include <ZEngine/ECS/SceneYamlValue.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <limits>
#include "SceneWorkingMemoryTestSupport.h"

using namespace ZEngine::ECS;

using YAMLFixedRoute = ZEngine::Tests::SceneTestAllocationRoute;

class SceneYAMLTest : public testing::Test
{
protected:
    SceneYAML::Storage Storage = {};
    void               SetUp() override
    {
        ASSERT_TRUE(Storage.Initialize());
    }
};

TEST_F(SceneYAMLTest, FixedParserStateUsesTheScopedBudgetAndRefusesOneByteShortBeforeLibyamlAllocation)
{
    auto           output = Storage.Scalar("preserved");
    const auto     used   = Storage.BytesUsed();
    YAMLFixedRoute probe  = {};
    {
        ZEngine::Core::Serialization::YAMLAllocationScope scope(probe);
        SceneDiagnostics                                  diagnostics = {};
        EXPECT_FALSE(ReadSceneYAML("{value: 1}", {}, &output, &diagnostics));
        EXPECT_TRUE(diagnostics.HasError(SceneDiagnosticCode::LimitExceeded));
    }
    ASSERT_GT(probe.Required, 0u);
    EXPECT_EQ(probe.Attempts, 0u);
    EXPECT_EQ(probe.Live, 0u);
    EXPECT_EQ(output.Scalar(), "preserved");
    EXPECT_EQ(Storage.BytesUsed(), used);
    for (size_t limit : {probe.Required - 1, probe.Required})
    {
        YAMLFixedRoute route = {};
        route.Limit          = limit;
        {
            ZEngine::Core::Serialization::YAMLAllocationScope scope(route);
            SceneDiagnostics                                  diagnostics = {};
            EXPECT_EQ(ReadSceneYAML("{value: 1}", {}, &output, &diagnostics), limit == probe.Required);
            if (limit < probe.Required)
            {
                EXPECT_EQ(route.Attempts, 0u);
                EXPECT_EQ(output.Scalar(), "preserved");
                EXPECT_EQ(Storage.BytesUsed(), used);
            }
            else
            {
                EXPECT_GT(route.Attempts, 0u);
                EXPECT_EQ(route.Peak, probe.Required);
                EXPECT_EQ(output["value"].Scalar(), "1");
            }
        }
        EXPECT_EQ(route.Blocks, 0u);
        EXPECT_EQ(route.Live, 0u);
    }
}

TEST_F(SceneYAMLTest, NestedAllocationScopesRestoreTheCreatingThreadsFixedWorkingBudget)
{
    YAMLFixedRoute outer = {}, inner = {};
    EXPECT_EQ(ZEngine::Core::Serialization::YAMLWorkingMemoryBudget(), nullptr);
    {
        ZEngine::Core::Serialization::YAMLAllocationScope scope(outer);
        EXPECT_EQ(ZEngine::Core::Serialization::YAMLWorkingMemoryBudget(), outer.WorkingMemoryBudget());
        {
            ZEngine::Core::Serialization::YAMLAllocationScope nested(inner);
            EXPECT_EQ(ZEngine::Core::Serialization::YAMLWorkingMemoryBudget(), inner.WorkingMemoryBudget());
        }
        EXPECT_EQ(ZEngine::Core::Serialization::YAMLWorkingMemoryBudget(), outer.WorkingMemoryBudget());
    }
    EXPECT_EQ(ZEngine::Core::Serialization::YAMLWorkingMemoryBudget(), nullptr);
}

TEST_F(SceneYAMLTest, MissingFieldsAndWrongTypesAreSafeQueries)
{
    const auto scalar  = Storage.Scalar("scalar");
    const auto missing = scalar["missing"];
    EXPECT_FALSE(missing.IsDefined());
    EXPECT_FALSE(missing.IsMap());
    EXPECT_FALSE(missing.IsSequence());
    EXPECT_TRUE(missing.Scalar().empty());
    EXPECT_TRUE(missing.Mapping().empty());
    EXPECT_TRUE(missing.Sequence().empty());
    EXPECT_FALSE(missing[size_t{999}].IsDefined());
}

TEST_F(SceneYAMLTest, MalformedAndForbiddenDocumentsPreserveOutput)
{
    auto       output = Storage.Scalar("preserved");
    const auto used   = Storage.BytesUsed();
    for (const std::string_view source : {"", "[", "a: [1,", "a:\n b: 1\n c", "a: 1\na: 2", "a: &anchor 1", "a: *missing", "a: !custom value", "a: !!str value", "[? [a,b]: c]", "---\na: 1\n---\nb: 2", "a: \"\\uD800\"", "a: \"\\x00\""})
    {
        SCOPED_TRACE(source);
        SceneDiagnostics diagnostics = {};
        EXPECT_FALSE(ReadSceneYAML(source, {}, &output, &diagnostics));
        EXPECT_EQ(output.Scalar(), "preserved");
        EXPECT_TRUE(diagnostics.HasErrors());
        EXPECT_TRUE(Storage.Good());
        EXPECT_EQ(Storage.BytesUsed(), used);
    }
    const std::string bad_utf8 = {'a', ':', ' ', char(0xff)};
    EXPECT_FALSE(ReadSceneYAML(bad_utf8, {}, &output, nullptr));
    EXPECT_EQ(output.Scalar(), "preserved");
}

TEST_F(SceneYAMLTest, CollectionDepthNodeAndScalarLimitsAreCheckedBeforePublication)
{
    auto             output       = Storage.Scalar("preserved");
    SceneDiagnostics diagnostics  = {};
    const auto       read_limited = [&](std::string_view source, const SceneDocumentLimits& limits) {
        diagnostics.Clear();
        EXPECT_FALSE(ReadSceneYAML(source, limits, &output, &diagnostics));
        EXPECT_TRUE(diagnostics.HasError(SceneDiagnosticCode::LimitExceeded));
        EXPECT_FALSE(diagnostics.HasError(SceneDiagnosticCode::InvalidData));
        EXPECT_TRUE(Storage.Good());
        EXPECT_EQ(Storage.Failure(), SceneDiagnosticCode::None);
    };
    auto limits               = SceneDocumentLimits{};
    limits.MaxCollectionItems = 1;
    read_limited("[1, 2]", limits);
    limits                = {};
    limits.MaxScalarBytes = 1;
    read_limited("ab", limits);
    limits          = {};
    limits.MaxNodes = 2;
    read_limited("a: 1", limits);
    limits          = {};
    limits.MaxDepth = 2;
    read_limited("[[[1]]]", limits);
    limits              = {};
    limits.MaxFileBytes = 2;
    read_limited("a: 1", limits);
    EXPECT_EQ(output.Scalar(), "preserved");
}

TEST_F(SceneYAMLTest, TinyMapUniquenessNeedsNoScratchCapacityAndStillRejectsDuplicateText)
{
    for (uint32_t count = 0; count <= 2; ++count)
    {
        for (const bool duplicate : {false, true})
        {
            SCOPED_TRACE(count);
            SCOPED_TRACE(duplicate);
            alignas(std::max_align_t) uint8_t memory[ZKilo(8)] = {};
            SceneYAML::Storage                bounded          = {};
            ASSERT_TRUE(bounded.InitializeBorrowed(memory, sizeof(memory), 64, 64));
            auto map = bounded.Make(SceneYAML::NodeType::Map);
            for (uint32_t i = 0; i < count; ++i)
            {
                // Distinct scalar nodes may contain the same key text.
                const auto key   = bounded.Scalar(i == 0 || duplicate ? "a" : "b");
                const auto value = bounded.Scalar("value");
                ASSERT_TRUE(bounded.AppendField(map, key, value));
            }
            const auto available = bounded.ByteCapacity() - bounded.BytesUsed();
            if (available)
            {
                ASSERT_NE(bounded.Allocator()->AllocateNoZero(available, 1), nullptr);
            }
            const auto used = bounded.BytesUsed();
            ASSERT_EQ(used, bounded.ByteCapacity());
            const bool     unique = count < 2 || !duplicate;
            YAMLFixedRoute route  = {};
            {
                ZEngine::Core::Serialization::YAMLAllocationScope scope(route);
                EXPECT_EQ(bounded.UniqueKeys(map), unique);
            }
            EXPECT_EQ(route.Required, 0u);
            EXPECT_EQ(route.Attempts, 0u);
            EXPECT_EQ(route.Live, 0u);
            EXPECT_EQ(bounded.Good(), unique);
            EXPECT_EQ(bounded.Failure(), unique ? SceneDiagnosticCode::None : SceneDiagnosticCode::InvalidData);
            EXPECT_EQ(bounded.BytesUsed(), used);
        }
    }
}

TEST_F(SceneYAMLTest, LargeMapUniquenessHonorsExactSortingBudgetWithoutChangingInsertionOrder)
{
    constexpr size_t required = sizeof(ZEngine::Core::Containers::HeapSortState<uint32_t>) + sizeof(ZEngine::Core::Containers::Array<uint32_t>) + sizeof(ZEngine::Core::Memory::ArenaTemp) + sizeof(ZEngine::Core::Memory::WorkingMemoryCharge);
    for (const size_t count : {3u, 64u, 257u})
    {
        for (const bool duplicate : {false, true})
        {
            for (const size_t shortfall : {1u, 0u})
            {
                SCOPED_TRACE(count);
                SCOPED_TRACE(duplicate);
                SCOPED_TRACE(shortfall);
                alignas(std::max_align_t) uint8_t memory[ZKilo(256)] = {};
                SceneYAML::Storage                bounded            = {};
                ASSERT_TRUE(bounded.InitializeBorrowed(memory, sizeof(memory)));
                auto map = bounded.Make(SceneYAML::NodeType::Map);
                for (size_t i = 0; i < count; ++i)
                {
                    char       text[16] = {};
                    const auto encoded  = std::to_chars(text, text + sizeof(text), duplicate && i + 1 == count ? count : count - i);
                    ASSERT_EQ(encoded.ec, std::errc{});
                    const auto key = bounded.Scalar({text, static_cast<size_t>(encoded.ptr - text)});
                    ASSERT_TRUE(bounded.AppendField(map, key, bounded.UInt(static_cast<uint32_t>(i))));
                }
                const auto     used  = bounded.BytesUsed();
                YAMLFixedRoute route = {};
                route.Limit          = required - shortfall;
                {
                    ZEngine::Core::Serialization::YAMLAllocationScope scope(route);
                    EXPECT_EQ(bounded.UniqueKeys(map), !shortfall && !duplicate);
                }
                EXPECT_EQ(route.Required, required);
                EXPECT_EQ(route.Peak, shortfall ? 0 : required);
                EXPECT_EQ(route.Live, 0u);
                EXPECT_EQ(route.Attempts, 0u);
                EXPECT_EQ(bounded.BytesUsed(), used);
                EXPECT_EQ(bounded.Failure(), shortfall ? SceneDiagnosticCode::LimitExceeded : duplicate ? SceneDiagnosticCode::InvalidData : SceneDiagnosticCode::None);
                size_t index = 0;
                for (const auto entry : map.Mapping())
                {
                    uint32_t value = UINT32_MAX;
                    EXPECT_TRUE(ReadSceneYAMLUInt(entry.Value, value));
                    EXPECT_EQ(value, index++);
                }
                EXPECT_EQ(index, count);
            }
        }
    }
}

TEST_F(SceneYAMLTest, RoutedMapSortingUsesItsActualOwnerInsteadOfAnUnrelatedScope)
{
    for (const bool borrowed : {false, true})
    {
        YAMLFixedRoute owner = {}, unrelated = {};
        owner.Limit                                        = SIZE_MAX;
        alignas(std::max_align_t) uint8_t memory[ZKilo(8)] = {};
        {
            SceneYAML::Storage bounded = {};
            ASSERT_TRUE(borrowed ? bounded.InitializeGrowingBorrowed(memory, sizeof(memory), sizeof(memory), owner) : bounded.Initialize(ZKilo(8), 64, 64, 64, &owner));
            auto map = bounded.Make(SceneYAML::NodeType::Map);
            for (const auto key : {"c", "a", "b"})
            {
                ASSERT_TRUE(bounded.Set(map, key, bounded.Scalar("value")));
            }
            const auto attempts = owner.Attempts;
            {
                ZEngine::Core::Serialization::YAMLAllocationScope scope(unrelated);
                EXPECT_EQ(bounded.WorkingMemoryBudget(), owner.WorkingMemoryBudget());
                EXPECT_TRUE(bounded.UniqueKeys(map));
            }
            EXPECT_GT(owner.Peak, 0u);
            EXPECT_EQ(owner.Live, 0u);
            EXPECT_EQ(owner.Attempts, attempts);
            EXPECT_EQ(unrelated.Required, 0u);
            EXPECT_EQ(unrelated.Attempts, 0u);
        }
        EXPECT_EQ(owner.Blocks, 0u);
    }
}

TEST_F(SceneYAMLTest, FailedMapScratchReservationDrainsTheSortingCharge)
{
    auto map = Storage.Make(SceneYAML::NodeType::Map);
    for (const auto key : {"c", "a", "b"})
    {
        ASSERT_TRUE(Storage.Set(map, key, Storage.Scalar("value")));
    }
    const auto available = Storage.ByteCapacity() - Storage.BytesUsed();
    ASSERT_NE(Storage.Allocator()->AllocateNoZero(available, 1), nullptr);
    const auto     used  = Storage.BytesUsed();
    YAMLFixedRoute route = {};
    route.Limit          = SIZE_MAX;
    {
        ZEngine::Core::Serialization::YAMLAllocationScope scope(route);
        EXPECT_FALSE(Storage.UniqueKeys(map));
    }
    EXPECT_GT(route.Peak, 0u);
    EXPECT_EQ(route.Live, 0u);
    EXPECT_EQ(route.Attempts, 0u);
    EXPECT_EQ(Storage.BytesUsed(), used);
    EXPECT_EQ(Storage.Failure(), SceneDiagnosticCode::LimitExceeded);
}

TEST_F(SceneYAMLTest, DecodedCapacityRefusalIsReportedBeforeReaderRollsBack)
{
    alignas(std::max_align_t) uint8_t memory[128] = {};
    SceneYAML::Storage                bounded     = {};
    ASSERT_TRUE(bounded.InitializeBorrowed(memory, sizeof(memory)));
    auto             output      = bounded.Destination();
    SceneDiagnostics diagnostics = {};
    EXPECT_FALSE(ReadSceneYAML("a: [1, 2, 3]", {}, &output, &diagnostics));
    EXPECT_TRUE(diagnostics.HasError(SceneDiagnosticCode::LimitExceeded));
    EXPECT_TRUE(bounded.Good());
    EXPECT_EQ(bounded.Failure(), SceneDiagnosticCode::None);
    EXPECT_EQ(bounded.BytesUsed(), 0u);
    EXPECT_FALSE(output.IsDefined());
}

TEST_F(SceneYAMLTest, SyntaxAndDuplicateKeysAreNotReportedAsBudgetRefusals)
{
    auto output = Storage.Destination();
    for (const std::string_view source : {"[", "a: 1\na: 2", "a: &anchor 1", "a: !tag 1"})
    {
        SceneDiagnostics diagnostics = {};
        EXPECT_FALSE(ReadSceneYAML(source, {}, &output, &diagnostics));
        EXPECT_TRUE(diagnostics.HasError(SceneDiagnosticCode::InvalidData));
        EXPECT_FALSE(diagnostics.HasError(SceneDiagnosticCode::LimitExceeded));
        EXPECT_EQ(Storage.Failure(), SceneDiagnosticCode::None);
    }
}

TEST(SceneDiagnosticsTest, StructuredCategoriesSurviveEntryOverflowAndClearResetsThem)
{
    SceneDiagnostics diagnostics = {};
    for (uint32_t i = 0; i < SceneDiagnostics::MAX_ENTRIES; ++i)
    {
        diagnostics.Add(SceneDiagnosticSeverity::Info, "info");
    }
    diagnostics.Error("limit", SceneDiagnosticCode::LimitExceeded);
    diagnostics.Error("allocation", SceneDiagnosticCode::AllocationFailed);
    EXPECT_EQ(diagnostics.Dropped, 2u);
    EXPECT_EQ(diagnostics.ErrorCount, 2u);
    EXPECT_TRUE(diagnostics.HasError(SceneDiagnosticCode::LimitExceeded));
    EXPECT_TRUE(diagnostics.HasError(SceneDiagnosticCode::AllocationFailed));
    EXPECT_FALSE(diagnostics.HasError(SceneDiagnosticCode::InvalidData));
    diagnostics.Clear();
    EXPECT_FALSE(diagnostics.HasErrors());
    EXPECT_FALSE(diagnostics.HasError(SceneDiagnosticCode::LimitExceeded));
    diagnostics.Error("invalid");
    EXPECT_EQ(diagnostics.Entries[0].Code, SceneDiagnosticCode::InvalidData);
    EXPECT_TRUE(diagnostics.HasError(SceneDiagnosticCode::InvalidData));
}

TEST_F(SceneYAMLTest, QuotedNullAndNullLikeStringsRetainTheirMeaning)
{
    auto root = Storage.Destination();
    ASSERT_TRUE(ReadSceneYAML("empty: null\nquoted: \"null\"\nliteral: '~'\nunicode: \"日本語\"\n", {}, &root, nullptr));
    const auto& view = root;
    EXPECT_TRUE(view["empty"].IsNull());
    EXPECT_TRUE(view["quoted"].IsScalar());
    EXPECT_EQ(view["quoted"].Scalar(), "null");
    EXPECT_EQ(view["literal"].Scalar(), "~");
    SceneYAML::Emitter emitter(4096);
    emitter << root;
    std::string text;
    ASSERT_TRUE(emitter.Output(&text));
    auto decoded = Storage.Destination();
    ASSERT_TRUE(ReadSceneYAML(text, {}, &decoded, nullptr));
    EXPECT_EQ(decoded["unicode"].Scalar(), "日本語");
    EXPECT_TRUE(decoded["empty"].IsNull());
    EXPECT_EQ(decoded["quoted"].Scalar(), "null");
}

TEST_F(SceneYAMLTest, CheckedNumbersRejectOverflowAndTrailingCharactersWithoutChangingDestination)
{
    for (const char* text : {"", "-1", "+1", "4294967296", "999999999999999999999", "1x", " 1", "1 "})
    {
        uint32_t value = 42;
        EXPECT_FALSE(ReadSceneYAMLUInt(Storage.Scalar(text), value)) << text;
        EXPECT_EQ(value, 42u);
    }
    uint32_t integer = 0;
    ASSERT_TRUE(ReadSceneYAMLUInt(Storage.Scalar("4294967295"), integer));
    EXPECT_EQ(integer, UINT32_MAX);
    for (const char* text : {"", "++1", "+-1", "1e9999", "nan", ".inf", "inf", "-inf", "1x", " 1", "1 "})
    {
        float value = 42.f;
        EXPECT_FALSE(ReadSceneYAMLFloat(Storage.Scalar(text), value)) << text;
        EXPECT_EQ(value, 42.f);
    }
    float real = 0.f;
    ASSERT_TRUE(ReadSceneYAMLFloat(Storage.Scalar("+1.25e2"), real));
    EXPECT_EQ(real, 125.f);
}

TEST_F(SceneYAMLTest, BoundedWriterPreservesOutputOnCapacityFailure)
{
    SceneYAML::Emitter emitter(16);
    emitter << Storage.Scalar(std::string(1000, 'a'));
    std::string output = "preserved";
    EXPECT_FALSE(emitter.Output(&output));
    EXPECT_FALSE(emitter.good());
    EXPECT_EQ(output, "preserved");
    EXPECT_FALSE(emitter.Output(&output));
}

TEST_F(SceneYAMLTest, WriterAcceptsAnEmptyBorrowedScalarWithoutANullNativePointer)
{
    SceneYAML::Emitter emitter(4096);
    emitter << std::string_view{};
    std::string output;
    ASSERT_TRUE(emitter.Output(&output));
    auto decoded = Storage.Destination();
    ASSERT_TRUE(ReadSceneYAML(output, {}, &decoded, nullptr));
    EXPECT_TRUE(decoded.IsScalar());
    EXPECT_TRUE(decoded.Scalar().empty());
}

TEST_F(SceneYAMLTest, WriterRejectsUndefinedNonfiniteAndIncompleteValues)
{
    for (const auto& node : {SceneYAML::Node{}, Storage.Float(std::numeric_limits<float>::infinity())})
    {
        SceneYAML::Emitter emitter(4096);
        emitter << node;
        std::string output = "preserved";
        EXPECT_FALSE(emitter.Output(&output));
        EXPECT_EQ(output, "preserved");
    }
    SceneYAML::Emitter emitter(4096);
    emitter << SceneYAML::BeginMap << "missing value";
    std::string output = "preserved";
    EXPECT_FALSE(emitter.Output(&output));
    EXPECT_EQ(output, "preserved");
}

TEST_F(SceneYAMLTest, HandleReassignmentDoesNotRewriteAnotherPayload)
{
    auto root  = Storage.Make(SceneYAML::NodeType::Map);
    auto child = Storage.Make(SceneYAML::NodeType::Map);
    ASSERT_TRUE(Storage.Set(root, "child", child));
    ASSERT_TRUE(Storage.Set(child, "value", Storage.Scalar("original")));
    auto original = root["child"];
    ASSERT_TRUE(Storage.Set(root, "child", Storage.Scalar("replacement")));
    EXPECT_EQ(original["value"].Scalar(), "original");
    EXPECT_EQ(root["child"].Scalar(), "replacement");
}

TEST_F(SceneYAMLTest, CloneOwnsEveryScalarAndKeyAfterSourceDestruction)
{
    auto copy = Storage.Destination();
    {
        SceneYAML::Storage source = {};
        ASSERT_TRUE(source.Initialize(ZMega(1)));
        const auto root = source.Make(SceneYAML::NodeType::Map);
        const auto list = source.Make(SceneYAML::NodeType::Sequence, SceneYAML::EmitterStyle::Flow);
        ASSERT_TRUE(source.Append(list, source.Scalar("owned text")));
        ASSERT_TRUE(source.Append(list, source.Make(SceneYAML::NodeType::Null)));
        ASSERT_TRUE(source.Set(root, "list", list));
        ASSERT_TRUE(source.Set(root, "number", source.UInt(123)));
        source.Freeze();
        copy = Storage.Clone(root);
        ASSERT_TRUE(copy.IsMap());
        EXPECT_NE(copy.Owner(), root.Owner());
    }
    EXPECT_EQ(copy["list"].Style(), SceneYAML::EmitterStyle::Flow);
    EXPECT_EQ(copy["list"][size_t{0}].Scalar(), "owned text");
    EXPECT_TRUE(copy["list"][size_t{1}].IsNull());
    EXPECT_EQ(copy["number"].Scalar(), "123");
    EXPECT_TRUE(copy["number"].Plain());
}

TEST_F(SceneYAMLTest, CloneRejectsDuplicateKeysDepthAndMemoryOverflow)
{
    for (uint32_t mode = 0; mode < 4; ++mode)
    {
        SceneYAML::Storage source = {}, destination = {};
        ASSERT_TRUE(source.Initialize(ZMega(1)));
        ASSERT_TRUE(destination.Initialize(mode == 3 ? 32 : ZMega(1)));
        auto root = source.Make(SceneYAML::NodeType::Map);
        ASSERT_TRUE(source.Set(root, "value", source.Scalar("text")));
        if (mode == 0)
        {
            root = {};
        }
        if (mode == 1)
        {
            ASSERT_TRUE(source.AppendField(root, source.Scalar("value"), source.UInt(7)));
        }
        if (mode == 2)
        {
            const auto nested = source.Make(SceneYAML::NodeType::Map);
            ASSERT_TRUE(source.Set(nested, "nested", root));
            root = nested;
        }
        EXPECT_FALSE(destination.Clone(root, mode == 2 ? 1 : 32).IsDefined());
        EXPECT_FALSE(destination.Good());
    }
}

TEST_F(SceneYAMLTest, IterativeCloneUsesOneExactReservationAndRefusesBeforeMutation)
{
    auto source = Storage.Scalar("owned leaf");
    for (uint32_t i = 0; i < 32; ++i)
    {
        const auto parent = Storage.Make(i % 2 ? SceneYAML::NodeType::Map : SceneYAML::NodeType::Sequence, SceneYAML::EmitterStyle::Flow);
        ASSERT_TRUE(i % 2 ? Storage.AppendField(parent, Storage.Scalar("key"), source) : Storage.Append(parent, source));
        source = parent;
    }
    YAMLFixedRoute     route = {}, unrelated = {};
    SceneYAML::Storage destination = {};
    ASSERT_TRUE(destination.Initialize(ZMega(2), 1048576, 65536, 4096, &route));
    const auto preserved = destination.Scalar("preserved");
    const auto mark      = destination.Mark();
    const auto attempts  = route.Attempts;
    EXPECT_FALSE(destination.Clone(source).IsDefined());
    EXPECT_EQ(destination.Failure(), SceneDiagnosticCode::LimitExceeded);
    const auto required = route.Required;
    ASSERT_GT(required, 0u);
    EXPECT_EQ(destination.BytesUsed(), mark.Arena.CurrentOffset);
    route.WatchBytes = required;
    for (const bool exact : {false, true})
    {
        destination.Rollback(mark);
        route.Limit = required - (exact ? 0 : 1);
        ZEngine::Core::Serialization::YAMLAllocationScope scope(unrelated);
        auto                                              copy = destination.Clone(source);
        EXPECT_EQ(copy.IsDefined(), exact);
        EXPECT_EQ(destination.Failure(), exact ? SceneDiagnosticCode::None : SceneDiagnosticCode::LimitExceeded);
        EXPECT_EQ(route.Live, 0u);
        EXPECT_EQ(route.Attempts, attempts);
        EXPECT_EQ(unrelated.Required, 0u);
        EXPECT_EQ(preserved.Scalar(), "preserved");
        if (!exact)
        {
            EXPECT_EQ(destination.BytesUsed(), mark.Arena.CurrentOffset);
        }
        else
        {
            for (uint32_t i = 0; i < 32; ++i)
            {
                EXPECT_EQ(copy.Style(), SceneYAML::EmitterStyle::Flow);
                copy = copy.IsMap() ? copy.Mapping().front().Value : copy.Sequence().front();
            }
            EXPECT_EQ(copy.Scalar(), "owned leaf");
        }
    }
    // No additional reservation for rescanning any completed child subtree.
    EXPECT_EQ(route.WatchCalls, 2u);
    EXPECT_EQ(route.WatchReleases, 1u);
    EXPECT_EQ(route.Peak, required);
}

TEST_F(SceneYAMLTest, IterativeClonePreservesDepthLeafAndExpandedNodeBoundaries)
{
    auto source = Storage.Make(SceneYAML::NodeType::Null);
    for (uint32_t i = 0; i < 32; ++i)
    {
        const auto parent = Storage.Make(SceneYAML::NodeType::Sequence);
        ASSERT_TRUE(Storage.Append(parent, source));
        source = parent;
    }
    for (const uint32_t depth : {31u, 32u})
    {
        SceneYAML::Storage destination = {};
        ASSERT_TRUE(destination.Initialize(ZMega(2)));
        EXPECT_EQ(destination.Clone(source, depth).IsDefined(), depth == 32);
        EXPECT_EQ(destination.Failure(), depth == 32 ? SceneDiagnosticCode::None : SceneDiagnosticCode::LimitExceeded);
    }
    const auto repeated = Storage.Make(SceneYAML::NodeType::Sequence);
    const auto leaf     = Storage.Scalar("shared");
    for (uint32_t i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(Storage.Append(repeated, leaf));
    }
    for (const uint32_t nodes : {5u, 6u})
    {
        SceneYAML::Storage destination = {};
        ASSERT_TRUE(destination.Initialize(ZMega(2), nodes));
        const auto copy = destination.Clone(repeated);
        EXPECT_EQ(copy.IsDefined(), nodes == 6);
        EXPECT_EQ(destination.Failure(), nodes == 6 ? SceneDiagnosticCode::None : SceneDiagnosticCode::LimitExceeded);
        if (copy.IsDefined())
        {
            EXPECT_EQ(copy.size(), 5u);
        }
    }
}

TEST_F(SceneYAMLTest, IterativeCloneKeepsSameOwnerIndicesStableAcrossWideRecordAndLinkGrowth)
{
    const auto source = Storage.Make(SceneYAML::NodeType::Sequence);
    const auto leaf   = Storage.Scalar("shared source");
    for (uint32_t i = 0; i < 65536; ++i)
    {
        ASSERT_TRUE(Storage.Append(source, leaf));
    }
    const auto copy = Storage.Clone(source);
    ASSERT_TRUE(copy.IsSequence());
    EXPECT_EQ(copy.size(), 65536u);
    EXPECT_EQ(source.size(), 65536u);
    size_t visited = 0;
    for (const auto item : copy.Sequence())
    {
        EXPECT_EQ(item.Scalar(), "shared source");
        EXPECT_NE(item.Scalar().data(), leaf.Scalar().data());
        ++visited;
    }
    EXPECT_EQ(visited, 65536u);
    EXPECT_EQ(leaf.Scalar(), "shared source");
}

TEST_F(SceneYAMLTest, ScalarNullAndEmptyCollectionClonesNeedNoParentScratchReservation)
{
    const auto         scalar      = Storage.Scalar("owned");
    const auto         null        = Storage.Make(SceneYAML::NodeType::Null);
    YAMLFixedRoute     route       = {};
    SceneYAML::Storage destination = {};
    ASSERT_TRUE(destination.Initialize(ZMega(2), 1048576, 65536, 4096, &route));
    EXPECT_EQ(destination.Clone(scalar).Scalar(), "owned");
    EXPECT_TRUE(destination.Clone(null).IsNull());
    for (const auto type : {SceneYAML::NodeType::Map, SceneYAML::NodeType::Sequence})
    {
        const auto copy = destination.Clone(Storage.Make(type, SceneYAML::EmitterStyle::Flow));
        EXPECT_EQ(copy.Type(), type);
        EXPECT_EQ(copy.size(), 0u);
        EXPECT_EQ(copy.Style(), SceneYAML::EmitterStyle::Flow);
    }
    EXPECT_EQ(route.Required, 0u);
    EXPECT_EQ(route.Live, 0u);
}

TEST_F(SceneYAMLTest, IndicesAndTextRemainStableAcrossRecordAndLinkGrowth)
{
    const auto  root    = Storage.Make(SceneYAML::NodeType::Sequence);
    const auto  scalar  = Storage.Scalar("retained text");
    const auto* address = scalar.Scalar().data();
    for (uint32_t i = 0; i < 4096; ++i)
    {
        const auto record = Storage.Make(SceneYAML::NodeType::Map);
        ASSERT_TRUE(Storage.Set(record, "index", Storage.UInt(i)));
        ASSERT_TRUE(Storage.Set(record, "text", scalar));
        ASSERT_TRUE(Storage.Append(root, record));
    }
    EXPECT_EQ(scalar.Scalar().data(), address);
    EXPECT_EQ(root.size(), 4096u);
    uint32_t i = 0;
    for (const auto record : root.Sequence())
    {
        uint32_t value = UINT32_MAX;
        ASSERT_TRUE(ReadSceneYAMLUInt(record["index"], value));
        EXPECT_EQ(value, i++);
        EXPECT_EQ(record["text"].Scalar(), "retained text");
    }
}

TEST_F(SceneYAMLTest, ReaderRejectsUnboundDestinationsAndRollsBackArenaExhaustion)
{
    SceneYAML::Node unbound;
    EXPECT_FALSE(ReadSceneYAML("a: 1", {}, &unbound, nullptr));
    SceneYAML::Storage small;
    ASSERT_TRUE(small.Initialize(4096));
    auto       output = small.Scalar("retained");
    const auto used   = small.BytesUsed();
    const auto source = std::string("a: \"") + std::string(4000, 'x') + '"';
    EXPECT_FALSE(ReadSceneYAML(source, {}, &output, nullptr));
    EXPECT_TRUE(small.Good());
    EXPECT_EQ(small.BytesUsed(), used);
    EXPECT_EQ(output.Scalar(), "retained");
    ASSERT_TRUE(ReadSceneYAML("a: 1", {}, &output, nullptr));
    EXPECT_EQ(output["a"].Scalar(), "1");
}

TEST_F(SceneYAMLTest, CyclesForeignChildrenAndFrozenMutationAreRejected)
{
    const auto root  = Storage.Make(SceneYAML::NodeType::Map);
    const auto child = Storage.Make(SceneYAML::NodeType::Sequence);
    ASSERT_TRUE(Storage.Set(root, "child", child));
    const auto mark = Storage.Mark();
    EXPECT_FALSE(Storage.Append(child, root));
    Storage.Rollback(mark);
    EXPECT_TRUE(Storage.Good());
    EXPECT_EQ(child.size(), 0u);
    SceneYAML::Storage foreign;
    ASSERT_TRUE(foreign.Initialize(4096));
    EXPECT_FALSE(Storage.Append(child, foreign.Scalar("foreign")));
    Storage.Rollback(mark);
    Storage.Freeze();
    EXPECT_FALSE(Storage.Set(root, "child", root));
    EXPECT_FALSE(Storage.Make(SceneYAML::NodeType::Map).IsDefined());
    EXPECT_EQ(root["child"].size(), 0u);
}

TEST_F(SceneYAMLTest, CycleWalkExactBudgetPreservesReplacementOnRefusalAndUsesActualOwner)
{
    YAMLFixedRoute route = {}, unrelated = {};
    route.Limit                = SIZE_MAX;
    SceneYAML::Storage storage = {};
    ASSERT_TRUE(storage.Initialize(ZMega(2), 1048576, 65536, 4096, &route));
    const auto map      = storage.Make(SceneYAML::NodeType::Map);
    const auto original = storage.Scalar("preserved");
    const auto nested   = storage.Make(SceneYAML::NodeType::Sequence);
    ASSERT_TRUE(storage.Set(map, "key", original));
    ASSERT_TRUE(storage.Append(nested, original));
    const auto mark     = storage.Mark();
    const auto attempts = route.Attempts;
    route.Limit         = 0;
    EXPECT_FALSE(storage.Set(map, "key", nested));
    EXPECT_EQ(storage.Failure(), SceneDiagnosticCode::LimitExceeded);
    const auto required = route.Required;
    ASSERT_GT(required, 0u);
    EXPECT_EQ(map["key"].Scalar(), "preserved");
    EXPECT_EQ(storage.BytesUsed(), mark.Arena.CurrentOffset);
    route.WatchBytes = required;
    for (const bool exact : {false, true})
    {
        storage.Rollback(mark);
        route.Limit = required - (exact ? 0 : 1);
        ZEngine::Core::Serialization::YAMLAllocationScope scope(unrelated);
        EXPECT_EQ(storage.Set(map, "key", nested), exact);
        EXPECT_EQ(storage.Good(), exact);
        EXPECT_EQ(storage.Failure(), exact ? SceneDiagnosticCode::None : SceneDiagnosticCode::LimitExceeded);
        EXPECT_EQ(map["key"].IsSequence(), exact);
        if (!exact)
        {
            EXPECT_EQ(map["key"].Scalar(), "preserved");
        }
        EXPECT_EQ(storage.BytesUsed(), mark.Arena.CurrentOffset);
        EXPECT_EQ(route.Live, 0u);
        EXPECT_EQ(route.Attempts, attempts);
        EXPECT_EQ(unrelated.Required, 0u);
    }
    EXPECT_EQ(route.WatchCalls, 2u);
    EXPECT_EQ(route.WatchReleases, 1u);
    EXPECT_EQ(route.Peak, required);
}

TEST_F(SceneYAMLTest, ScalarAndEmptyChildCycleChecksNeedNoTraversalReservation)
{
    YAMLFixedRoute     route   = {};
    SceneYAML::Storage storage = {};
    ASSERT_TRUE(storage.Initialize(ZMega(2), 1048576, 65536, 4096, &route));
    const auto parent = storage.Make(SceneYAML::NodeType::Sequence);
    ASSERT_TRUE(storage.Append(parent, storage.Scalar("scalar")));
    ASSERT_TRUE(storage.Append(parent, storage.Make(SceneYAML::NodeType::Null)));
    ASSERT_TRUE(storage.Append(parent, storage.Make(SceneYAML::NodeType::Map)));
    ASSERT_TRUE(storage.Append(parent, storage.Make(SceneYAML::NodeType::Sequence)));
    EXPECT_EQ(parent.size(), 4u);
    EXPECT_EQ(route.Required, 0u);
    EXPECT_EQ(route.Live, 0u);
    const auto used = storage.BytesUsed();
    EXPECT_FALSE(storage.Append(parent, parent));
    EXPECT_EQ(parent.size(), 4u);
    EXPECT_EQ(storage.BytesUsed(), used);
    EXPECT_EQ(route.Required, 0u);
}

TEST_F(SceneYAMLTest, CycleWalkPreservesDepthAndExpandedNodeVisitBoundaries)
{
    for (const uint32_t depth : {32u, 33u})
    {
        SceneYAML::Storage storage = {};
        ASSERT_TRUE(storage.Initialize(ZMega(2)));
        auto nested = storage.Make(SceneYAML::NodeType::Null);
        for (uint32_t i = 0; i < depth; ++i)
        {
            const auto parent = storage.Make(SceneYAML::NodeType::Sequence);
            ASSERT_TRUE(storage.Append(parent, nested));
            nested = parent;
        }
        const auto destination = storage.Make(SceneYAML::NodeType::Sequence);
        EXPECT_EQ(storage.Append(destination, nested), depth == 32);
        EXPECT_EQ(destination.size(), depth == 32 ? 1u : 0u);
    }
    // Shared nodes are legal, but repeated visits still consume the original
    // conservative bound; a DAG must not expand into unbounded traversal work.
    for (const uint32_t leaves : {3u, 4u})
    {
        SceneYAML::Storage storage = {};
        ASSERT_TRUE(storage.Initialize(ZMega(2), 4));
        const auto nested      = storage.Make(SceneYAML::NodeType::Sequence);
        const auto leaf        = storage.Make(SceneYAML::NodeType::Null);
        const auto destination = storage.Make(SceneYAML::NodeType::Sequence);
        for (uint32_t i = 0; i < leaves; ++i)
        {
            ASSERT_TRUE(storage.Append(nested, leaf));
        }
        EXPECT_EQ(storage.Append(destination, nested), leaves == 3);
        EXPECT_EQ(destination.size(), leaves == 3 ? 1u : 0u);
    }
}

TEST_F(SceneYAMLTest, WideCycleWalkFindsTheLastChildWithoutAllocatingOrChangingLinks)
{
    YAMLFixedRoute route       = {};
    route.Limit                = SIZE_MAX;
    SceneYAML::Storage storage = {};
    ASSERT_TRUE(storage.Initialize(ZMega(16), 1048576, 65536, 4096, &route));
    const auto nested = storage.Make(SceneYAML::NodeType::Sequence);
    const auto leaf   = storage.Scalar("leaf");
    const auto last   = storage.Make(SceneYAML::NodeType::Sequence);
    const auto holder = storage.Make(SceneYAML::NodeType::Map);
    ASSERT_TRUE(storage.Set(holder, "key", leaf));
    for (uint32_t i = 0; i < 65535; ++i)
    {
        ASSERT_TRUE(storage.Append(nested, leaf));
    }
    ASSERT_TRUE(storage.Append(nested, last));
    const auto attempts = route.Attempts;
    const auto used     = storage.BytesUsed();
    ASSERT_TRUE(storage.Set(holder, "key", nested));
    const auto required = route.Required;
    route.WatchBytes    = required;
    EXPECT_FALSE(storage.Append(last, nested));
    EXPECT_EQ(storage.Failure(), SceneDiagnosticCode::InvalidData);
    EXPECT_EQ(nested.size(), 65536u);
    EXPECT_EQ(last.size(), 0u);
    EXPECT_EQ(holder["key"].size(), 65536u);
    EXPECT_EQ(storage.BytesUsed(), used);
    EXPECT_EQ(route.Attempts, attempts);
    EXPECT_EQ(route.WatchCalls, 1u);
    EXPECT_EQ(route.WatchReleases, 1u);
    EXPECT_EQ(route.Live, 0u);
}
