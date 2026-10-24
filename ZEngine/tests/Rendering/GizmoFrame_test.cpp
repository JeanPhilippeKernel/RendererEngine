#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Rendering/Renderers/Graphics/GizmoPass.h>
#include <gtest/gtest.h>
#include <array>
#include <limits>

using namespace ZEngine::Rendering::Renderers;
using namespace ZEngine::Applications;

TEST(GizmoFrameTest, DefaultsAreEmptyAndRecordsAreInitialized)
{
    GizmoFrameData frame = {};
    EXPECT_TRUE(frame.Empty());
    EXPECT_TRUE(frame.ValidCounts());
    EXPECT_TRUE(frame.ValidMeshLineGroups());
    EXPECT_EQ(frame.MeshLineGroupCount, 0u);
    EXPECT_TRUE(frame.MeshLineGroups[0].MeshUUID.is_nil());
    EXPECT_FALSE(frame.DecorationsOmitted);
    EXPECT_EQ(frame.Lines[0].A.x, 0.f);
    EXPECT_EQ(frame.Lines[0].Width, 0.f);
    EXPECT_EQ(frame.Lines[0].Color, 0u);
    EXPECT_FALSE(frame.Lines[0].RoundCaps);
    EXPECT_EQ(frame.Lines[0].StartJoinNormal.x, 0.f);
    EXPECT_EQ(frame.Lines[0].EndJoinNormal.y, 0.f);
    EXPECT_TRUE(frame.Lines[0].ValidJoins());
    EXPECT_EQ(frame.Triangles[0].C.z, 0.f);
    EXPECT_EQ(frame.Triangles[0].AntiAliasEdges, 7u);
    EXPECT_EQ(frame.Sprites[0].OutlineWidth, 0.f);
    EXPECT_FALSE(frame.Sprites[0].Round);
    EXPECT_LT(sizeof(GizmoFrameData), 160 * 1024u);
}

TEST(GizmoFrameTest, CopiesOnlyUsedPrefixesAndResetOnlyResetsCounts)
{
    GizmoFrameData source = {}, destination = {};
    source.LineCount = source.TriangleCount = source.SpriteCount = 1;
    source.DecorationsOmitted                                    = true;
    source.MeshLineGroupCount                                    = 1;
    source.MeshLineGroups[0]                                     = {.MeshUUID = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111"), .Count = 1};
    destination.MeshLineGroups[1].Count                          = 103;
    source.Lines[0].Width                                        = 2.5f;
    source.Triangles[0].Color                                    = 17;
    source.Sprites[0].Size                                       = 12.f;
    destination.Lines[1].Width                                   = 71.f;
    destination.Triangles[1].Color                               = 83;
    destination.Sprites[1].Size                                  = 97.f;
    destination                                                  = source;
    EXPECT_EQ(destination.Lines[0].Width, 2.5f);
    EXPECT_EQ(destination.Triangles[0].Color, 17u);
    EXPECT_EQ(destination.Sprites[0].Size, 12.f);
    EXPECT_TRUE(destination.DecorationsOmitted);
    EXPECT_EQ(destination.Lines[1].Width, 71.f);
    EXPECT_EQ(destination.Triangles[1].Color, 83u);
    EXPECT_EQ(destination.Sprites[1].Size, 97.f);
    EXPECT_EQ(destination.MeshLineGroupCount, 1u);
    EXPECT_EQ(destination.MeshLineGroups[0].MeshUUID, source.MeshLineGroups[0].MeshUUID);
    EXPECT_EQ(destination.MeshLineGroups[1].Count, 103u);
    destination.Reset();
    EXPECT_TRUE(destination.Empty());
    EXPECT_FALSE(destination.DecorationsOmitted);
    EXPECT_EQ(destination.MeshLineGroupCount, 0u);
    EXPECT_EQ(destination.MeshLineGroups[0].Count, 1u);
    EXPECT_EQ(destination.Lines[0].Width, 2.5f);
    EXPECT_EQ(destination.Lines[1].Width, 71.f);
}

TEST(GizmoFrameTest, InvalidCountsNeverCopyOutOfBounds)
{
    GizmoFrameData source = {}, destination = {};
    source.LineCount           = GizmoFrameData::MaxLines + 1;
    destination.LineCount      = 1;
    destination.Lines[0].Color = 13;
    destination                = source;
    EXPECT_TRUE(destination.Empty());
    EXPECT_EQ(destination.Lines[0].Color, 13u);
}

TEST(GizmoFrameTest, MailboxRetainsIndependentGeometryWithTheExactCamera)
{
    AppRenderPipeline pipeline = {};
    RenderFrameState  source = {}, read = {};
    source.Camera.Position.x = 7.f;
    source.Gizmo.LineCount   = 1;
    source.Gizmo.Lines[0]    = {
        .A = {7.f, 0.f, 0.f},
          .Width = 2.5f, .Color = 41
    };
    ASSERT_TRUE(pipeline.PublishFrameState(source));
    source.Camera.Position.x  = 13.f;
    source.Gizmo.Lines[0].A.x = 13.f;
    ASSERT_TRUE(pipeline.TryReadFrameState(read));
    EXPECT_EQ(read.Camera.Position.x, 7.f);
    ASSERT_EQ(read.Gizmo.LineCount, 1u);
    EXPECT_EQ(read.Gizmo.Lines[0].A.x, 7.f);
    for (uint32_t i = 0; i < 100; ++i)
        ASSERT_TRUE(pipeline.PublishFrameState(source));
    EXPECT_EQ(read.Gizmo.Lines[0].A.x, 7.f);
    ASSERT_TRUE(pipeline.TryReadFrameState(read));
    EXPECT_EQ(read.Gizmo.Lines[0].A.x, 13.f);
    source.Gizmo.Reset();
    ASSERT_TRUE(pipeline.PublishFrameState(source));
    ASSERT_TRUE(pipeline.TryReadFrameState(read));
    EXPECT_TRUE(read.Gizmo.Empty());
    pipeline.ReleaseFrameState();
}

TEST(GizmoFrameTest, InvalidGeometryCountsDoNotEvictUnreadFrames)
{
    AppRenderPipeline pipeline = {};
    RenderFrameState  source = {}, read = {};
    source.Camera.Position.x = 11.f;
    ASSERT_TRUE(pipeline.PublishFrameState(source));
    source.Gizmo.TriangleCount = GizmoFrameData::MaxTriangles + 1;
    EXPECT_FALSE(pipeline.PublishFrameState(source));
    ASSERT_TRUE(pipeline.TryReadFrameState(read));
    EXPECT_EQ(read.Camera.Position.x, 11.f);
    EXPECT_TRUE(read.Gizmo.Empty());
    EXPECT_FALSE(pipeline.TryReadFrameState(read));
    pipeline.ReleaseFrameState();
}

TEST(GizmoPassTest, EmptyPassDropsOutBeforeAccessingGraphOrDevice)
{
    GizmoPass pass = {};
    EXPECT_FALSE(pass.Register(nullptr, "Gizmo Pass", {}, nullptr, nullptr));
    GizmoFrameData frame = {};
    frame.LineCount      = 1;
    frame.Lines[0].Width = 2.f;
    pass.SetFrame(frame);
    EXPECT_FALSE(pass.Register(nullptr, "Gizmo Pass", {}, nullptr, nullptr));
    frame.Reset();
    pass.SetFrame(frame);
    EXPECT_FALSE(pass.Register(nullptr, "Gizmo Pass", {}, nullptr, nullptr));
    pass.Deinitialize(nullptr);
    pass.Deinitialize(nullptr);
}

TEST(GizmoPassTest, PipelineIsOneInstancedStraightAlphaOverlayWithoutDepth)
{
    ZEngine::Core::Memory::ArenaAllocator arena = {};
    arena.Initialize(4096, 4096);
    GizmoPass  pass = {};
    const auto desc = pass.BuildGraphicsPipelineDescription(&arena);
    EXPECT_STREQ(desc.ShaderSpecificationValue.Name, "gizmo");
    EXPECT_FALSE(desc.EnableDepthTest);
    EXPECT_FALSE(desc.EnableDepthWrite);
    EXPECT_TRUE(desc.EnableBlending);
    EXPECT_EQ(desc.CullMode, VK_CULL_MODE_NONE);
    ASSERT_EQ(desc.VertexInputBindingSpecifications.size(), 1u);
    EXPECT_EQ(desc.VertexInputBindingSpecifications[0].Rate, VK_VERTEX_INPUT_RATE_INSTANCE);
    EXPECT_EQ(desc.VertexInputBindingSpecifications[0].Stride, sizeof(GizmoGPUInstance));
    ASSERT_EQ(desc.VertexInputAttributeSpecifications.size(), 6u);
    for (uint32_t i = 0; i < 4; ++i)
        EXPECT_EQ(desc.VertexInputAttributeSpecifications[i].Offset, i * 16u);
    EXPECT_EQ(desc.VertexInputAttributeSpecifications[4].Offset, 64u);
    EXPECT_EQ(desc.VertexInputAttributeSpecifications[5].Offset, 68u);
}

TEST(GizmoPassTest, PackingPreservesTriangleLineSpriteOrderAndAllParameters)
{
    GizmoFrameData frame = {};
    frame.TriangleCount = frame.LineCount = frame.SpriteCount = 1;
    frame.Triangles[0]                                        = {
        .A = {1.f, 2.f, 3.f},
          .B = {4.f, 5.f, 6.f},
          .C = {7.f, 8.f, 9.f},
          .Color = 19
    };
    frame.Lines[0] = {
        .A = {10.f, 11.f, 12.f},
          .B = {13.f, 14.f, 15.f},
          .Width = 2.5f, .DashOn = 4.f, .DashOff = 3.f, .DashPhase = 17.f, .Color = 23, .RoundCaps = true
    };
    frame.Sprites[0] = {
        .Position = {21.f, 22.f, 23.f},
          .Size = 12.f, .OutlineWidth = 1.f, .Fill = 29, .Outline = 31, .Round = true
    };
    std::array<GizmoGPUInstance, 4> packed = {};
    packed[3].Color                        = 71;
    uint32_t count                         = 99;
    ASSERT_TRUE(GizmoPass::Pack(frame, packed.data(), packed.size(), &count));
    ASSERT_EQ(count, 3u);
    EXPECT_EQ(packed[0].A[0], 1.f);
    EXPECT_EQ(packed[0].C[2], 9.f);
    EXPECT_EQ(packed[0].Parameters[3], 0.f);
    EXPECT_EQ(packed[0].Parameters[0], 7.f);
    EXPECT_EQ(packed[0].Color, 19u);
    EXPECT_EQ(packed[1].A[0], 10.f);
    EXPECT_EQ(packed[1].A[3], 2.5f);
    EXPECT_EQ(packed[1].B[3], 4.f);
    EXPECT_EQ(packed[1].C[3], 3.f);
    EXPECT_EQ(packed[1].Parameters[0], 17.f);
    EXPECT_EQ(packed[1].Parameters[1], 1.f);
    EXPECT_EQ(packed[1].Parameters[3], 1.f);
    EXPECT_EQ(packed[1].Color, 23u);
    EXPECT_EQ(packed[2].A[1], 22.f);
    EXPECT_EQ(packed[2].Parameters[0], 1.f);
    EXPECT_EQ(packed[2].Parameters[1], 12.f);
    EXPECT_EQ(packed[2].Parameters[2], 1.f);
    EXPECT_EQ(packed[2].Parameters[3], 2.f);
    EXPECT_EQ(packed[2].Outline, 31u);
    EXPECT_EQ(packed[3].Color, 71u);
}

TEST(GizmoPassTest, JoinedLineNormalsUseExistingInstanceFieldsAndRejectMalformedRecipesAtomically)
{
    GizmoFrameData frame                   = {};
    frame.LineCount                        = 1;
    frame.Lines[0]                         = {.Width = 3.5f, .Color = 29, .RoundCaps = true, .StartJoinNormal = GizmoJoinNormal::FromUnit(.6, .8), .EndJoinNormal = GizmoJoinNormal::FromUnit(1.0, 0.0)};
    std::array<GizmoGPUInstance, 2> output = {};
    uint32_t                        count  = 0;
    ASSERT_TRUE(GizmoPass::Pack(frame, output.data(), output.size(), &count));
    ASSERT_EQ(count, 1u);
    EXPECT_EQ(output[0].Parameters[1], 7.f);
    EXPECT_NEAR(output[0].C[0], .6f, .0001f);
    EXPECT_NEAR(output[0].C[1], .8f, .0001f);
    EXPECT_EQ(output[0].C[2], 1.f);
    EXPECT_EQ(output[0].Parameters[2], 0.f);
    GizmoFrameData copied = frame;
    EXPECT_NEAR(copied.Lines[0].StartJoinNormal.NormalX(), .6f, .0001f);
    EXPECT_EQ(copied.Lines[0].EndJoinNormal.NormalX(), 1.f);
    EXPECT_FALSE(GizmoJoinNormal::FromUnit(std::numeric_limits<float>::quiet_NaN(), 1.0).Enabled());
    EXPECT_FALSE(GizmoJoinNormal::FromUnit(1.0, std::numeric_limits<float>::infinity()).Enabled());
    const auto original = frame.Lines[0];
    for (uint32_t malformed = 0; malformed < 4; ++malformed)
    {
        frame.Lines[0] = original;
        if (malformed == 0)
            frame.Lines[0].StartJoinNormal = {1, 0};
        else if (malformed == 1)
            frame.Lines[0].EndJoinNormal = {32767, 32767};
        else if (malformed == 2)
            frame.Lines[0].StartJoinNormal = {16384, 0};
        else
            frame.Lines[0].RoundCaps = false;
        output[0].Color = 71;
        count           = 83;
        EXPECT_FALSE(GizmoPass::Pack(frame, output.data(), output.size(), &count));
        EXPECT_EQ(output[0].Color, 71u);
        EXPECT_EQ(count, 83u);
    }
}

TEST(GizmoPassTest, InvalidRecipesPreserveAllOutputAndCount)
{
    GizmoFrameData frame                   = {};
    frame.LineCount                        = 1;
    std::array<GizmoGPUInstance, 2> packed = {};
    packed[0].Color                        = 61;
    uint32_t count                         = 73;
    EXPECT_FALSE(GizmoPass::Pack(frame, packed.data(), packed.size(), &count));
    frame.Lines[0].Width = 2.f;
    EXPECT_FALSE(GizmoPass::Pack(frame, packed.data(), 0, &count));
    frame.SpriteCount = 1;
    frame.Sprites[0]  = {.Size = 12.f, .OutlineWidth = 7.f};
    EXPECT_FALSE(GizmoPass::Pack(frame, packed.data(), packed.size(), &count));
    frame.SpriteCount  = 0;
    frame.Lines[0].A.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(GizmoPass::Pack(frame, packed.data(), packed.size(), &count));
    EXPECT_EQ(packed[0].Color, 61u);
    EXPECT_EQ(count, 73u);
}

TEST(GizmoPassTest, PackingMaximumCapacityDoesNotTruncate)
{
    GizmoFrameData frame = {};
    frame.LineCount      = GizmoFrameData::MaxLines;
    frame.TriangleCount  = GizmoFrameData::MaxTriangles;
    frame.SpriteCount    = GizmoFrameData::MaxSprites;
    for (auto& line : frame.Lines)
        line.Width = 2.f;
    for (auto& sprite : frame.Sprites)
        sprite.Size = 12.f;
    std::array<GizmoGPUInstance, GizmoPass::MaxInstances> packed = {};
    uint32_t                                              count  = 0;
    ASSERT_TRUE(GizmoPass::Pack(frame, packed.data(), packed.size(), &count));
    EXPECT_EQ(count, GizmoPass::MaxInstances);
    EXPECT_EQ(packed.back().Parameters[3], 2.f);
    EXPECT_FALSE(GizmoPass::Pack(frame, packed.data(), packed.size() - 1, &count));
    EXPECT_EQ(count, GizmoPass::MaxInstances);
}

TEST(GizmoPassTest, ResidencyFiltersOnlyMeshBoxRangesAndKeepsPaintOrder)
{
    GizmoFrameData frame = {};
    frame.LineCount      = 6;
    for (uint32_t i = 0; i < frame.LineCount; ++i)
        frame.Lines[i] = {.Width = 1.f, .Color = i + 1};
    const auto first         = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    const auto second        = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");
    frame.MeshLineGroupCount = 2;
    frame.MeshLineGroups[0]  = {.MeshUUID = first, .First = 1, .Count = 2};
    frame.MeshLineGroups[1]  = {.MeshUUID = second, .First = 4, .Count = 1};
    struct Residency
    {
        uuids::uuid Visible = {};
        uint32_t    Calls   = 0;
    } residency        = {.Visible = second};
    const auto visible = [](void* context, const uuids::uuid& id) {
        auto& service = *static_cast<Residency*>(context);
        ++service.Calls;
        return id == service.Visible;
    };
    std::array<GizmoGPUInstance, 6> packed = {};
    uint32_t                        count  = 17;
    ASSERT_TRUE(GizmoPass::Pack(frame, packed.data(), 4, &count, visible, &residency));
    EXPECT_EQ(count, 4u);
    EXPECT_EQ(residency.Calls, 2u);
    EXPECT_EQ(packed[0].Color, 1u);
    EXPECT_EQ(packed[1].Color, 4u);
    EXPECT_EQ(packed[2].Color, 5u);
    EXPECT_EQ(packed[3].Color, 6u);
    // A missing service fails closed for boxes, not guide/handle geometry.
    ASSERT_TRUE(GizmoPass::Pack(frame, packed.data(), 3, &count));
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(packed[0].Color, 1u);
    EXPECT_EQ(packed[1].Color, 4u);
    EXPECT_EQ(packed[2].Color, 6u);
    EXPECT_FALSE(GizmoPass::Pack(frame, packed.data(), 2, &count));
    EXPECT_EQ(count, 3u);
}

TEST(GizmoPassTest, MalformedMeshRangesFailBeforeCopyPackingOrCallback)
{
    GizmoFrameData source = {}, destination = {};
    source.LineCount = 6;
    for (auto& line : source.Lines)
        line.Width = 1.f;
    source.MeshLineGroupCount                   = 1;
    const auto                          id      = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    const std::array<GizmoMeshLines, 5> invalid = {
        {{}, {.MeshUUID = id}, {.MeshUUID = id, .First = 7, .Count = 1}, {.MeshUUID = id, .First = 5, .Count = 2}, {.MeshUUID = id, .First = UINT32_MAX, .Count = UINT32_MAX}}
    };
    std::array<GizmoGPUInstance, 6> output = {};
    output[0].Color                        = 91;
    uint32_t count                         = 83;
    auto     reject                        = [&] {
        EXPECT_FALSE(source.ValidMeshLineGroups());
        destination = source;
        EXPECT_TRUE(destination.Empty());
        EXPECT_EQ(destination.MeshLineGroupCount, 0u);
        EXPECT_FALSE(GizmoPass::Pack(source, output.data(), output.size(), &count));
        EXPECT_EQ(output[0].Color, 91u);
        EXPECT_EQ(count, 83u);
    };
    for (const auto& range : invalid)
    {
        source.MeshLineGroups[0] = range;
        reject();
    }
    source.MeshLineGroups[0]  = {.MeshUUID = id, .First = 1, .Count = 3};
    source.MeshLineGroupCount = 2;
    source.MeshLineGroups[1]  = {.MeshUUID = id, .First = 2, .Count = 1};
    reject();
    source.MeshLineGroupCount = GizmoFrameData::MaxMeshLineGroups + 1;
    reject();
}

TEST(GizmoFrameTest, MeshRangeMetadataIsCopiedThroughMailboxAndInvalidPublicationPreservesReadyFrame)
{
    AppRenderPipeline pipeline = {};
    RenderFrameState  source = {}, read = {};
    source.Gizmo.LineCount = 3;
    for (uint32_t i = 0; i < 3; ++i)
        source.Gizmo.Lines[i] = {.Width = 1.f, .Color = i + 1};
    source.Gizmo.MeshLineGroupCount = 1;
    source.Gizmo.MeshLineGroups[0]  = {.MeshUUID = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111"), .First = 1, .Count = 1};
    ASSERT_TRUE(pipeline.PublishFrameState(source));
    source.Gizmo.MeshLineGroups[0].First = UINT32_MAX;
    EXPECT_FALSE(pipeline.PublishFrameState(source));
    ASSERT_TRUE(pipeline.TryReadFrameState(read));
    EXPECT_EQ(read.Gizmo.MeshLineGroupCount, 1u);
    EXPECT_EQ(read.Gizmo.MeshLineGroups[0].First, 1u);
    EXPECT_EQ(read.Gizmo.MeshLineGroups[0].Count, 1u);
    EXPECT_EQ(read.Gizmo.Lines[1].Color, 2u);
    pipeline.ReleaseFrameState();
}
