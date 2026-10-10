#include <ZEngine/Rendering/Renderers/ZUIPass.h>
#include <gtest/gtest.h>

using namespace ZEngine::Rendering::Renderers;
using namespace ZEngine::UI;

namespace
{
    ZUIRenderPayload Payload(float width, float height)
    {
        ZUIRenderPayload payload{};
        payload.Scale[0]     = 2.f / width;
        payload.Scale[1]     = 2.f / height;
        payload.Translate[0] = payload.Translate[1] = -1.f;
        return payload;
    }

    ZUIDrawListCmd Clip(float x, float y, float width, float height)
    {
        ZUIDrawListCmd cmd{};
        cmd.ClipX = x;
        cmd.ClipY = y;
        cmd.ClipW = width;
        cmd.ClipH = height;
        return cmd;
    }
} // namespace

TEST(ZUIRenderScaleTest, ClipUsesActualSwapchainRatherThanWindowDpi)
{
    const auto payload = Payload(1982, 1211);
    const auto cmd     = Clip(280, 50, 100, 20);
    // Regression: window framebuffer=2478x1514, but old swapchain=1500x800.
    // The old 1.25 multiplier produced x=350; projected geometry starts at x=212.
    const auto rect    = payload.GetScissorRect(cmd, {1500, 800});
    EXPECT_EQ(rect.offset.x, 211);
    EXPECT_EQ(rect.offset.y, 33);
    EXPECT_EQ(rect.extent.width, 77u);
    EXPECT_EQ(rect.extent.height, 14u);
}

TEST(ZUIRenderScaleTest, FractionalDpiRoundsOutwardPerAxis)
{
    const auto rect = Payload(1000, 800).GetScissorRect(Clip(10.2f, 20.2f, 30.1f, 40.1f), {1250, 1001});
    EXPECT_EQ(rect.offset.x, 12);
    EXPECT_EQ(rect.offset.y, 25);
    EXPECT_EQ(rect.extent.width, 39u);
    EXPECT_EQ(rect.extent.height, 51u);
}

TEST(ZUIRenderScaleTest, NativeAndRetinaScissorsStayAligned)
{
    const auto payload = Payload(1500, 800);
    const auto cmd     = Clip(280, 50, 100, 20);
    const auto native  = payload.GetScissorRect(cmd, {1500, 800});
    EXPECT_EQ(native.offset.x, 280);
    EXPECT_EQ(native.offset.y, 50);
    EXPECT_EQ(native.extent.width, 100u);
    EXPECT_EQ(native.extent.height, 20u);

    const auto retina = payload.GetScissorRect(cmd, {3000, 1600});
    EXPECT_EQ(retina.offset.x, 560);
    EXPECT_EQ(retina.offset.y, 100);
    EXPECT_EQ(retina.extent.width, 200u);
    EXPECT_EQ(retina.extent.height, 40u);
}

TEST(ZUIRenderScaleTest, FullScreenClipCoversRoundedFractionalFramebuffer)
{
    const auto rect = Payload(1982, 1211).GetScissorRect(Clip(0, 0, 1982, 1211), {2478, 1514});
    EXPECT_EQ(rect.offset.x, 0);
    EXPECT_EQ(rect.offset.y, 0);
    EXPECT_EQ(rect.extent.width, 2478u);
    EXPECT_EQ(rect.extent.height, 1514u);
}

TEST(ZUIRenderScaleTest, ClampsPartiallyAndFullyOffscreenClips)
{
    const auto payload = Payload(100, 100);
    auto       rect    = payload.GetScissorRect(Clip(-10, -20, 50, 60), {200, 200});
    EXPECT_EQ(rect.offset.x, 0);
    EXPECT_EQ(rect.offset.y, 0);
    EXPECT_EQ(rect.extent.width, 80u);
    EXPECT_EQ(rect.extent.height, 80u);

    rect = payload.GetScissorRect(Clip(90, 90, 50, 60), {200, 200});
    EXPECT_EQ(rect.offset.x, 180);
    EXPECT_EQ(rect.offset.y, 180);
    EXPECT_EQ(rect.extent.width, 20u);
    EXPECT_EQ(rect.extent.height, 20u);

    rect = payload.GetScissorRect(Clip(110, 110, 10, 10), {200, 200});
    EXPECT_EQ(rect.extent.width, 0u);
    EXPECT_EQ(rect.extent.height, 0u);
    rect = payload.GetScissorRect(Clip(-50, -50, 10, 10), {200, 200});
    EXPECT_EQ(rect.extent.width, 0u);
    EXPECT_EQ(rect.extent.height, 0u);
}

TEST(ZUIRenderScaleTest, EmptyOrUninitializedPayloadDoesNotDraw)
{
    const auto payload = Payload(100, 100);
    EXPECT_EQ(payload.GetScissorRect(Clip(0, 0, 0, 10), {200, 200}).extent.width, 0u);
    EXPECT_EQ(payload.GetScissorRect(Clip(0, 0, 10, -1), {200, 200}).extent.height, 0u);
    EXPECT_EQ(payload.GetScissorRect(Clip(0, 0, 10, 10), {0, 0}).extent.width, 0u);
    EXPECT_EQ(ZUIRenderPayload{}.GetScissorRect(Clip(0, 0, 10, 10), {200, 200}).extent.width, 0u);
}
