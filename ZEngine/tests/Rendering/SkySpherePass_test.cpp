#include <ZEngine/Rendering/Renderers/Graphics/SkySpherePass.h>
#include <gtest/gtest.h>

using namespace ZEngine::Rendering::Renderers;

TEST(SkySpherePassTest, PipelineDrawsOnlyAtTheBackgroundDepth)
{
    SkySpherePass pass = {};
    const auto    desc = pass.BuildGraphicsPipelineDescription(nullptr);

    EXPECT_STREQ(desc.DebugName, "Sky-Sphere-Pipeline");
    EXPECT_STREQ(desc.ShaderSpecificationValue.Name, "sky_sphere");
    EXPECT_TRUE(desc.EnableDepthTest);
    EXPECT_FALSE(desc.EnableDepthWrite);
    EXPECT_EQ(desc.DepthCompareOp, VK_COMPARE_OP_EQUAL);
    EXPECT_FALSE(desc.EnableBlending);
    EXPECT_EQ(desc.CullMode, VK_CULL_MODE_NONE);
    EXPECT_EQ(sizeof(SkySpherePushConstants), 80u);
}
