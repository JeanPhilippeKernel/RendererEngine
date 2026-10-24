#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <gtest/gtest.h>
#include <limits>
#include <random>
#include <vector>

using namespace ZEngine::Core::VFS;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Importers;
using namespace ZEngine::Importers::AssetCodec;

class MeshAssetReaderTest : public testing::Test
{
protected:
    MemoryManager       Memory          = {};
    ArenaAllocator      DecodeArena     = {};
    VFSMemoryBackend    Backend         = {};
    VFSContext          VFS             = {};
    AssetMesh           Source          = {};
    AssetNodeHierarchy  Hierarchy       = {};
    AssetMesh           Output          = {};
    AssetNodeHierarchy  OutputHierarchy = {};
    ImportConfiguration Config          = {};
    VFSPath             Path            = VFSPath::Parse("/meshes/triangle.zemesh").Value();
    uuids::uuid         ID              = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    uuids::uuid         HierarchyID     = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");

    void                SetUp() override
    {
        Memory.Initialize(ZMega(64), {});
        Memory.MainArena.CreateSubArena(ZMega(16), &DecodeArena, "MeshAssetReaderTest/Decode");
        Backend.Initialize(&Memory.MainArena);
        VFS.Initialize(&Memory.MainArena);
        ASSERT_TRUE(VFS.Mount(&Backend, VFSPath::Root(), 0).Succeeded());
        Config.VFS = &VFS;
        Config.OutputAssetsPath.init(&Memory.MainArena, "/meshes");
        Config.OutputAssetFile.init(&Memory.MainArena, "triangle.zemesh");
        Source.MeshUUID = ID;
        Source.Vertices.init(&Memory.MainArena, 24, 24);
        Source.Vertices[8]  = 1.f;
        Source.Vertices[17] = 1.f;
        Source.Indices.init(&Memory.MainArena, 3);
        Source.Indices.push(0);
        Source.Indices.push(1);
        Source.Indices.push(2);
        Source.SubMeshes.init(&Memory.MainArena, 1);
        Source.SubMeshes.push({.VertexCount = 3, .IndexCount = 3, .VertexUnitStreamSize = 32, .IndexUnitStreamSize = 4, .TotalByteSize = 108});
        Hierarchy.NodeHierarchyUUID = HierarchyID;
        Hierarchy.MeshUUID          = ID;
        Hierarchy.Hierarchies.init(&Memory.MainArena, 2);
        Hierarchy.LocalTransforms.init(&Memory.MainArena, 2);
        Hierarchy.GlobalTransforms.init(&Memory.MainArena, 2);
        Hierarchy.Names.init(&Memory.MainArena, 2);
        Hierarchy.MaterialNames.init(&Memory.MainArena, 0);
        Hierarchy.NodeNames.init(&Memory.MainArena, 16);
        Hierarchy.NodeMeshes.init(&Memory.MainArena, 16);
        Hierarchy.NodeMaterials.init(&Memory.MainArena, 16);
        AddNode(Hierarchy, -1, 0);
        AddNode(Hierarchy, 0, 1);
        Hierarchy.Names.push_use({}).init(&Memory.MainArena, "Root");
        Hierarchy.Names.push_use({}).init(&Memory.MainArena, "Child");
        Hierarchy.NodeNames.insert(0, 0);
        Hierarchy.NodeNames.insert(1, 1);
        Hierarchy.NodeMeshes.insert(1, 0);
    }
    void TearDown() override
    {
        VFS.Shutdown();
    }
    bool Cook()
    {
        return SerializeMeshAssetFile(&Memory.MainArena, Source, Hierarchy, Config).Succeeded();
    }
    VFSResult<void> Read(const MeshAssetReadLimits& limits = {}, const uuids::uuid& expected = {})
    {
        return ReadMeshAssetFileVFS(&DecodeArena, VFS, Path, &Output, &OutputHierarchy, expected, limits);
    }
    std::vector<uint8_t> Bytes()
    {
        const auto open = VFS.Open(Path, VFSOpenFlags::Read);
        EXPECT_TRUE(open.Succeeded());
        if (open.Failed())
            return {};
        const auto size = open.Value()->Size();
        EXPECT_TRUE(size.Succeeded());
        std::vector<uint8_t> result(size.Value());
        const auto           read = open.Value()->ReadAll({result.data(), result.size()});
        EXPECT_TRUE(read.Succeeded());
        EXPECT_EQ(read.Value(), result.size());
        VFS.Close(open.Value());
        return result;
    }
    template <typename T>
    static void Set(std::vector<uint8_t>& bytes, size_t offset, const T& value)
    {
        ASSERT_LE(offset + sizeof(T), bytes.size());
        ASSERT_EQ(ZEngine::Helpers::secure_memcpy(bytes.data() + offset, bytes.size() - offset, &value, sizeof(T)), ZEngine::Helpers::MEMORY_OP_SUCCESS);
    }
    void Write(const std::vector<uint8_t>& bytes)
    {
        ASSERT_TRUE(WriteFileAtomically(VFS, Path, {bytes.data(), bytes.size()}).Succeeded());
    }
    void ExpectRejectedWithoutMutation()
    {
        const auto  before   = DecodeArena.m_current_offset;
        const auto  previous = DecodeArena.m_previous_offset;
        const auto  identity = Output.MeshUUID;
        const auto* vertices = Output.Vertices.data();
        const auto  result   = Read();
        EXPECT_TRUE(result.Failed());
        EXPECT_EQ(DecodeArena.m_current_offset, before);
        EXPECT_EQ(DecodeArena.m_previous_offset, previous);
        EXPECT_EQ(Output.MeshUUID, identity);
        EXPECT_EQ(Output.Vertices.data(), vertices);
    }
    // Offsets in this intentionally tiny v1 fixture, derived from serialized sizes.
    size_t IndicesCount() const
    {
        return 24 + 8 + Source.Vertices.size() * sizeof(float);
    }
    size_t SubMeshesCount() const
    {
        return IndicesCount() + 8 + Source.Indices.size() * sizeof(uint32_t);
    }
    size_t HierarchyIdentity() const
    {
        return SubMeshesCount() + 8 + Source.SubMeshes.size() * sizeof(AssetSubMesh);
    }
    size_t NodesCount() const
    {
        return HierarchyIdentity() + 32;
    }
    size_t LocalCount() const
    {
        return NodesCount() + 8 + Hierarchy.Hierarchies.size() * sizeof(ZEngine::Helpers::NodeHierarchy);
    }
    size_t GlobalCount() const
    {
        return LocalCount() + 8 + Hierarchy.LocalTransforms.size() * sizeof(ZEngine::Core::Maths::Mat4f);
    }
    size_t NamesCount() const
    {
        return GlobalCount() + 8 + Hierarchy.GlobalTransforms.size() * sizeof(ZEngine::Core::Maths::Mat4f);
    }
    size_t NodeNamesMap() const
    {
        size_t offset = NamesCount() + 4;
        for (const auto& name : Hierarchy.Names)
            offset += 8 + (name.size() ? name.size() + 1 : 0);
        offset += 4;
        for (const auto& name : Hierarchy.MaterialNames)
            offset += 8 + (name.size() ? name.size() + 1 : 0);
        return offset;
    }
};

TEST_F(MeshAssetReaderTest, RoundTripsCurrentCookerWithOwnedArenaDataAndStableIdentity)
{
    ASSERT_TRUE(Cook());
    ASSERT_TRUE(Read({}, ID).Succeeded());
    EXPECT_EQ(Output.MeshUUID, ID);
    EXPECT_EQ(Output.Vertices.size(), 24u);
    EXPECT_EQ(Output.Indices[2], 2u);
    EXPECT_EQ(Output.SubMeshes.size(), 1u);
    EXPECT_EQ(OutputHierarchy.NodeHierarchyUUID, HierarchyID);
    EXPECT_EQ(OutputHierarchy.MeshUUID, ID);
    EXPECT_EQ(OutputHierarchy.Hierarchies.size(), 2u);
    EXPECT_STREQ(OutputHierarchy.Names[0].c_str(), "Root");
    EXPECT_STREQ(OutputHierarchy.Names[1].c_str(), "Child");
    EXPECT_EQ(*OutputHierarchy.NodeNames.find(1), 1u);
    EXPECT_EQ(*OutputHierarchy.NodeMeshes.find(1), 0u);
    EXPECT_EQ(OutputHierarchy.Names[0].m_allocator, &DecodeArena);
    EXPECT_GT(DecodeArena.m_current_offset, 0u);
}

TEST_F(MeshAssetReaderTest, RuntimeBoundsDoNotChangeCookedBytesOrDeserializeAsAuthoredData)
{
    ASSERT_TRUE(Cook());
    const auto before   = Bytes();
    Source.BoundsCenter = {100.f, 200.f, 300.f};
    Source.BoundsRadius = 400.f;
    Source.BoundsMin    = {-5.f, -6.f, -7.f};
    Source.BoundsMax    = {8.f, 9.f, 10.f};
    Source.BoundsValid  = true;
    ASSERT_TRUE(Cook());
    EXPECT_EQ(Bytes(), before);
    ASSERT_TRUE(Read().Succeeded());
    EXPECT_FALSE(Output.BoundsValid);
    EXPECT_EQ(Output.BoundsRadius, 0.f);
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        EXPECT_EQ(Output.BoundsCenter[axis], 0.f);
        EXPECT_EQ(Output.BoundsMin[axis], 0.f);
        EXPECT_EQ(Output.BoundsMax[axis], 0.f);
    }
}

TEST_F(MeshAssetReaderTest, WideCookerHierarchyMaintainsTheTailWithoutRepeatedSiblingWalks)
{
    for (int i = 0; i < 4096; ++i)
    {
        const auto child = AddNode(Hierarchy, 0, 1);
        ASSERT_GT(child, 0);
        EXPECT_EQ(Hierarchy.Hierarchies[0].LastChild, child);
        EXPECT_EQ(Hierarchy.Hierarchies[child - 1].RightSibling, child);
    }
    ASSERT_TRUE(Cook());
    ASSERT_TRUE(Read().Succeeded());
    EXPECT_EQ(OutputHierarchy.Hierarchies[0].FirstChild, 1);
    EXPECT_EQ(OutputHierarchy.Hierarchies[0].LastChild, 4097);
    EXPECT_EQ(OutputHierarchy.Hierarchies[4097].RightSibling, -1);
}

TEST_F(MeshAssetReaderTest, InvalidCookerParentAndDepthDoNotAppendPartialNodes)
{
    struct ParentDepth
    {
        int Parent = -1;
        int Depth  = 0;
    };
    const ParentDepth cases[] = {
        {-2, 0},
        {-1, 1},
        { 9, 1},
        { 0, 0},
        { 1, 1}
    };
    for (const auto& input : cases)
    {
        EXPECT_EQ(AddNode(Hierarchy, input.Parent, input.Depth), -1);
        EXPECT_EQ(Hierarchy.Hierarchies.size(), 2u);
        EXPECT_EQ(Hierarchy.LocalTransforms.size(), 2u);
        EXPECT_EQ(Hierarchy.GlobalTransforms.size(), 2u);
    }
    EXPECT_EQ(Hierarchy.Hierarchies[0].LastChild, 1);
    EXPECT_EQ(Hierarchy.Hierarchies[1].RightSibling, -1);
}

TEST_F(MeshAssetReaderTest, CookerAppendRepairsLegacyMissingTailAndRejectsCyclicLinks)
{
    Hierarchy.Hierarchies[0].LastChild = -1;
    EXPECT_EQ(AddNode(Hierarchy, 0, 1), 2);
    EXPECT_EQ(Hierarchy.Hierarchies[0].LastChild, 2);
    EXPECT_EQ(Hierarchy.Hierarchies[1].RightSibling, 2);
    Hierarchy.Hierarchies[0].LastChild    = -1;
    Hierarchy.Hierarchies[2].RightSibling = 1;
    EXPECT_EQ(AddNode(Hierarchy, 0, 1), -1);
    EXPECT_EQ(Hierarchy.Hierarchies.size(), 3u);
}

TEST_F(MeshAssetReaderTest, CookerAppendCapacityFailureLeavesAllPublishedPrefixesUnchanged)
{
    const auto capacity           = Memory.MainArena.m_total_size;
    Memory.MainArena.m_total_size = Memory.MainArena.m_current_offset;
    EXPECT_EQ(AddNode(Hierarchy, 0, 1), -1);
    Memory.MainArena.m_total_size = capacity;
    EXPECT_EQ(Hierarchy.Hierarchies.size(), 2u);
    EXPECT_EQ(Hierarchy.LocalTransforms.size(), 2u);
    EXPECT_EQ(Hierarchy.GlobalTransforms.size(), 2u);
    EXPECT_EQ(Hierarchy.Hierarchies[0].LastChild, 1);
    EXPECT_EQ(Hierarchy.Hierarchies[1].RightSibling, -1);
    ASSERT_EQ(AddNode(Hierarchy, 0, 1), 2);
    ASSERT_TRUE(Cook());
    ASSERT_TRUE(Read().Succeeded());
}

TEST_F(MeshAssetReaderTest, EveryTruncatedPrefixFailsAndRestoresExistingOutputsAndArena)
{
    ASSERT_TRUE(Cook());
    ASSERT_TRUE(Read().Succeeded());
    const auto complete = Bytes();
    for (size_t length = 0; length < complete.size(); ++length)
    {
        SCOPED_TRACE(length);
        Write({complete.begin(), complete.begin() + length});
        ExpectRejectedWithoutMutation();
    }
}

TEST_F(MeshAssetReaderTest, MagicAndVersionMustBothMatchAndTrailingDataIsRejected)
{
    ASSERT_TRUE(Cook());
    const auto original = Bytes();
    for (size_t offset : {size_t(0), size_t(4)})
    {
        auto bytes = original;
        Set(bytes, offset, uint32_t(0));
        Write(bytes);
        ExpectRejectedWithoutMutation();
    }
    auto bytes = original;
    bytes.push_back(0);
    Write(bytes);
    ExpectRejectedWithoutMutation();
}

TEST_F(MeshAssetReaderTest, RejectsNilMismatchedAndAliasedAssetIdentities)
{
    ASSERT_TRUE(Cook());
    EXPECT_EQ(Read({}, HierarchyID).Error(), VFSError::Corrupted);
    const auto original = Bytes();
    for (size_t offset : {size_t(8), HierarchyIdentity(), HierarchyIdentity() + 16})
    {
        auto bytes = original;
        Set(bytes, offset, uuids::uuid{});
        Write(bytes);
        ExpectRejectedWithoutMutation();
    }
    auto bytes = original;
    Set(bytes, HierarchyIdentity(), ID);
    Write(bytes);
    ExpectRejectedWithoutMutation();
}

TEST_F(MeshAssetReaderTest, ChecksEveryCountBeforeAllocationOrIntegerNarrowing)
{
    ASSERT_TRUE(Cook());
    const auto original = Bytes();
    for (size_t offset : {size_t(24), IndicesCount(), SubMeshesCount(), NodesCount(), LocalCount(), GlobalCount(), NamesCount() + 4, NodeNamesMap()})
    {
        SCOPED_TRACE(offset);
        auto bytes = original;
        Set(bytes, offset, UINT64_MAX);
        Write(bytes);
        ExpectRejectedWithoutMutation();
    }
    auto bytes = original;
    Set(bytes, NamesCount(), UINT32_MAX);
    Write(bytes);
    ExpectRejectedWithoutMutation();
}

TEST_F(MeshAssetReaderTest, FileDecodeAndStringBudgetsFailWithoutChangingState)
{
    ASSERT_TRUE(Cook());
    MeshAssetReadLimits limits = {};
    limits.MaxFileBytes        = 1;
    EXPECT_EQ(Read(limits).Error(), VFSError::SizeLimitExceeded);
    limits                 = {};
    limits.MaxDecodedBytes = 1;
    EXPECT_EQ(Read(limits).Error(), VFSError::SizeLimitExceeded);
    limits             = {};
    limits.MaxVertices = 2;
    EXPECT_EQ(Read(limits).Error(), VFSError::SizeLimitExceeded);
    limits                = {};
    limits.MaxStringBytes = 3;
    EXPECT_EQ(Read(limits).Error(), VFSError::SizeLimitExceeded);
    limits                     = {};
    limits.MaxStringTotalBytes = 1;
    EXPECT_EQ(Read(limits).Error(), VFSError::SizeLimitExceeded);
    EXPECT_EQ(DecodeArena.m_current_offset, 0u);
    EXPECT_EQ(Output.Vertices.size(), 0u);
}

TEST_F(MeshAssetReaderTest, ArenaExhaustionIsRecoverableAndPreservesPreviousOutput)
{
    ASSERT_TRUE(Cook());
    ASSERT_TRUE(Read().Succeeded());
    const auto*    vertices = Output.Vertices.data();
    ArenaAllocator tiny     = {};
    Memory.MainArena.CreateSubArena(64, &tiny, "MeshAssetReaderTest/Tiny");
    EXPECT_EQ(ReadMeshAssetFileVFS(&tiny, VFS, Path, &Output, &OutputHierarchy).Error(), VFSError::OutOfMemory);
    EXPECT_EQ(tiny.m_current_offset, 0u);
    EXPECT_EQ(Output.Vertices.data(), vertices);
    EXPECT_EQ(Output.MeshUUID, ID);
}

TEST_F(MeshAssetReaderTest, EmptyAndNonFiniteVertexDataIsRejected)
{
    ASSERT_TRUE(Cook());
    auto bytes = Bytes();
    Set(bytes, 32, std::numeric_limits<float>::quiet_NaN());
    Write(bytes);
    ExpectRejectedWithoutMutation();
    Source.Vertices.m_size = 0;
    ASSERT_TRUE(Cook());
    ExpectRejectedWithoutMutation();
}

TEST_F(MeshAssetReaderTest, IndicesMustBeSubmeshLocalAndInsideTheirOwnVertexRange)
{
    Source.Indices[2] = 3;
    ASSERT_TRUE(Cook());
    ExpectRejectedWithoutMutation();
    Source.Indices[2]               = 2;
    Source.SubMeshes[0].VertexCount = UINT32_MAX;
    ASSERT_TRUE(Cook());
    ExpectRejectedWithoutMutation();
}

TEST_F(MeshAssetReaderTest, SubmeshRangesAndStridesCannotEscapeOrRescanTheStream)
{
    const auto original = Source.SubMeshes[0];
    for (uint32_t* field : {&Source.SubMeshes[0].VertexOffset, &Source.SubMeshes[0].IndexOffset, &Source.SubMeshes[0].IndexCount, &Source.SubMeshes[0].VertexUnitStreamSize, &Source.SubMeshes[0].IndexUnitStreamSize})
    {
        Source.SubMeshes[0] = original;
        *field              = UINT32_MAX;
        ASSERT_TRUE(Cook());
        ExpectRejectedWithoutMutation();
    }
    Source.SubMeshes[0]            = original;
    Source.SubMeshes[0].IndexCount = 2;
    ASSERT_TRUE(Cook());
    ExpectRejectedWithoutMutation();
}

TEST_F(MeshAssetReaderTest, TransformCountsAndFiniteValuesAreValidated)
{
    Hierarchy.GlobalTransforms[0](1, 2) = std::numeric_limits<float>::infinity();
    ASSERT_TRUE(Cook());
    ExpectRejectedWithoutMutation();
    Hierarchy.GlobalTransforms[0](1, 2) = 0.f;
    Hierarchy.LocalTransforms.m_size    = 1;
    ASSERT_TRUE(Cook());
    ExpectRejectedWithoutMutation();
}

TEST_F(MeshAssetReaderTest, HierarchyRequiresConsistentDepthParentsAndSiblingLinks)
{
    const auto root  = Hierarchy.Hierarchies[0];
    const auto child = Hierarchy.Hierarchies[1];
    for (int* field : {&Hierarchy.Hierarchies[0].Parent, &Hierarchy.Hierarchies[0].FirstChild, &Hierarchy.Hierarchies[0].LastChild, &Hierarchy.Hierarchies[1].Parent, &Hierarchy.Hierarchies[1].RightSibling, &Hierarchy.Hierarchies[1].DepthLevel})
    {
        Hierarchy.Hierarchies[0] = root;
        Hierarchy.Hierarchies[1] = child;
        *field                   = INT32_MAX;
        ASSERT_TRUE(Cook());
        ExpectRejectedWithoutMutation();
    }
    Hierarchy.Hierarchies[0]            = root;
    Hierarchy.Hierarchies[1]            = child;
    Hierarchy.Hierarchies[0].FirstChild = -1;
    ASSERT_TRUE(Cook());
    ExpectRejectedWithoutMutation();
    Hierarchy.Hierarchies[0]        = root;
    Hierarchy.Hierarchies[1].Parent = 1;
    ASSERT_TRUE(Cook());
    ExpectRejectedWithoutMutation();
}

TEST_F(MeshAssetReaderTest, StringsRequireTheirEncodedTerminatorAndNoEmbeddedNull)
{
    ASSERT_TRUE(Cook());
    const auto original = Bytes();
    for (const auto [at, value] : {
         std::pair{    NamesCount() + 4 + 8,   uint8_t(0)},
         std::pair{NamesCount() + 4 + 8 + 4, uint8_t('x')}
    })
    {
        auto bytes = original;
        bytes[at]  = value;
        Write(bytes);
        ExpectRejectedWithoutMutation();
    }
}

TEST_F(MeshAssetReaderTest, EmptyNamesUseTheLegacyZeroLengthEncodingWithoutAStoredTerminator)
{
    Hierarchy.Names[0].clear();
    ASSERT_TRUE(Cook());
    ASSERT_TRUE(Read().Succeeded());
    EXPECT_TRUE(OutputHierarchy.Names[0].empty());
    ASSERT_NE(OutputHierarchy.Names[0].c_str(), nullptr);
    EXPECT_EQ(OutputHierarchy.Names[0].c_str()[0], '\0');
    EXPECT_STREQ(OutputHierarchy.Names[1].c_str(), "Child");
}

TEST_F(MeshAssetReaderTest, MapsRejectDuplicateKeysAndOutOfRangeReferences)
{
    ASSERT_TRUE(Cook());
    const auto original = Bytes();
    for (const auto [at, value] : {
         std::pair{ NodeNamesMap() + 8, UINT32_MAX},
         std::pair{NodeNamesMap() + 12, UINT32_MAX}
    })
    {
        auto bytes = original;
        Set(bytes, at, value);
        Write(bytes);
        ExpectRejectedWithoutMutation();
    }
    auto     bytes     = original;
    uint32_t first_key = 0;
    ZEngine::Helpers::secure_memcpy(&first_key, sizeof(first_key), bytes.data() + NodeNamesMap() + 8, sizeof(first_key));
    Set(bytes, NodeNamesMap() + 16, first_key);
    Write(bytes);
    ExpectRejectedWithoutMutation();
}

TEST_F(MeshAssetReaderTest, DeepHierarchyValidationIsIterativeAndBounded)
{
    constexpr uint32_t count = 4096;
    Hierarchy.Hierarchies.init(&Memory.MainArena, count);
    Hierarchy.LocalTransforms.init(&Memory.MainArena, count);
    Hierarchy.GlobalTransforms.init(&Memory.MainArena, count);
    Hierarchy.Names.clear();
    Hierarchy.NodeNames.clear();
    Hierarchy.NodeMeshes.clear();
    for (uint32_t i = 0; i < count; ++i)
        AddNode(Hierarchy, i ? int(i) - 1 : -1, int(i));
    Hierarchy.NodeMeshes.insert(count - 1, 0);
    ASSERT_TRUE(Cook());
    ASSERT_TRUE(Read().Succeeded());
    EXPECT_EQ(OutputHierarchy.Hierarchies.size(), count);
    EXPECT_EQ(OutputHierarchy.Hierarchies[count - 1].DepthLevel, int(count) - 1);
}

TEST_F(MeshAssetReaderTest, CorruptInputStressNeverPublishesPartialOutputOrGrowsFailedTransactions)
{
    ASSERT_TRUE(Cook());
    const auto   original = Bytes();
    std::mt19937 random(1234);
    for (uint32_t iteration = 0; iteration < 1000; ++iteration)
    {
        auto           bytes = original;
        const uint32_t edits = random() % 4 + 1;
        for (uint32_t edit = 0; edit < edits; ++edit)
            bytes[random() % bytes.size()] ^= uint8_t(random() % 255 + 1);
        Write(bytes);
        const auto         scratch   = BeginTempArena(&DecodeArena);
        AssetMesh          mesh      = {};
        AssetNodeHierarchy hierarchy = {};
        const auto         result    = ReadMeshAssetFileVFS(&DecodeArena, VFS, Path, &mesh, &hierarchy);
        if (result.Failed())
        {
            EXPECT_EQ(DecodeArena.m_current_offset, scratch.CurrentOffset);
            EXPECT_TRUE(mesh.MeshUUID.is_nil());
            EXPECT_TRUE(hierarchy.Hierarchies.empty());
        }
        else
        {
            EXPECT_EQ(mesh.Indices.size(), 3u);
            EXPECT_EQ(hierarchy.Hierarchies.size(), 2u);
        }
        EndTempArena(scratch);
    }
}
