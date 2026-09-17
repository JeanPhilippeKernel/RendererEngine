#include <ZEngine/Rendering/Renderers/Graphics/EnvironmentBackgroundPass.h>
#include <gtest/gtest.h>

using namespace ZEngine::Rendering::Renderers;

TEST(EnvironmentBackgroundPassTest, PipelineDrawsCubemapBackgroundAtTheFarDepth)
{
    EnvironmentBackgroundPass pass = {};
    const auto                desc = pass.BuildGraphicsPipelineDescription(nullptr);

    EXPECT_STREQ(desc.DebugName, "Environment-Background-Pipeline");
    EXPECT_STREQ(desc.ShaderSpecificationValue.Name, "environment_background");
    EXPECT_TRUE(desc.EnableDepthTest);
    EXPECT_FALSE(desc.EnableDepthWrite);
    EXPECT_EQ(desc.DepthCompareOp, VK_COMPARE_OP_EQUAL);
    EXPECT_FALSE(desc.EnableBlending);
    EXPECT_EQ(desc.CullMode, VK_CULL_MODE_NONE);
    EXPECT_EQ(sizeof(EnvironmentBackgroundPushConstants), 32u);
}
