#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIInput.h>
#include <ZEngine/UI/ZUIInteraction.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <gtest/gtest.h>

using namespace ZEngine::UI;

TEST(ZUIInputCaptureTest, FocusLossRetiresEveryPhysicalKeyButPreservesWidgetAndSessionState)
{
    ZUIContext ctx                     = {};
    bool ZUIContext::* physical_keys[] = {
        &ZUIContext::CtrlDown,   &ZUIContext::ShiftDown,    &ZUIContext::AltDown,      &ZUIContext::BackspacePressed,     &ZUIContext::BackspaceHeld, &ZUIContext::TabPressed,   &ZUIContext::ShiftTabPressed, &ZUIContext::EscapePressed, &ZUIContext::EnterPressed, &ZUIContext::SpacePressed,  &ZUIContext::ArrowUpPressed, &ZUIContext::ArrowDownPressed, &ZUIContext::ArrowLeftPressed, &ZUIContext::ArrowRightPressed, &ZUIContext::HomePressed,
        &ZUIContext::EndPressed, &ZUIContext::CtrlCPressed, &ZUIContext::CtrlXPressed, &ZUIContext::CtrlBackspacePressed, &ZUIContext::CtrlAPressed,  &ZUIContext::CtrlZPressed, &ZUIContext::CtrlYPressed,    &ZUIContext::DeletePressed, &ZUIContext::F2Pressed,    &ZUIContext::ArrowLeftHeld, &ZUIContext::ArrowRightHeld, &ZUIContext::ArrowUpHeld,      &ZUIContext::ArrowDownHeld,    &ZUIContext::DeleteHeld,
    };
    for (auto key : physical_keys)
        ctx.*key = true;
    ctx.KeyRepeatTimer = ctx.ArrowRepeatTimer = 1.f;
    ctx.ScrollDelta                           = 1.f;
    ctx.HotKey = ctx.ActiveKey = ctx.FocusKey = 42;
    ctx.TextInputActive                       = true;
    ctx.UndoFieldKey                          = 42;
    ctx.UndoTop                               = 1;
    ctx.UndoStack[0].Buf[0]                   = 'x';
    ctx.InteractionCapture                    = {true, true};
    ctx.PopupStackSize                        = 1;
    ctx.ActiveModalKey                        = 99;
    ctx.TextInputLen                          = 1;
    ctx.TextInput[0]                          = 'a';
    ctx.TextInput[31]                         = 't';
    ctx.DragPayload[511]                      = 'p';
    ZUIFeedFocusLost(&ctx);
    for (auto key : physical_keys)
        EXPECT_FALSE(ctx.*key);
    EXPECT_FLOAT_EQ(ctx.KeyRepeatTimer, 0.f);
    EXPECT_FLOAT_EQ(ctx.ArrowRepeatTimer, 0.f);
    EXPECT_FLOAT_EQ(ctx.ScrollDelta, 0.f);
    EXPECT_EQ(ctx.HotKey, 0u);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_EQ(ctx.TextInputLen, 0u);
    EXPECT_EQ(ctx.TextInput[0], '\0');
    EXPECT_EQ(ctx.TextInput[31], 't'); // unused buffer capacity is not cleared
    EXPECT_EQ(ctx.DragPayload[511], 'p');
    EXPECT_EQ(ctx.FocusKey, 42u);
    EXPECT_TRUE(ctx.TextInputActive);
    EXPECT_EQ(ctx.UndoFieldKey, 42u);
    EXPECT_EQ(ctx.UndoTop, 1);
    EXPECT_EQ(ctx.UndoStack[0].Buf[0], 'x');
    EXPECT_TRUE(ctx.InteractionCapture.Pointer);
    EXPECT_TRUE(ctx.InteractionCapture.Keyboard);
    EXPECT_EQ(ctx.PopupStackSize, 1u);
    EXPECT_EQ(ctx.ActiveModalKey, 99u);
}

TEST(ZUIInputCaptureTest, FocusLossIsIdempotentAndKeepsPointerSerialsMonotonicAcrossSyntheticReleases)
{
    ZUIFeedFocusLost(nullptr);
    ZUIContext ctx = {};
    ctx.CtrlDown = ctx.ShiftDown = ctx.AltDown = true;
    for (int button = 0; button < 3; ++button)
    {
        ZUIFeedMouseButton(&ctx, button, true);
        ctx.MouseReleaseSerial[button] = 17;
        ctx.MouseReleaseCtrl[button] = ctx.MouseReleaseShift[button] = true;
    }
    ctx.DragSourceKey = ctx.DragTargetKey = 42;
    ctx.DragDropFired                     = true;
    ctx.DragPayloadKind                   = ZUIDragPayloadKind::VFSPath;
    ctx.DragPayloadLen                    = 4;
    ctx.DragPayload[0]                    = 't';
    ZUIFeedFocusLost(&ctx);
    ZUIFeedFocusLost(&ctx);
    for (int button = 0; button < 3; ++button)
    {
        EXPECT_EQ(ctx.MousePressSerial[button], 1u);
        EXPECT_EQ(ctx.MouseReleaseSerial[button], 17u);
        EXPECT_FALSE(ctx.MouseDown[button]);
        EXPECT_FALSE(ctx.MousePressed[button]);
        EXPECT_FALSE(ctx.MouseReleased[button]);
        EXPECT_FALSE(ctx.MousePressCtrl[button]);
        EXPECT_FALSE(ctx.MousePressShift[button]);
        EXPECT_FALSE(ctx.MousePressAlt[button]);
        EXPECT_FALSE(ctx.MouseReleaseCtrl[button]);
        EXPECT_FALSE(ctx.MouseReleaseShift[button]);
        ZUIFeedMouseButton(&ctx, button, false); // synthetic blur release
        EXPECT_FALSE(ctx.MouseReleased[button]);
        ZUIFeedMouseButton(&ctx, button, true); // genuine new gesture
        EXPECT_EQ(ctx.MousePressSerial[button], 2u);
        EXPECT_TRUE(ctx.MousePressed[button]);
    }
    EXPECT_EQ(ctx.DragSourceKey, 0u);
    EXPECT_EQ(ctx.DragTargetKey, 0u);
    EXPECT_FALSE(ctx.DragDropFired);
    EXPECT_EQ(ctx.DragPayloadKind, ZUIDragPayloadKind::Opaque);
    EXPECT_EQ(ctx.DragPayloadLen, 0u);
    EXPECT_EQ(ctx.DragPayload[0], '\0');
}

TEST(ZUIInputCaptureTest, FeedLatchesIndependentPhysicalButtonPositionsAndIgnoresDuplicateEdges)
{
    ZUIContext ctx = {};
    ZUIFeedMousePos(&ctx, 10.f, 20.f);
    ZUIFeedMouseButton(&ctx, 0, true);
    ZUIFeedMousePos(&ctx, 30.f, 40.f);
    ZUIFeedMouseButton(&ctx, 0, true);
    EXPECT_FLOAT_EQ(ctx.MousePressPos[0][0], 10.f);
    EXPECT_EQ(ctx.MousePressSerial[0], 1u);
    ZUIFeedMouseButton(&ctx, 1, true);
    EXPECT_FLOAT_EQ(ctx.MousePressPos[1][0], 30.f);
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedMousePos(&ctx, 50.f, 60.f);
    ZUIFeedMouseButton(&ctx, 0, false);
    EXPECT_FLOAT_EQ(ctx.MouseReleasePos[0][0], 30.f);
    EXPECT_FLOAT_EQ(ctx.MouseReleasePos[0][1], 40.f);
    EXPECT_EQ(ctx.MouseReleaseSerial[0], 1u);
    ZUIFeedBeginFrame(&ctx);
    EXPECT_FALSE(ctx.MouseReleased[0]);
    EXPECT_EQ(ctx.MouseReleaseSerial[0], 1u);
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedMouseButton(&ctx, 1, true);
    EXPECT_FALSE(ctx.MouseReleased[0]);
    EXPECT_FALSE(ctx.MousePressed[1]);
    ZUIFeedMouseButton(&ctx, 0, true);
    EXPECT_EQ(ctx.MousePressSerial[0], 2u);
    EXPECT_FLOAT_EQ(ctx.MousePressPos[0][0], 50.f);
    EXPECT_TRUE(ctx.MouseDown[1]);
}

TEST(ZUIInputCaptureTest, PressIntentLatchesAltPerButtonAcrossReleaseFramesAndDuplicateEdges)
{
    ZUIContext ctx = {};
    EXPECT_FALSE(ctx.MousePressAlt[0]);
    ctx.AltDown = true;
    ZUIFeedMouseButton(&ctx, 0, true);
    ctx.AltDown = false;
    ZUIFeedMouseButton(&ctx, 1, true);
    ZUIFeedMouseButton(&ctx, 0, true);
    EXPECT_TRUE(ctx.MousePressAlt[0]);
    EXPECT_FALSE(ctx.MousePressAlt[1]);
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedBeginFrame(&ctx);
    EXPECT_TRUE(ctx.MousePressAlt[0]);
    ZUIFeedMouseButton(&ctx, 0, true);
    EXPECT_FALSE(ctx.MousePressAlt[0]);
}

TEST(ZUIInputCaptureTest, SelectionModifiersRemainBoundToThePhysicalPressAcrossDuplicateEdgesAndRelease)
{
    ZUIContext ctx = {};
    ctx.CtrlDown = ctx.ShiftDown = true;
    ZUIFeedMouseButton(&ctx, 0, true);
    ctx.CtrlDown = ctx.ShiftDown = false;
    ZUIFeedMouseButton(&ctx, 1, true);
    ZUIFeedMouseButton(&ctx, 0, true);
    EXPECT_TRUE(ctx.MousePressCtrl[0]);
    EXPECT_TRUE(ctx.MousePressShift[0]);
    EXPECT_FALSE(ctx.MousePressCtrl[1]);
    EXPECT_FALSE(ctx.MousePressShift[1]);
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedBeginFrame(&ctx);
    EXPECT_TRUE(ctx.MousePressCtrl[0]);
    EXPECT_TRUE(ctx.MousePressShift[0]);
    ZUIFeedMouseButton(&ctx, 0, true);
    EXPECT_FALSE(ctx.MousePressCtrl[0]);
    EXPECT_FALSE(ctx.MousePressShift[0]);
}

TEST(ZUIInputCaptureTest, ReleaseModifiersSurviveFrameResetsDuplicateEdgesAndNewPressesPerButton)
{
    ZUIFeedMouseButton(nullptr, 0, false);
    for (int button = 0; button < 3; ++button)
    {
        for (uint32_t modifiers = 0; modifiers < 4; ++modifiers)
        {
            SCOPED_TRACE(button);
            SCOPED_TRACE(modifiers);
            ZUIContext ctx = {};
            for (int index = 0; index < 3; ++index)
            {
                EXPECT_FALSE(ctx.MouseReleaseCtrl[index]);
                EXPECT_FALSE(ctx.MouseReleaseShift[index]);
            }
            ZUIFeedMouseButton(&ctx, button, true);
            const bool control = (modifiers & 1) != 0;
            const bool shift   = (modifiers & 2) != 0;
            ctx.CtrlDown       = control;
            ctx.ShiftDown      = shift;
            ZUIFeedMouseButton(&ctx, button, false);
            ctx.CtrlDown  = !control;
            ctx.ShiftDown = !shift;
            ZUIFeedBeginFrame(&ctx);
            ZUIFeedMouseButton(&ctx, button, false); // Duplicate cannot rewrite the release.
            ZUIFeedMouseButton(&ctx, button, true);  // Nor can a different gesture's press.
            ZUIFeedMouseButton(&ctx, button, true);
            ZUIFeedMouseButton(&ctx, -1, false);
            ZUIFeedMouseButton(&ctx, 3, false);
            EXPECT_EQ(ctx.MouseReleaseSerial[button], 1u);
            EXPECT_EQ(ctx.MousePressSerial[button], 2u);
            EXPECT_EQ(ctx.MouseReleaseCtrl[button], control);
            EXPECT_EQ(ctx.MouseReleaseShift[button], shift);
            for (int other = 0; other < 3; ++other)
                if (other != button)
                {
                    EXPECT_FALSE(ctx.MouseReleaseCtrl[other]);
                    EXPECT_FALSE(ctx.MouseReleaseShift[other]);
                    EXPECT_EQ(ctx.MouseReleaseSerial[other], 0u);
                }
            ZUIFeedMouseButton(&ctx, button, false); // Only a genuine new release replaces it.
            EXPECT_EQ(ctx.MouseReleaseSerial[button], 2u);
            EXPECT_EQ(ctx.MouseReleaseCtrl[button], !control);
            EXPECT_EQ(ctx.MouseReleaseShift[button], !shift);
        }
    }
}

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

TEST(ZUIInputCaptureTest, InteractionOwnerCapturesAndReleasesWithoutAnOverlayBuild)
{
    ZUIContext ctx       = {};
    ctx.ViewportInputKey = ctx.HotKey = 42;
    ctx.InteractionCapture            = {true, true};
    for (uint32_t update = 0; update < 10; ++update)
    {
        const auto capture = ZUIGetInputCapture(&ctx);
        EXPECT_TRUE(capture.Pointer);
        EXPECT_TRUE(capture.Keyboard);
    }
    ctx.InteractionCapture = {};
    EXPECT_FALSE(ZUIGetInputCapture(&ctx).Pointer);
    EXPECT_FALSE(ZUIGetInputCapture(&ctx).Keyboard);
}

TEST(ZUIInputCaptureTest, OverlayFrameLifecycleDoesNotResetSessionCapture)
{
    ZEngine::Core::Memory::MemoryManager memory = {};
    memory.Initialize(ZMega(1), {});
    ZUIContext ctx = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZKilo(64), ZKilo(16), 64, 16);
    ctx.InteractionCapture = {true, true};
    ZUIBeginFrame(&ctx, 1.f / 60.f);
    EXPECT_TRUE(ZUIGetInputCapture(&ctx).Pointer);
    EXPECT_TRUE(ZUIGetInputCapture(&ctx).Keyboard);
    ZUIEndFrame(&ctx);
    EXPECT_TRUE(ctx.InteractionCapture.Pointer);
    EXPECT_TRUE(ctx.InteractionCapture.Keyboard);
    ZUIContextDestroy(&ctx);
    memory.Shutdown();
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

TEST(ZUIInputCaptureTest, PendingPopupCapturesBeforeTheFirstBuildAndCloseReleasesBothChannels)
{
    ZUIContext ctx       = {};
    ctx.ViewportInputKey = ctx.HotKey = ctx.ActiveKey = 42;
    ctx.FocusKey                                      = 7;
    ctx.MousePressSerial[0]                           = 19;
    ZUIOpenPopup(&ctx, "PendingViewportPopup", 30.f, 40.f);
    ASSERT_NE(ctx.PendingPopupKey, 0u);
    const auto key = ctx.PendingPopupKey;
    for (uint32_t update = 0; update < 32; ++update)
    {
        const auto capture = ZUIGetInputCapture(&ctx);
        EXPECT_TRUE(capture.Pointer);
        EXPECT_TRUE(capture.Keyboard);
        EXPECT_EQ(ctx.PendingPopupKey, key);
        EXPECT_EQ(ctx.PopupStackSize, 0u);
        EXPECT_EQ(ctx.FocusKey, 7u);
        EXPECT_EQ(ctx.ActiveKey, 42u);
        EXPECT_EQ(ctx.MousePressSerial[0], 19u);
    }
    ZUIClosePopup(&ctx);
    EXPECT_FALSE(ZUIGetInputCapture(&ctx).Pointer);
    EXPECT_FALSE(ZUIGetInputCapture(&ctx).Keyboard);
    EXPECT_EQ(ctx.FocusKey, 7u);
    EXPECT_EQ(ctx.MousePressSerial[0], 19u);
}

TEST(ZUIInputCaptureTest, PendingPopupPublicationPreservesOwnershipAndIndependentCaptureWithoutArenaGrowth)
{
    ZEngine::Core::Memory::MemoryManager memory = {};
    memory.Initialize(ZMega(1), {});
    ZUIContext ctx = {};
    ZUIContextInit(&ctx, &memory.MainArena, ZKilo(64), ZKilo(16), 64, 16);
    ZUIBeginFrame(&ctx, 1.f / 60.f);
    ctx.ViewportInputKey = ctx.HotKey = 42;
    ZUIOpenPopup(&ctx, "PendingViewportPopup", 30.f, 40.f);
    const auto key    = ctx.PendingPopupKey;
    const auto main   = memory.MainArena.m_current_offset;
    const auto frame  = ctx.FrameArena.m_current_offset;
    const auto states = ctx.PersistentArena.m_current_offset;
    for (uint32_t update = 0; update < 32; ++update)
    {
        EXPECT_TRUE(ZUIGetInputCapture(&ctx).Pointer);
        EXPECT_TRUE(ZUIGetInputCapture(&ctx).Keyboard);
        EXPECT_EQ(memory.MainArena.m_current_offset, main);
        EXPECT_EQ(ctx.FrameArena.m_current_offset, frame);
        EXPECT_EQ(ctx.PersistentArena.m_current_offset, states);
    }
    ZUIEndFrame(&ctx);
    EXPECT_EQ(ctx.PendingPopupKey, 0u);
    ASSERT_EQ(ctx.PopupStackSize, 1u);
    EXPECT_EQ(ctx.PopupStack[0].Key, key);
    EXPECT_EQ(ctx.PopupStack[0].OwnerFocusKey, 42u);
    EXPECT_TRUE(ZUIGetInputCapture(&ctx).Pointer);
    EXPECT_TRUE(ZUIGetInputCapture(&ctx).Keyboard);
    for (uint32_t independent = 0; independent < 4; ++independent)
    {
        ctx.InteractionCapture = {(independent & 1u) != 0, (independent & 2u) != 0};
        ZUIClosePopup(&ctx);
        const auto capture = ZUIGetInputCapture(&ctx);
        EXPECT_EQ(capture.Pointer, ctx.InteractionCapture.Pointer);
        EXPECT_EQ(capture.Keyboard, ctx.InteractionCapture.Keyboard);
        ZUIOpenPopup(&ctx, "PendingViewportPopup", 30.f, 40.f);
        EXPECT_TRUE(ZUIGetInputCapture(&ctx).Pointer);
        EXPECT_TRUE(ZUIGetInputCapture(&ctx).Keyboard);
    }
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
    root.LastChild        = &popup;
    modal.Parent          = &root;
    modal.NextSib         = &popup;
    popup.Parent          = &root;
    popup.PrevSib         = &modal;
    popup.FirstChild      = &item;
    popup.LastChild       = &item;
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
