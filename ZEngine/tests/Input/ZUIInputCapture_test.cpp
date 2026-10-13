#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIInteraction.h>
#include <ZEngine/UI/ZUIWidgets.h>
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
    ctx.ViewportInputKey          = 42;

    const ZUIInputCapture capture = ZUIGetInputCapture(&ctx);
    EXPECT_FALSE(capture.Pointer);
    EXPECT_FALSE(capture.Keyboard);

    ctx.TextInputActive = true;
    EXPECT_TRUE(ZUIGetInputCapture(&ctx).Keyboard);
}

TEST(ZUIInputCaptureTest, MissingViewportCapturesPointer)
{
    ZUIContext            ctx     = {};

    const ZUIInputCapture capture = ZUIGetInputCapture(&ctx);
    EXPECT_TRUE(capture.Pointer);
    EXPECT_FALSE(capture.Keyboard);
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

TEST(ZUIInputCaptureTest, ExplicitPointerLeaseCapturesOnlyThePointerAndCannotBeStolen)
{
    ZUIContext ctx                     = {};
    ctx.ViewportInputKey               = 42;
    ctx.ViewportGeneration             = 7;

    const ZUIPointerCaptureToken token = ZUICapturePointer(&ctx, 100, 42, 7);
    ASSERT_NE(token, 0u);
    EXPECT_TRUE(ZUIHasPointerCapture(&ctx, token));
    EXPECT_TRUE(ZUIGetInputCapture(&ctx).Pointer);
    EXPECT_FALSE(ZUIGetInputCapture(&ctx).Keyboard);
    EXPECT_EQ(ZUICapturePointer(&ctx, 101, 42, 7), 0u);

    ASSERT_TRUE(ZUIReleasePointerCapture(&ctx, token));
    // A consumer must observe the terminal event before another native tool
    // can lease the pointer. This prevents a release from being mistaken for
    // the next tool's drag start.
    EXPECT_EQ(ZUICapturePointer(&ctx, 101, 42, 7), 0u);
    ZUIPointerCaptureEndReason reason = ZUIPointerCaptureEndReason::None;
    ASSERT_TRUE(ZUIConsumePointerCaptureEnd(&ctx, token, reason));
    EXPECT_EQ(reason, ZUIPointerCaptureEndReason::Released);
    EXPECT_FALSE(ZUIGetInputCapture(&ctx).Pointer);
    EXPECT_NE(ZUICapturePointer(&ctx, 101, 42, 7), 0u);
}

TEST(ZUIInputCaptureTest, EndOfFrameCancelsAViewportLeaseForEscapeOrViewportReplacement)
{
    ZEngine::Core::Memory::MemoryManager memory = {};
    memory.Initialize(ZMega(1), {});

    ZUIContext ctx = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZKilo(64), ZKilo(16), 64, 16);

    ZUIBeginFrame(&ctx, 1.f / 60.f);
    ZUIBox* root = ZUIBeginColumn(&ctx, "root", ZPx(100.f), ZPx(100.f));
    ZUIEndColumn(&ctx);
    ctx.ViewportInputKey                      = 42;
    ctx.ViewportGeneration                    = 3;
    const ZUIPointerCaptureToken escape_token = ZUICapturePointer(&ctx, 1, 42, 3);
    ASSERT_NE(escape_token, 0u);
    ctx.EscapePressed = true;
    ZUIEndFrame(&ctx);

    ZUIPointerCaptureEndReason reason = ZUIPointerCaptureEndReason::None;
    ASSERT_TRUE(ZUIConsumePointerCaptureEnd(&ctx, escape_token, reason));
    EXPECT_EQ(reason, ZUIPointerCaptureEndReason::Escape);

    ZUIBeginFrame(&ctx, 1.f / 60.f);
    root = ZUIBeginColumn(&ctx, "root", ZPx(100.f), ZPx(100.f));
    ZUIEndColumn(&ctx);
    ctx.ViewportInputKey                        = 42;
    ctx.ViewportGeneration                      = 4;
    const ZUIPointerCaptureToken viewport_token = ZUICapturePointer(&ctx, 1, 42, 4);
    ASSERT_NE(viewport_token, 0u);
    ctx.ViewportGeneration = 5;
    ZUIEndFrame(&ctx);

    ASSERT_TRUE(ZUIConsumePointerCaptureEnd(&ctx, viewport_token, reason));
    EXPECT_EQ(reason, ZUIPointerCaptureEndReason::ViewportUnavailable);
    ZUIContextDestroy(&ctx);
    memory.Shutdown();
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
