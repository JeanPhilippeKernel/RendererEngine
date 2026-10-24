#include <ZEngine/ECS/SceneYAMLReader.h>
#include <ZEngine/ECS/SceneYamlValue.h>
#include <gtest/gtest.h>
#include <limits>

using namespace ZEngine::ECS;

class SceneYAMLTest : public testing::Test
{
protected:
    SceneYAML::Storage Storage = {};
    void               SetUp() override
    {
        ASSERT_TRUE(Storage.Initialize());
    }
};

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
    auto output               = Storage.Scalar("preserved");
    auto limits               = SceneDocumentLimits{};
    limits.MaxCollectionItems = 1;
    EXPECT_FALSE(ReadSceneYAML("[1, 2]", limits, &output, nullptr));
    limits                = {};
    limits.MaxScalarBytes = 1;
    EXPECT_FALSE(ReadSceneYAML("ab", limits, &output, nullptr));
    limits          = {};
    limits.MaxNodes = 2;
    EXPECT_FALSE(ReadSceneYAML("a: 1", limits, &output, nullptr));
    limits          = {};
    limits.MaxDepth = 2;
    EXPECT_FALSE(ReadSceneYAML("[[[1]]]", limits, &output, nullptr));
    limits              = {};
    limits.MaxFileBytes = 2;
    EXPECT_FALSE(ReadSceneYAML("a: 1", limits, &output, nullptr));
    EXPECT_EQ(output.Scalar(), "preserved");
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
