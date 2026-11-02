#include <ZEngine/Rendering/Materials/MaterialAlpha.h>
#include <ZEngine/Rendering/Meshes/Mesh.h>
#include <ZEngine/Rendering/Renderers/Graphics/ObjectIdPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/TransparentPass.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstddef>

using namespace ZEngine::Rendering::Materials;

TEST(MaterialAlphaTest, ExplicitModesRoundTripWithoutGuessingLegacy)
{
    for (AlphaMode mode : {AlphaMode::Legacy, AlphaMode::Opaque, AlphaMode::Mask, AlphaMode::Blend})
        EXPECT_EQ(ParseAlphaMode(AlphaModeName(mode)), mode);
    EXPECT_EQ(ParseAlphaMode(""), AlphaMode::Legacy);
    EXPECT_EQ(ParseAlphaMode("unknown"), AlphaMode::Legacy);
    EXPECT_EQ(offsetof(ZEngine::Rendering::Meshes::MeshMaterial, AlphaMode), 116u);
    EXPECT_EQ(sizeof(ZEngine::Rendering::Meshes::MeshMaterial), 128u);
}

TEST(MaterialAlphaTest, ZeroAlphaOnlyMeansInvisibleInTheAppropriateMode)
{
    EXPECT_FALSE(IsConstantFullyTransparent(AlphaMode::Opaque, 0.f, .5f, false));
    EXPECT_TRUE(IsConstantFullyTransparent(AlphaMode::Blend, 0.f, .5f, false));
    for (float alpha : {.25f, .5f, 1.f})
        EXPECT_FALSE(IsConstantFullyTransparent(AlphaMode::Blend, alpha, .5f, false));
    EXPECT_TRUE(IsConstantFullyTransparent(AlphaMode::Mask, .49f, .5f, false));
    EXPECT_FALSE(IsConstantFullyTransparent(AlphaMode::Mask, .5f, .5f, false));
    EXPECT_FALSE(IsConstantFullyTransparent(AlphaMode::Mask, 0.f, 0.f, false));
    EXPECT_TRUE(IsConstantFullyTransparent(AlphaMode::Legacy, 0.f, .5f, false));
    EXPECT_TRUE(IsConstantFullyTransparent(AlphaMode::Blend, 0.f, .5f, true));
    EXPECT_TRUE(IsConstantFullyTransparent(AlphaMode::Mask, 0.f, .5f, true));
    EXPECT_FALSE(IsConstantFullyTransparent(AlphaMode::Mask, 0.f, 0.f, true));
    EXPECT_FALSE(IsConstantFullyTransparent(AlphaMode::Legacy, 0.f, .5f, true));
    EXPECT_FALSE(IsConstantFullyTransparent(AlphaMode::Opaque, 0.f, .5f, true));
    EXPECT_FALSE(IsConstantFullyTransparent(AlphaMode::Blend, NAN, .5f, false));
    EXPECT_FALSE(IsConstantFullyTransparent(AlphaMode::Mask, 0.f, NAN, false));
}

TEST(MaterialAlphaTest, TransparencyOrderIsFarToNearWithDeterministicTiesAndInvalidDepthLast)
{
    struct Draw
    {
        float    Depth;
        uint32_t Order;
    };
    std::array<Draw, 5> draws = {
        {{1, 0}, {9, 3}, {9, 1}, {NAN, 2}, {3, 4}}
    };
    std::sort(draws.begin(), draws.end(), [](const Draw& a, const Draw& b) { return TransparentDepthBefore(a.Depth, a.Order, b.Depth, b.Order); });
    const uint32_t expected[] = {1, 3, 4, 0, 2};
    for (uint32_t i = 0; i < draws.size(); ++i)
        EXPECT_EQ(draws[i].Order, expected[i]);
}

TEST(MaterialAlphaTest, BlendedScenePipelineTestsDepthWithoutWritingItAndUsesAlphaBlending)
{
    ZEngine::Rendering::Renderers::TransparentPass pass;
    const auto                                     desc = pass.BuildGraphicsPipelineDescription(nullptr);
    EXPECT_TRUE(desc.EnableBlending);
    EXPECT_TRUE(desc.EnableDepthTest);
    EXPECT_FALSE(desc.EnableDepthWrite);
    EXPECT_STREQ(desc.ShaderSpecificationValue.Name, "forward_transparent");
}

TEST(MaterialAlphaTest, SelectionHasIndependentDepthWithoutIntegerAttachmentBlending)
{
    ZEngine::Rendering::Renderers::ObjectIdPass pass;
    const auto                                  desc = pass.BuildGraphicsPipelineDescription(nullptr);
    EXPECT_TRUE(desc.EnableDepthTest);
    EXPECT_TRUE(desc.EnableDepthWrite);
    EXPECT_FALSE(desc.EnableBlending);
    EXPECT_STREQ(desc.ShaderSpecificationValue.Name, "object_id");
}
