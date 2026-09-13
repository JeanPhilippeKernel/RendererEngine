#include <ZEngine/UI/ZUIContext.h>
#include <gtest/gtest.h>

using namespace ZEngine::UI;

TEST(ZUIInputCaptureTest, SceneViewportPointerTargetRemainsAvailableToTheCamera)
{
    ZUIContext ctx                = {};
    ctx.ViewportInputKey          = 42;
    ctx.HotKey                    = 42;
    ctx.ActiveKey                 = 42;
    ctx.DragSourceKey             = 42;

    const ZUIInputCapture capture = ZUIGetInputCapture(&ctx);
    EXPECT_FALSE(capture.Pointer);
    EXPECT_FALSE(capture.Keyboard);
}

TEST(ZUIInputCaptureTest, OtherInteractiveElementsCaptureThePointer)
{
    ZUIContext ctx       = {};
    ctx.ViewportInputKey = 42;

    ctx.HotKey           = 99;
    EXPECT_TRUE(ZUIGetInputCapture(&ctx).Pointer);

    ctx.HotKey    = 0;
    ctx.ActiveKey = 99;
    EXPECT_TRUE(ZUIGetInputCapture(&ctx).Pointer);

    ctx.ActiveKey     = 0;
    ctx.DragSourceKey = 99;
    EXPECT_TRUE(ZUIGetInputCapture(&ctx).Pointer);
}

TEST(ZUIInputCaptureTest, TextInputCapturesOnlyKeyboardInput)
{
    ZUIContext ctx                = {};
    ctx.FocusKey                  = 7;

    const ZUIInputCapture capture = ZUIGetInputCapture(&ctx);
    EXPECT_FALSE(capture.Pointer);
    EXPECT_FALSE(capture.Keyboard);

    ctx.TextInputActive = true;
    EXPECT_TRUE(ZUIGetInputCapture(&ctx).Keyboard);
}

TEST(ZUIInputCaptureTest, PopupsAndModalsCaptureBothInputChannels)
{
    ZUIContext ctx          = {};
    ctx.PopupStackSize      = 1;

    ZUIInputCapture capture = ZUIGetInputCapture(&ctx);
    EXPECT_TRUE(capture.Pointer);
    EXPECT_TRUE(capture.Keyboard);

    ctx.PopupStackSize = 0;
    ctx.ActiveModalKey = 123;
    capture            = ZUIGetInputCapture(&ctx);
    EXPECT_TRUE(capture.Pointer);
    EXPECT_TRUE(capture.Keyboard);
}
