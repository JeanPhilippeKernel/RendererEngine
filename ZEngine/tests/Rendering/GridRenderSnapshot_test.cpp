#include <ZEngine/ECS/SceneGridSettings.h>
#include <ZEngine/Rendering/Renderers/Graphics/GridPass.h>
#include <ZEngine/Rendering/Renderers/RenderGraphTopology.h>
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <type_traits>

using namespace ZEngine;
using namespace ZEngine::ECS;
using namespace ZEngine::Rendering::Scenes;
using namespace ZEngine::Rendering::Renderers;

namespace
{
    SceneGridSettings AuthoredGrid()
    {
        SceneGridSettings value = {};
        value.Enabled           = false;
        value.Origin            = {2.f, -3.f, 7.f};
        value.Orientation       = {.5f, -.5f, .5f, .5f};
        value.BaseSpacing       = .125f;
        value.MajorLineEvery    = 7;
        value.MinorColor        = {.1f, .2f, .3f, .4f};
        value.MajorColor        = {.2f, .3f, .4f, .5f};
        value.UColor            = {.3f, .4f, .5f, .6f};
        value.VColor            = {.4f, .5f, .6f, .7f};
        value.LineWidth         = 3.f;
        value.FadeRadius        = 1024.f;
        value.FadeStrength      = 2.f;
        value.MaxLOD            = 9;
        value.Labels            = SceneGridLabels::UV;
        return value;
    }

    void ExpectSamePush(const GridPushConstantData& actual, const GridPushConstantData& expected)
    {
        EXPECT_EQ(actual.OriginAndLogSpacing, expected.OriginAndLogSpacing);
        EXPECT_EQ(actual.Orientation, expected.Orientation);
        EXPECT_EQ(actual.ColorThin, expected.ColorThin);
        EXPECT_EQ(actual.ColorThick, expected.ColorThick);
        EXPECT_EQ(actual.ColorUAxis, expected.ColorUAxis);
        EXPECT_EQ(actual.ColorVAxis, expected.ColorVAxis);
        EXPECT_EQ(actual.Style, expected.Style);
        EXPECT_EQ(actual.Controls, expected.Controls);
    }
} // namespace

TEST(SceneGridRenderSnapshotTest, DefaultsMatchTheAuthoredPlaneWithoutPointersOrUninitializedLanes)
{
    static_assert(std::is_trivially_copyable_v<SceneGridRenderSnapshot>);
    static_assert(std::is_trivially_copyable_v<GridPushConstantData>);
    const SceneGridRenderSnapshot snapshot = {};
    SceneGridSettings             value    = {};
    ASSERT_TRUE(ReadSceneGridRenderSnapshot(snapshot, value));
    EXPECT_TRUE(EqualSceneGridSettings(value, SceneGridSettings{}));
    EXPECT_FLOAT_EQ(snapshot.Origin.w, 0.f);
    const GridPushConstantData push = {};
    for (uint32_t i = 0; i < 4; ++i)
    {
        EXPECT_EQ(push.OriginAndLogSpacing[i], 0.f);
        EXPECT_EQ(push.Orientation[i], 0.f);
        EXPECT_EQ(push.ColorThin[i], 0.f);
        EXPECT_EQ(push.ColorThick[i], 0.f);
        EXPECT_EQ(push.ColorUAxis[i], 0.f);
        EXPECT_EQ(push.ColorVAxis[i], 0.f);
        EXPECT_EQ(push.Style[i], 0.f);
        EXPECT_EQ(push.Controls[i], 0u);
    }
    EXPECT_EQ(sizeof(push), 128u);
    EXPECT_EQ(offsetof(GridPushConstantData, OriginAndLogSpacing), 0u);
    EXPECT_EQ(offsetof(GridPushConstantData, Orientation), 16u);
    EXPECT_EQ(offsetof(GridPushConstantData, ColorThin), 32u);
    EXPECT_EQ(offsetof(GridPushConstantData, ColorThick), 48u);
    EXPECT_EQ(offsetof(GridPushConstantData, ColorUAxis), 64u);
    EXPECT_EQ(offsetof(GridPushConstantData, ColorVAxis), 80u);
    EXPECT_EQ(offsetof(GridPushConstantData, Style), 96u);
    EXPECT_EQ(offsetof(GridPushConstantData, Controls), 112u);
}

TEST(SceneGridRenderSnapshotTest, RoundTripsEveryAuthoredFieldAndCanonicalizesQuaternion)
{
    auto authored                    = AuthoredGrid();
    authored.Origin.w                = std::numeric_limits<float>::quiet_NaN();
    authored.Orientation             = {-1.f, 1.f, -1.f, -1.f};
    SceneGridRenderSnapshot snapshot = {};
    ASSERT_TRUE(MakeSceneGridRenderSnapshot(authored, snapshot));
    EXPECT_FLOAT_EQ(snapshot.Origin.w, 0.f);
    EXPECT_FLOAT_EQ(snapshot.Orientation.x, .5f);
    EXPECT_FLOAT_EQ(snapshot.Orientation.y, -.5f);
    EXPECT_FLOAT_EQ(snapshot.Orientation.z, .5f);
    EXPECT_FLOAT_EQ(snapshot.Orientation.w, .5f);
    SceneGridSettings restored = {};
    restored.Origin.w          = 19.f;
    ASSERT_TRUE(ReadSceneGridRenderSnapshot(snapshot, restored));
    EXPECT_TRUE(EqualSceneGridSettings(restored, AuthoredGrid()));
    EXPECT_FLOAT_EQ(restored.Origin.w, 0.f);
    // A value copy cannot follow later changes to the scene's authoring storage.
    authored.Origin = {70.f, 80.f, 90.f};
    authored.MajorColor.fill(0.f);
    EXPECT_FLOAT_EQ(snapshot.Origin.x, 2.f);
    EXPECT_FLOAT_EQ(snapshot.ColorThick[3], .5f);
}

TEST(SceneGridRenderSnapshotTest, InvalidAuthoringPreservesCompleteOutput)
{
    SceneGridRenderSnapshot output = {};
    ASSERT_TRUE(MakeSceneGridRenderSnapshot(AuthoredGrid(), output));
    for (uint32_t field = 0; field < 8; ++field)
    {
        SceneGridSettings invalid = {};
        switch (field)
        {
            case 0:
                invalid.Origin.z = std::numeric_limits<float>::infinity();
                break;
            case 1:
                invalid.Orientation = {0.f, 0.f, 0.f, 0.f};
                break;
            case 2:
                invalid.BaseSpacing = 0.f;
                break;
            case 3:
                invalid.MajorLineEvery = 0;
                break;
            case 4:
                invalid.MinorColor[2] = -1.f;
                break;
            case 5:
                invalid.FadeRadius = -1.f;
                break;
            case 6:
                invalid.MaxLOD = 17;
                break;
            case 7:
                invalid.Labels = SceneGridLabels(2);
                break;
        }
        EXPECT_FALSE(MakeSceneGridRenderSnapshot(invalid, output));
        SceneGridSettings retained = {};
        ASSERT_TRUE(ReadSceneGridRenderSnapshot(output, retained));
        EXPECT_TRUE(EqualSceneGridSettings(retained, AuthoredGrid()));
    }
}

TEST(GridPassPackingTest, PacksTheCompleteOrientedSnapshotWithinMinimumPushBudget)
{
    SceneGridRenderSnapshot snapshot = {};
    ASSERT_TRUE(MakeSceneGridRenderSnapshot(AuthoredGrid(), snapshot));
    for (bool reverse : {false, true})
    {
        GridPushConstantData push = {};
        ASSERT_TRUE(GridPass::Pack(snapshot, reverse, push));
        EXPECT_FLOAT_EQ(push.OriginAndLogSpacing[0], 2.f);
        EXPECT_FLOAT_EQ(push.OriginAndLogSpacing[1], -3.f);
        EXPECT_FLOAT_EQ(push.OriginAndLogSpacing[2], 7.f);
        EXPECT_FLOAT_EQ(push.OriginAndLogSpacing[3], -3.f);
        EXPECT_FLOAT_EQ(push.Orientation[0], .5f);
        EXPECT_FLOAT_EQ(push.Orientation[1], -.5f);
        EXPECT_FLOAT_EQ(push.Orientation[2], .5f);
        EXPECT_FLOAT_EQ(push.Orientation[3], .5f);
        EXPECT_EQ(push.ColorThin, snapshot.ColorThin);
        EXPECT_EQ(push.ColorThick, snapshot.ColorThick);
        EXPECT_EQ(push.ColorUAxis, snapshot.ColorUAxis);
        EXPECT_EQ(push.ColorVAxis, snapshot.ColorVAxis);
        EXPECT_FLOAT_EQ(push.Style[0], 10.f);
        EXPECT_FLOAT_EQ(push.Style[1], 2.f);
        EXPECT_FLOAT_EQ(push.Style[2], 3.f);
        EXPECT_FLOAT_EQ(push.Style[3], 9.f);
        EXPECT_EQ(push.Controls[0], 7u);
        EXPECT_EQ(push.Controls[1], reverse ? 1u : 0u);
        EXPECT_EQ(push.Controls[2], uint32_t(SceneGridLabels::UV));
        EXPECT_EQ(push.Controls[3], 0u);
    }
}

TEST(GridPassPackingTest, RejectsInvalidRuntimeValuesWithoutChangingAnyOutputField)
{
    SceneGridRenderSnapshot snapshot = {};
    ASSERT_TRUE(MakeSceneGridRenderSnapshot(AuthoredGrid(), snapshot));
    GridPushConstantData sentinel = {};
    ASSERT_TRUE(GridPass::Pack(snapshot, true, sentinel));
    for (uint32_t field = 0; field < 14; ++field)
    {
        SceneGridRenderSnapshot invalid = {};
        switch (field)
        {
            case 0:
                invalid.Origin.x = std::numeric_limits<float>::quiet_NaN();
                break;
            case 1:
                invalid.Orientation.w = std::numeric_limits<float>::infinity();
                break;
            case 2:
                invalid.Orientation = {0.f, 0.f, 0.f, 0.f};
                break;
            case 3:
                invalid.CellSize = 0.f;
                break;
            case 4:
                invalid.MajorLineEvery = 0;
                break;
            case 5:
                invalid.ColorThin[0] = -1.f;
                break;
            case 6:
                invalid.ColorThick[1] = 2.f;
                break;
            case 7:
                invalid.ColorUAxis[2] = std::numeric_limits<float>::quiet_NaN();
                break;
            case 8:
                invalid.ColorVAxis[3] = std::numeric_limits<float>::infinity();
                break;
            case 9:
                invalid.FadeRadius = -1.f;
                break;
            case 10:
                invalid.FadeStrength = -1.f;
                break;
            case 11:
                invalid.LineWidth = 0.f;
                break;
            case 12:
                invalid.MaxLOD = -1;
                break;
            case 13:
                invalid.Labels = UINT32_MAX;
                break;
        }
        auto output = sentinel;
        EXPECT_FALSE(GridPass::Pack(invalid, false, output));
        ExpectSamePush(output, sentinel);
        auto restored     = AuthoredGrid();
        restored.Origin.w = 79.f;
        EXPECT_FALSE(ReadSceneGridRenderSnapshot(invalid, restored));
        EXPECT_TRUE(EqualSceneGridSettings(restored, AuthoredGrid()));
        EXPECT_EQ(restored.Origin.w, 79.f);
    }
}

TEST(GridPassPackingTest, KeepsPositiveExtremeSpacingAndFadeFiniteInLogSpace)
{
    for (float positive : {std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::min(), .125f, std::numeric_limits<float>::max()})
    {
        SceneGridRenderSnapshot snapshot = {};
        snapshot.CellSize                = positive;
        snapshot.FadeRadius              = positive;
        snapshot.MajorLineEvery          = UINT32_MAX;
        snapshot.MaxLOD                  = 16;
        snapshot.FadeStrength            = 0.f;
        GridPushConstantData push        = {};
        ASSERT_TRUE(GridPass::Pack(snapshot, false, push));
        EXPECT_TRUE(std::isfinite(push.OriginAndLogSpacing[3]));
        EXPECT_TRUE(std::isfinite(push.Style[0]));
        EXPECT_FLOAT_EQ(push.OriginAndLogSpacing[3], float(std::log2(double(positive))));
        EXPECT_FLOAT_EQ(push.Style[0], push.OriginAndLogSpacing[3]);
        EXPECT_EQ(push.Controls[0], UINT32_MAX);
        EXPECT_EQ(push.Style[3], 16.f);
    }
}

class GridPassGraphTest : public testing::Test
{
protected:
    Core::Memory::ArenaAllocator Arena   = {};
    RenderGraph                  Graph   = {};
    RenderGraphResourceBuilder   Builder = {};
    GridPass                     Pass    = {};

    void                         SetUp() override
    {
        Arena.Initialize(ZMega(1), 0);
        Graph.FrameArena.Initialize(ZMega(1), 0);
        Graph.Resources.init(&Arena, 2);
        Graph.ResourceIndex.init(&Arena, 4);
        Graph.Passes.init(&Arena, 2);
        for (cstring name : {"Scene", "Grid"})
        {
            auto& pass = Graph.Passes.push_use({});
            pass.Name  = name;
            pass.Reads.init(&Arena, 2);
            pass.Writes.init(&Arena, 2);
        }
        Builder.Initialize(&Graph);
        Builder.CurrentPass = 0;
        Builder.WriteDepthAttachment(RendererResourceName::FrameDepthRenderTargetName, {.Width = 64, .Height = 64, .Format = Rendering::Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE});
        Builder.WriteColorAttachment(RendererResourceName::FrameColorRenderTargetName, {.Width = 64, .Height = 64, .Format = Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM});
        Builder.CurrentPass = 1;
    }
};

TEST_F(GridPassGraphTest, SamplesDepthAndLoadsColorWithoutDepthAttachmentOrWrite)
{
    ASSERT_TRUE(Pass.Register(nullptr, "Grid", {.RenderWidth = 64, .RenderHeight = 64}, &Builder, nullptr));
    const auto& grid = Graph.Passes[1];
    ASSERT_EQ(grid.Reads.size(), 1u);
    ASSERT_EQ(grid.Writes.size(), 1u);
    EXPECT_EQ(grid.Reads[0].Handle.Index, 0u);
    EXPECT_EQ(grid.Reads[0].Handle.Version, 1u);
    EXPECT_EQ(grid.Reads[0].Access, RGAccess::ShaderRead);
    EXPECT_STREQ(grid.Reads[0].BindingKey, "SceneDepth");
    EXPECT_EQ(grid.Writes[0].Handle.Index, 1u);
    EXPECT_EQ(grid.Writes[0].Handle.Version, 2u);
    EXPECT_EQ(grid.Writes[0].Access, RGAccess::ColorReadWrite);
    EXPECT_EQ(grid.Writes[0].LoadOp, Rendering::Specifications::LoadOperation::LOAD);
    EXPECT_TRUE(ValidatePassDeclarations(&Arena, Graph.Passes, Graph.Resources));
    Core::Containers::Array<uint32_t> order = {};
    order.init(&Arena, 2);
    ASSERT_TRUE(BuildPassTopology(&Arena, Graph.Passes, order, nullptr));
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
}

TEST_F(GridPassGraphTest, DisabledOrInvalidSnapshotRetiresTheOldPlaneAndCanRecover)
{
    SceneGridRenderSnapshot config = {};
    config.Enabled                 = false;
    Pass.SetConfig(config);
    EXPECT_FALSE(Pass.Register(nullptr, "Grid", {.RenderWidth = 64, .RenderHeight = 64}, &Builder, nullptr));
    config.Enabled  = true;
    config.CellSize = std::numeric_limits<float>::quiet_NaN();
    Pass.SetConfig(config);
    Pass.SetCameraDepthConvention(true);
    EXPECT_FALSE(Pass.Register(nullptr, "Grid", {.RenderWidth = 64, .RenderHeight = 64}, &Builder, nullptr));
    EXPECT_TRUE(Graph.Passes[1].Reads.empty());
    EXPECT_TRUE(Graph.Passes[1].Writes.empty());
    EXPECT_EQ(Graph.Resources[1].LatestVersion, 1u);
    config.CellSize = .25f;
    Pass.SetConfig(config);
    ASSERT_TRUE(Pass.Register(nullptr, "Grid", {.RenderWidth = 64, .RenderHeight = 64}, &Builder, nullptr));
    EXPECT_EQ(Graph.Passes[1].Reads.size(), 1u);
    EXPECT_EQ(Graph.Passes[1].Writes.size(), 1u);
}

TEST_F(GridPassGraphTest, MinimizedViewportAndMissingBuilderDeclareNothing)
{
    EXPECT_FALSE(Pass.Register(nullptr, "Grid", {.RenderWidth = 0, .RenderHeight = 64}, &Builder, nullptr));
    EXPECT_FALSE(Pass.Register(nullptr, "Grid", {.RenderWidth = 64, .RenderHeight = 0}, &Builder, nullptr));
    EXPECT_FALSE(Pass.Register(nullptr, "Grid", {.RenderWidth = 64, .RenderHeight = 64}, nullptr, nullptr));
    EXPECT_TRUE(Graph.Passes[1].Reads.empty());
    EXPECT_TRUE(Graph.Passes[1].Writes.empty());
    EXPECT_EQ(Graph.Resources[1].LatestVersion, 1u);
    // Normal/reverse depth is a uniform choice, never a mutable cached PSO key.
    const auto before = Pass.BuildGraphicsPipelineDescription(nullptr);
    Pass.SetCameraDepthConvention(true);
    const auto after = Pass.BuildGraphicsPipelineDescription(nullptr);
    EXPECT_EQ(before.EnableDepthTest, after.EnableDepthTest);
    EXPECT_EQ(before.EnableDepthWrite, after.EnableDepthWrite);
    EXPECT_EQ(before.DepthCompareOp, after.DepthCompareOp);
}
