#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Core/Maths/Geometry.h>
#include <ZEngine/ECS/SceneGridSettings.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <ZEngine/Rendering/Renderers/Graphics/GridLabelPass.h>
#include <ZEngine/Rendering/Renderers/RenderGraphTopology.h>
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <type_traits>

using namespace ZEngine;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::Rendering::Cameras;
using namespace ZEngine::Rendering::Renderers;
namespace G = ZEngine::Core::Maths::Geometry;

namespace
{
    GridLabelFontSnapshot Font()
    {
        GridLabelFontSnapshot font = {};
        font.Atlas                 = {7, 3};
        font.LogicalViewportHeight = 600.f;
        font.Glyphs[0]             = {
            {.1f, .2f, .15f, .3f},
            {8.f, 11.f}
        };
        font.Glyphs[1] = {
            {.4f, .5f, .45f, .6f},
            {9.f, 10.f}
        };
        return font;
    }

    CameraFrameData CapturedCamera(bool orthographic = false, bool reverse = false, Vec3f position = {0.f, 0.f, 10.f})
    {
        FlyCamera camera(4.f / 3.f, {});
        camera.SetPosition(position);
        camera.SetOrientation(0.f, 0.f);
        if (orthographic)
            camera.SetProjectionType(CameraType::ORTHOGRAPHIC);
        auto frame = camera.CaptureFrameData();
        if (reverse)
            for (uint32_t column = 0; column < 4; ++column)
                frame.Projection(2, column) = frame.Projection(3, column) - frame.Projection(2, column);
        frame.UsesReverseZ = reverse;
        return frame;
    }

    Rendering::Scenes::SceneGridRenderSnapshot Grid()
    {
        ECS::SceneGridSettings settings                 = {};
        // U=+X, N=+Z, V=-Y. Both labels are visible from a -Z-facing camera.
        settings.Orientation                            = {std::sqrt(.5f), 0.f, 0.f, std::sqrt(.5f)};
        settings.Labels                                 = ECS::SceneGridLabels::UV;
        settings.FadeStrength                           = 0.f;
        settings.FadeRadius                             = 1000.f;
        Rendering::Scenes::SceneGridRenderSnapshot grid = {};
        EXPECT_TRUE(Rendering::Scenes::MakeSceneGridRenderSnapshot(settings, grid));
        return grid;
    }

    GridLabelFrameData Frame()
    {
        GridLabelFrameData frame = {};
        EXPECT_TRUE(BuildGridLabelFrame(Grid(), CapturedCamera(), Font(), 800, 600, frame));
        EXPECT_EQ(frame.Count, 2u);
        return frame;
    }

    void SameFrame(const GridLabelFrameData& a, const GridLabelFrameData& b)
    {
        EXPECT_EQ(a.Count, b.Count);
        EXPECT_EQ(a.Atlas.Index, b.Atlas.Index);
        EXPECT_EQ(a.Atlas.Generation, b.Atlas.Generation);
        EXPECT_EQ(a.Push.Rects, b.Push.Rects);
        EXPECT_EQ(a.Push.UVs, b.Push.UVs);
        EXPECT_EQ(a.Push.Colors, b.Push.Colors);
        EXPECT_EQ(a.Push.Depths, b.Push.Depths);
    }

    Vec2f Center(const GridLabelFrameData& frame, uint32_t label, float width = 800.f, float height = 600.f)
    {
        const auto& rect = frame.Push.Rects[label];
        return {(rect[0] + rect[2] * .5f + 1.f) * width * .5f, (rect[1] + rect[3] * .5f + 1.f) * height * .5f};
    }
} // namespace

TEST(GridLabelTest, DefaultsAreEmptyInitializedPointerFreeValuesWithinTheMinimumPushBudget)
{
    static_assert(std::is_trivially_copyable_v<GridLabelGlyph>);
    static_assert(std::is_trivially_copyable_v<GridLabelFontSnapshot>);
    static_assert(std::is_trivially_copyable_v<GridLabelFrameData>);
    const GridLabelFontSnapshot font  = {};
    const GridLabelFrameData    frame = {};
    EXPECT_FALSE(font.Atlas.Valid());
    EXPECT_EQ(font.LogicalViewportHeight, 0.f);
    EXPECT_EQ(font.Glyphs[0].Size.x, 0.f);
    EXPECT_EQ(font.Glyphs[1].UV[3], 0.f);
    EXPECT_EQ(frame.Count, 0u);
    EXPECT_TRUE(ValidGridLabelFrame(frame));
    EXPECT_EQ(sizeof(frame.Push), 112u);
    EXPECT_EQ(offsetof(GridLabelPushConstantData, UVs), 32u);
    EXPECT_EQ(offsetof(GridLabelPushConstantData, Colors), 64u);
    EXPECT_EQ(offsetof(GridLabelPushConstantData, Depths), 96u);
    for (uint32_t i = 0; i < 2; ++i)
        for (uint32_t lane = 0; lane < 4; ++lane)
        {
            EXPECT_EQ(frame.Push.Rects[i][lane], 0.f);
            EXPECT_EQ(frame.Push.UVs[i][lane], 0.f);
            EXPECT_EQ(frame.Push.Colors[i][lane], 0.f);
            EXPECT_EQ(frame.Push.Depths[lane], 0.f);
        }
}

TEST(GridLabelTest, UsesLocalUAndVNotTheNormalOrHardCodedWorldXZ)
{
    const auto frame = Frame();
    const auto grid  = Grid();
    const auto font  = Font();
    EXPECT_NEAR(Center(frame, 0).x, 400.f + GridLabelAxisOffset, .0001f);
    EXPECT_NEAR(Center(frame, 0).y, 300.f, .0001f);
    EXPECT_NEAR(Center(frame, 1).x, 400.f, .0001f);
    EXPECT_NEAR(Center(frame, 1).y, 300.f + GridLabelAxisOffset, .0001f);
    EXPECT_EQ(frame.Push.UVs[0], font.Glyphs[0].UV);
    EXPECT_EQ(frame.Push.UVs[1], font.Glyphs[1].UV);
    EXPECT_EQ(frame.Push.Colors[0], grid.ColorUAxis);
    EXPECT_EQ(frame.Push.Colors[1], grid.ColorVAxis);
    EXPECT_EQ(frame.Atlas.Index, font.Atlas.Index);
    EXPECT_EQ(frame.Atlas.Generation, font.Atlas.Generation);
}

TEST(GridLabelTest, PerspectiveOrthographicReverseDepthAndHighDpiKeepLogicalTextSize)
{
    for (bool orthographic : {false, true})
        for (bool reverse : {false, true})
            for (uint32_t scale : {1u, 2u, 3u})
            {
                const auto         camera = CapturedCamera(orthographic, reverse);
                const auto         font   = Font();
                GridLabelFrameData frame  = {};
                ASSERT_TRUE(BuildGridLabelFrame(Grid(), camera, font, 800 * scale, 600 * scale, frame));
                ASSERT_EQ(frame.Count, 2u);
                EXPECT_FLOAT_EQ(frame.Push.Depths[2], reverse ? 1.f : 0.f);
                EXPECT_NEAR(Center(frame, 0, 800.f * scale, 600.f * scale).x / scale, 400.f + GridLabelAxisOffset, .0001f);
                EXPECT_NEAR(Center(frame, 1, 800.f * scale, 600.f * scale).y / scale, 300.f + GridLabelAxisOffset, .0001f);
                for (uint32_t i = 0; i < 2; ++i)
                {
                    EXPECT_NEAR(frame.Push.Rects[i][2] * 400.f, font.Glyphs[i].Size.x, .00001f);
                    EXPECT_NEAR(frame.Push.Rects[i][3] * 300.f, font.Glyphs[i].Size.y, .00001f);
                    G::ProjectionFrame projection = {};
                    ASSERT_TRUE(G::MakeProjectionFrame(camera.View, camera.Projection, camera.Position, orthographic ? G::ProjectionKind::Orthographic : G::ProjectionKind::Perspective, reverse, &projection));
                    G::ProjectedPoint origin = {};
                    ASSERT_TRUE(
                        G::Project(
                            {
                    },
                            projection,
                            {.Size = {800.f, 600.f}},
                            &origin));
                    EXPECT_FLOAT_EQ(frame.Push.Depths[i], origin.Depth);
                }
            }
}

TEST(GridLabelTest, TranslatedRotatedPlaneUsesTheSameCapturedCameraForAnchorProjection)
{
    auto grid                     = Grid();
    grid.Origin                   = {1.f, -2.f, -3.f};
    grid.Orientation              = {.5f, .5f, .5f, .5f};
    auto               camera     = CapturedCamera();
    G::ProjectionFrame projection = {};
    ASSERT_TRUE(G::MakeProjectionFrame(camera.View, camera.Projection, camera.Position, G::ProjectionKind::Perspective, false, &projection));
    float wpp = 0.f;
    ASSERT_TRUE(G::WorldPerPixel(projection, grid.Origin, 600.f, &wpp));
    const Core::Containers::FixedArray<Vec3f, 2> axes  = {Vec3f(0.f, 1.f, 0.f), Vec3f(1.f, 0.f, 0.f)};
    GridLabelFrameData                           frame = {};
    ASSERT_TRUE(BuildGridLabelFrame(grid, camera, Font(), 800, 600, frame));
    ASSERT_EQ(frame.Count, 2u);
    for (uint32_t i = 0; i < 2; ++i)
    {
        G::ProjectedPoint expected = {};
        ASSERT_TRUE(
            G::Project(
                grid.Origin + axes[i] * (wpp * GridLabelAxisOffset),
                projection,
                {
                .Size = {800.f, 600.f}
        },
                &expected));
        EXPECT_NEAR(Center(frame, i).x, expected.Screen.x, .0001f);
        EXPECT_NEAR(Center(frame, i).y, expected.Screen.y, .0001f);
        EXPECT_FLOAT_EQ(frame.Push.Depths[i], expected.Depth);
    }
}

TEST(GridLabelTest, DefaultPlaneVAnchorUsesPositiveZAndNotPositiveY)
{
    auto grid        = Grid();
    grid.Orientation = {};
    auto      camera = CapturedCamera();
    FlyCamera tilted(4.f / 3.f, {});
    tilted.SetPosition({0.f, 6.f, 10.f});
    tilted.SetOrientation(30.f, 0.f);
    camera                   = tilted.CaptureFrameData();
    GridLabelFrameData frame = {};
    ASSERT_TRUE(BuildGridLabelFrame(grid, camera, Font(), 800, 600, frame));
    ASSERT_EQ(frame.Count, 2u);
    G::ProjectionFrame projection = {};
    ASSERT_TRUE(G::MakeProjectionFrame(camera.View, camera.Projection, camera.Position, G::ProjectionKind::Perspective, false, &projection));
    float wpp = 0.f;
    ASSERT_TRUE(G::WorldPerPixel(projection, {}, 600.f, &wpp));
    G::ProjectedPoint expected = {};
    ASSERT_TRUE(
        G::Project(
            {
            0.f, 0.f, wpp * GridLabelAxisOffset
    },
            projection,
            {.Size = {800.f, 600.f}},
            &expected));
    EXPECT_NEAR(Center(frame, 1).x, expected.Screen.x, .0001f);
    EXPECT_NEAR(Center(frame, 1).y, expected.Screen.y, .0001f);
    EXPECT_FLOAT_EQ(frame.Push.Depths[1], expected.Depth);
}

TEST(GridLabelTest, FadeUsesConfiguredLocalPlaneDistanceAndAuthoredAxisAlpha)
{
    auto grid                     = Grid();
    grid.FadeRadius               = 10.f;
    grid.FadeStrength             = 2.f;
    grid.ColorUAxis[3]            = .6f;
    grid.ColorVAxis[3]            = .4f;
    const auto         camera     = CapturedCamera();
    G::ProjectionFrame projection = {};
    ASSERT_TRUE(G::MakeProjectionFrame(camera.View, camera.Projection, camera.Position, G::ProjectionKind::Perspective, false, &projection));
    float wpp = 0.f;
    ASSERT_TRUE(G::WorldPerPixel(projection, {}, 600.f, &wpp));
    const double       falloff = std::pow(1.0 - wpp * GridLabelAxisOffset / grid.FadeRadius, 2.0);
    GridLabelFrameData frame   = {};
    ASSERT_TRUE(BuildGridLabelFrame(grid, camera, Font(), 800, 600, frame));
    ASSERT_EQ(frame.Count, 2u);
    EXPECT_NEAR(frame.Push.Colors[0][3], .6 * falloff, .000001);
    EXPECT_NEAR(frame.Push.Colors[1][3], .4 * falloff, .000001);
    // Camera distance normal to the plane is ten: full world-distance fade
    // would incorrectly hide both labels at this radius.
    EXPECT_GT(frame.Push.Colors[0][3], 0.f);
}

TEST(GridLabelTest, TransparentAxisCompactsTheOtherGlyphWithoutReusingItsDepthOrColor)
{
    auto grid                = Grid();
    grid.ColorUAxis[3]       = 0.f;
    GridLabelFrameData frame = {};
    ASSERT_TRUE(BuildGridLabelFrame(grid, CapturedCamera(), Font(), 800, 600, frame));
    ASSERT_EQ(frame.Count, 1u);
    EXPECT_EQ(frame.Push.UVs[0], Font().Glyphs[1].UV);
    EXPECT_EQ(frame.Push.Colors[0], grid.ColorVAxis);
    EXPECT_NEAR(Center(frame, 0).y, 300.f + GridLabelAxisOffset, .0001f);
    EXPECT_EQ(frame.Push.Rects[1], (Core::Containers::FixedArray<float, 4>{}));
    EXPECT_EQ(frame.Push.Depths[1], 0.f);
}

TEST(GridLabelTest, FullyOffscreenHiddenAndFarClippedLabelsAreNotClampedToTheViewport)
{
    for (const Vec3f origin : {Vec3f(100.f, 0.f, 0.f), Vec3f(0.f, 0.f, 12.f), Vec3f(0.f, 0.f, 9.85f), Vec3f(0.f, 0.f, -20000.f)})
    {
        auto grid       = Grid();
        grid.Origin     = origin;
        grid.FadeRadius = std::numeric_limits<float>::max();
        auto frame      = Frame();
        ASSERT_TRUE(BuildGridLabelFrame(grid, CapturedCamera(), Font(), 800, 600, frame));
        EXPECT_EQ(frame.Count, 0u);
        EXPECT_FALSE(frame.Atlas.Valid());
    }
}

TEST(GridLabelTest, PartiallyOffscreenTextKeepsItsOriginalRectangleForRasterClipping)
{
    auto               grid       = Grid();
    const auto         camera     = CapturedCamera(true);
    G::ProjectionFrame projection = {};
    ASSERT_TRUE(G::MakeProjectionFrame(camera.View, camera.Projection, camera.Position, G::ProjectionKind::Orthographic, false, &projection));
    float wpp = 0.f;
    ASSERT_TRUE(G::WorldPerPixel(projection, {}, 600.f, &wpp));
    grid.Origin.x            = (400.f - GridLabelAxisOffset - 2.f) * wpp;
    GridLabelFrameData frame = {};
    ASSERT_TRUE(BuildGridLabelFrame(grid, camera, Font(), 800, 600, frame));
    ASSERT_EQ(frame.Count, 2u);
    EXPECT_NEAR(Center(frame, 0).x, 798.f, .0001f);
    EXPECT_GT(frame.Push.Rects[0][0] + frame.Push.Rects[0][2], 1.f);
}

TEST(GridLabelTest, DisabledMissingFontAndMinimizedStatesProduceAnEmptyFrame)
{
    for (uint32_t mode = 0; mode < 5; ++mode)
    {
        auto     grid  = Grid();
        auto     font  = Font();
        uint32_t width = 800, height = 600;
        if (mode == 0)
            grid.Enabled = false;
        if (mode == 1)
            grid.Labels = 0;
        if (mode == 2)
            font = {};
        if (mode == 3)
            width = 0;
        if (mode == 4)
            height = 0;
        auto frame = Frame();
        ASSERT_TRUE(BuildGridLabelFrame(grid, CapturedCamera(), font, width, height, frame));
        SameFrame(frame, {});
    }
}

TEST(GridLabelTest, InvalidFontMetricsAndUVsPreserveTheCompleteOutput)
{
    const auto sentinel = Frame();
    for (uint32_t field = 0; field < 9; ++field)
    {
        auto font = Font();
        if (field == 0)
            font.LogicalViewportHeight = 0.f;
        if (field == 1)
            font.LogicalViewportHeight = std::numeric_limits<float>::quiet_NaN();
        if (field == 2)
            font.Glyphs[0].Size.x = -1.f;
        if (field == 3)
            font.Glyphs[1].Size.y = std::numeric_limits<float>::infinity();
        if (field == 4)
            font.Glyphs[0].UV[0] = -.1f;
        if (field == 5)
            font.Glyphs[1].UV[3] = 1.1f;
        if (field == 6)
            font.Glyphs[0].UV[2] = font.Glyphs[0].UV[0];
        if (field == 7)
            font.Glyphs[1].UV[1] = std::numeric_limits<float>::quiet_NaN();
        if (field == 8)
            font.Glyphs[1].UV[3] = font.Glyphs[1].UV[1];
        auto frame = sentinel;
        EXPECT_FALSE(BuildGridLabelFrame(Grid(), CapturedCamera(), font, 800, 600, frame)) << field;
        SameFrame(frame, sentinel);
    }
}

TEST(GridLabelTest, InvalidGridAndCameraPreserveTheCompleteOutput)
{
    const auto sentinel = Frame();
    for (uint32_t field = 0; field < 7; ++field)
    {
        auto grid   = Grid();
        auto camera = CapturedCamera();
        if (field == 0)
            grid.Labels = 2;
        if (field == 1)
            grid.Orientation = {0.f, 0.f, 0.f, 0.f};
        if (field == 2)
            grid.FadeRadius = 0.f;
        if (field == 3)
            camera.ProjectionType = CameraType::UNDEFINED;
        if (field == 4)
            camera.Projection(1, 1) = std::numeric_limits<float>::quiet_NaN();
        if (field == 5)
            camera.Position.x = std::numeric_limits<float>::infinity();
        if (field == 6)
            camera.Projection(2, 3) = 0.f;
        auto frame = sentinel;
        EXPECT_FALSE(BuildGridLabelFrame(grid, camera, Font(), 800, 600, frame)) << field;
        SameFrame(frame, sentinel);
    }
}

TEST(GridLabelTest, UnrepresentableFontScaleRejectsBeforeFloatConversionAndPreservesOutput)
{
    const auto sentinel        = Frame();
    auto       grid            = Grid();
    grid.FadeRadius            = std::numeric_limits<float>::max();
    auto font                  = Font();
    font.LogicalViewportHeight = 1.0e-20f;
    font.Glyphs[0].Size        = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    auto frame                 = sentinel;
    ASSERT_FALSE(BuildGridLabelFrame(grid, CapturedCamera(true), font, 800, 600, frame));
    SameFrame(frame, sentinel);
    font                       = Font();
    font.LogicalViewportHeight = std::numeric_limits<float>::max();
    font.Glyphs[0].Size        = {std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::denorm_min()};
    ASSERT_FALSE(BuildGridLabelFrame(grid, CapturedCamera(true), font, 800, 600, frame));
    SameFrame(frame, sentinel);
}

TEST(GridLabelTest, ExtremeSpacingAndCadenceDoNotMoveLabelsOrIntroduceInvalidNumbers)
{
    const auto expected = Frame();
    for (float spacing : {std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::min(), .1f, std::numeric_limits<float>::max()})
    {
        auto grid                = Grid();
        grid.CellSize            = spacing;
        grid.MajorLineEvery      = UINT32_MAX;
        grid.MaxLOD              = 16;
        GridLabelFrameData frame = {};
        ASSERT_TRUE(BuildGridLabelFrame(grid, CapturedCamera(), Font(), 800, 600, frame));
        SameFrame(frame, expected);
        grid.FadeRadius = spacing;
        ASSERT_TRUE(BuildGridLabelFrame(grid, CapturedCamera(), Font(), 800, 600, frame));
        EXPECT_TRUE(ValidGridLabelFrame(frame));
    }
}

TEST(GridLabelTest, RejectsForgedFramesBeforeAnyGpuWork)
{
    const auto valid = Frame();
    for (uint32_t field = 0; field < 9; ++field)
    {
        auto frame = valid;
        if (field == 0)
            frame.Count = 3;
        if (field == 1)
            frame.Atlas = {};
        if (field == 2)
            frame.Push.Rects[0][2] = 0.f;
        if (field == 3)
            frame.Push.UVs[1][2] = frame.Push.UVs[1][0];
        if (field == 4)
            frame.Push.Colors[0][3] = 2.f;
        if (field == 5)
            frame.Push.Depths[0] = -1.f;
        if (field == 6)
            frame.Push.Depths[2] = .5f;
        if (field == 7)
            frame.Push.Depths[3] = 1.f;
        if (field == 8)
            frame.Push.Rects[1][0] = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(ValidGridLabelFrame(frame)) << field;
    }
}

TEST(GridLabelTest, RetainedMailboxFontAndPlaneDoNotFollowLaterMainThreadChanges)
{
    Core::Memory::ArenaAllocator arena = {};
    arena.Initialize(ZMega(4), 0);
    Applications::AppRenderPipeline pipeline = {};
    pipeline.InitializeFrameStorage(&arena, 1);
    Rendering::Scenes::RenderScene scene = {};
    scene.Instances.init(&arena, 1);
    scene.Grid                           = Grid();
    Applications::RenderFrameState state = {};
    state.Camera                         = CapturedCamera();
    state.GridLabelFont                  = Font();
    state.RenderTargetW                  = 800;
    state.RenderTargetH                  = 600;
    ASSERT_TRUE(pipeline.PublishFrameState(state, &scene));
    Applications::RenderFrameState retained = {};
    ASSERT_TRUE(pipeline.TryReadFrameState(retained));
    const auto expected     = Frame();
    const auto arena_offset = arena.m_current_offset;
    for (uint32_t i = 0; i < 512; ++i)
    {
        scene.Grid.Origin.x                  = float(i + 1000);
        state.GridLabelFont.Glyphs[0].Size.x = float(i + 100);
        state.GridLabelFont.Atlas.Generation = i + 100;
        state.Camera.Position.z              = float(i + 20);
        ASSERT_TRUE(pipeline.PublishFrameState(state, &scene));
        GridLabelFrameData frame = {};
        ASSERT_TRUE(BuildGridLabelFrame(retained.Scene.Grid, retained.Camera, retained.GridLabelFont, retained.RenderTargetW, retained.RenderTargetH, frame));
        SameFrame(frame, expected);
    }
    EXPECT_EQ(arena.m_current_offset, arena_offset);
    pipeline.ReleaseFrameState();
    arena.Shutdown();
}

class GridLabelGraphTest : public testing::Test
{
protected:
    Core::Memory::ArenaAllocator Arena   = {};
    RenderGraph                  Graph   = {};
    RenderGraphResourceBuilder   Builder = {};
    GridLabelPass                Pass    = {};

    void                         SetUp() override
    {
        Arena.Initialize(ZMega(1), 0);
        Graph.FrameArena.Initialize(ZMega(1), 0);
        Graph.Resources.init(&Arena, 4);
        Graph.ResourceIndex.init(&Arena, 8);
        Graph.ImportedResources.init(&Arena, 2);
        Graph.ImportedResourceIndex.init(&Arena, 4);
        Graph.Passes.init(&Arena, 2);
        for (cstring name : {"Scene", "Grid Labels"})
        {
            auto& pass = Graph.Passes.push_use({});
            pass.Name  = name;
            pass.Reads.init(&Arena, 4);
            pass.Writes.init(&Arena, 2);
        }
        Builder.Initialize(&Graph);
        Builder.CurrentPass = 0;
        Builder.WriteDepthAttachment(RendererResourceName::FrameDepthRenderTargetName, {.Width = 64, .Height = 64, .Format = Rendering::Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE});
        Builder.WriteColorAttachment(RendererResourceName::FrameColorRenderTargetName, {.Width = 64, .Height = 64, .Format = Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM});
        Builder.CurrentPass = 1;
    }

    void TearDown() override
    {
        Graph.FrameArena.Shutdown();
        Arena.Shutdown();
    }
};

TEST_F(GridLabelGraphTest, DeclaresAtlasAndSceneDepthReadsAndLoadsColorWithoutDepthWrites)
{
    Pass.SetFrame(Frame());
    ASSERT_TRUE(Pass.Register(nullptr, "Grid Labels", {.RenderWidth = 64, .RenderHeight = 64}, &Builder, nullptr));
    const auto& labels = Graph.Passes[1];
    ASSERT_EQ(labels.Reads.size(), 2u);
    ASSERT_EQ(labels.Writes.size(), 1u);
    EXPECT_STREQ(labels.Reads[0].BindingKey, "FontAtlas");
    EXPECT_EQ(labels.Reads[0].Access, RGAccess::ShaderRead);
    const auto& atlas = Graph.Resources[labels.Reads[0].Handle.Index];
    EXPECT_TRUE(atlas.External);
    EXPECT_FALSE(atlas.Transient);
    EXPECT_EQ(atlas.TextureHandle.Index, Font().Atlas.Index);
    EXPECT_EQ(atlas.TextureHandle.Generation, Font().Atlas.Generation);
    EXPECT_STREQ(labels.Reads[1].BindingKey, "SceneDepth");
    EXPECT_EQ(labels.Reads[1].Handle.Index, 0u);
    EXPECT_EQ(labels.Reads[1].Access, RGAccess::ShaderRead);
    EXPECT_EQ(labels.Writes[0].Handle.Index, 1u);
    EXPECT_EQ(labels.Writes[0].LoadOp, Rendering::Specifications::LoadOperation::LOAD);
    EXPECT_EQ(labels.Writes[0].Access, RGAccess::ColorReadWrite);
    EXPECT_TRUE(ValidatePassDeclarations(&Arena, Graph.Passes, Graph.Resources));
    Core::Containers::Array<uint32_t> order = {};
    order.init(&Arena, 2);
    ASSERT_TRUE(BuildPassTopology(&Arena, Graph.Passes, order, nullptr));
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
}

TEST_F(GridLabelGraphTest, EmptyInvalidAndMinimizedFramesDeclareNothingAndCanRecover)
{
    EXPECT_FALSE(Pass.Register(nullptr, "Grid Labels", {.RenderWidth = 64, .RenderHeight = 64}, &Builder, nullptr));
    Pass.SetFrame(Frame());
    auto invalid           = Frame();
    invalid.Push.Depths[0] = std::numeric_limits<float>::quiet_NaN();
    Pass.SetFrame(invalid);
    EXPECT_FALSE(Pass.Register(nullptr, "Grid Labels", {.RenderWidth = 64, .RenderHeight = 64}, &Builder, nullptr));
    Pass.SetFrame(Frame());
    EXPECT_FALSE(Pass.Register(nullptr, "Grid Labels", {.RenderWidth = 0, .RenderHeight = 64}, &Builder, nullptr));
    EXPECT_FALSE(Pass.Register(nullptr, "Grid Labels", {.RenderWidth = 64, .RenderHeight = 0}, &Builder, nullptr));
    EXPECT_FALSE(Pass.Register(nullptr, "Grid Labels", {.RenderWidth = 64, .RenderHeight = 64}, nullptr, nullptr));
    EXPECT_TRUE(Graph.Passes[1].Reads.empty());
    EXPECT_TRUE(Graph.Passes[1].Writes.empty());
    EXPECT_TRUE(Graph.ImportedResources.empty());
    ASSERT_TRUE(Pass.Register(nullptr, "Grid Labels", {.RenderWidth = 64, .RenderHeight = 64}, &Builder, nullptr));
}

TEST_F(GridLabelGraphTest, PipelineUsesOneAtlasShaderAndUniformDepthConventionWithoutGeometryBuffers)
{
    const auto desc = Pass.BuildGraphicsPipelineDescription(nullptr);
    EXPECT_STREQ(desc.ShaderSpecificationValue.Name, "grid_labels");
    EXPECT_FALSE(desc.EnableDepthTest);
    EXPECT_FALSE(desc.EnableDepthWrite);
    EXPECT_TRUE(desc.EnableBlending);
    EXPECT_EQ(desc.CullMode, VK_CULL_MODE_NONE);
    EXPECT_TRUE(Pass.SupportsSecondaryRecording());
    Pass.SetFrame({});
    EXPECT_FALSE(Pass.RecordDraw(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));
}
