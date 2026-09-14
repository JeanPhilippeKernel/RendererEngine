#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIInteraction.h>
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

TEST(ZUIInputCaptureTest, PortaledPopupInsideModalRemainsMouseInteractive)
{
    ZEngine::Core::Memory::MemoryManager memory = {};
    memory.Initialize(ZMega(1), {});

    ZUIContext ctx = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZKilo(64), ZKilo(16), 64, 16);

    ZUIBox root           = {};
    ZUIBox modal          = {};
    ZUIBox popup          = {};
    ZUIBox item           = {};

    root.FirstChild       = &modal;
    modal.Parent          = &root;
    modal.NextSib         = &popup;
    popup.Parent          = &root;
    popup.FirstChild      = &item;
    item.Parent           = &popup;
    item.Key              = 42;
    item.Flags            = ZUI_Clickable;

    root.ScreenMin[0]     = 0.f;
    root.ScreenMin[1]     = 0.f;
    root.ScreenMax[0]     = 100.f;
    root.ScreenMax[1]     = 100.f;
    modal.ScreenMin[0]    = 10.f;
    modal.ScreenMin[1]    = 10.f;
    modal.ScreenMax[0]    = 60.f;
    modal.ScreenMax[1]    = 60.f;
    popup.ScreenMin[0]    = 70.f;
    popup.ScreenMin[1]    = 10.f;
    popup.ScreenMax[0]    = 95.f;
    popup.ScreenMax[1]    = 60.f;
    item.ScreenMin[0]     = 70.f;
    item.ScreenMin[1]     = 10.f;
    item.ScreenMax[0]     = 95.f;
    item.ScreenMax[1]     = 30.f;

    ctx.Root              = &root;
    ctx.ModalBox          = &modal;
    ctx.PopupStackSize    = 1;
    ctx.PopupStack[0].Box = &popup;
    ctx.MousePos[0]       = 80.f;
    ctx.MousePos[1]       = 20.f;

    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.HotKey, item.Key);

    ZUIContextDestroy(&ctx);
    memory.Shutdown();
}
