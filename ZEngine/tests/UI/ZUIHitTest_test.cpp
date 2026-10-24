#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIInteraction.h>
#include <gtest/gtest.h>
#include <limits>

using namespace ZEngine::UI;

namespace
{
    struct HitProbe
    {
        bool                  Accept       = true;
        mutable uint32_t      Calls        = 0;
        mutable const ZUIBox* LastBox      = nullptr;
        mutable float         LastPoint[2] = {};

        static bool           Test(const ZUIBox* box, const float point[2], const void* data)
        {
            const auto& probe = *static_cast<const HitProbe*>(data);
            ++probe.Calls;
            probe.LastBox      = box;
            probe.LastPoint[0] = point[0];
            probe.LastPoint[1] = point[1];
            return probe.Accept;
        }
    };

    class ZUIHitTest : public testing::Test
    {
    protected:
        ZEngine::Core::Memory::MemoryManager memory = {};
        ZUIContext                           ctx    = {};
        HitProbe                             probe  = {};

        void                                 SetUp() override
        {
            memory.Initialize(ZMega(1), {});
            ZUIContextInit(&ctx, &memory.MainArena, ZKilo(128), ZKilo(64), 64, 32);
            ZUIBeginFrame(&ctx, 1.f / 60.f);
            Add(1, 0.f, 0.f, 400.f, 300.f, ZUI_None);
            ctx.MousePos[0] = ctx.MousePos[1] = 50.f;
        }

        void TearDown() override
        {
            ZUIContextDestroy(&ctx);
            memory.Shutdown();
        }

        ZUIBox* Add(uint64_t key, float x, float y, float width, float height, ZUIBoxFlags flags = ZUI_Clickable, ZUIBox* parent = nullptr)
        {
            ctx.Current       = parent ? parent : ctx.Root;
            auto* box         = ZUIPushBox(&ctx, "", 0, flags);
            box->Key          = key;
            box->ScreenMin[0] = x;
            box->ScreenMin[1] = y;
            box->ScreenMax[0] = x + width;
            box->ScreenMax[1] = y + height;
            return box;
        }

        ZUIBox* Viewport()
        {
            ctx.ViewportInputKey = 2;
            return Add(2, 0.f, 0.f, 400.f, 300.f, ZUI_Clickable | ZUI_NoKeyboardFocus);
        }

        ZUIBox* Overlay(ZUIBox* parent = nullptr)
        {
            auto* box              = Add(3, 0.f, 0.f, 400.f, 300.f, ZUI_Clickable | ZUI_NoKeyboardFocus, parent);
            box->CustomHitTest     = HitProbe::Test;
            box->CustomHitTestData = &probe;
            return box;
        }
    };
} // namespace

TEST_F(ZUIHitTest, DefaultsToTheExistingRectangleBehavior)
{
    auto* box = Add(4, 40.f, 40.f, 30.f, 30.f);
    EXPECT_EQ(box->CustomHitTest, nullptr);
    EXPECT_EQ(box->CustomHitTestData, nullptr);
    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.HotKey, box->Key);
}

TEST_F(ZUIHitTest, ReadOnlyQueryUsesExplicitPointWithoutChangingOrConsumingInput)
{
    Viewport();
    const auto* shape   = Overlay();
    ctx.HotKey          = 27;
    ctx.ActiveKey       = 28;
    ctx.FocusKey        = 29;
    ctx.MousePressed[0] = ctx.MouseDown[0] = true;
    ctx.ScrollDelta                        = 1.f;
    const float point[2]                   = {60.f, 60.f};
    const auto  used                       = ctx.FrameArena.m_current_offset;
    for (uint32_t i = 0; i < 100; ++i)
    {
        const auto query = ZUIQueryPointer(&ctx, point);
        EXPECT_EQ(query.HotBox, shape);
        EXPECT_EQ(query.ScrollBox, nullptr);
    }
    EXPECT_EQ(ctx.HotKey, 27u);
    EXPECT_EQ(ctx.ActiveKey, 28u);
    EXPECT_EQ(ctx.FocusKey, 29u);
    EXPECT_TRUE(ctx.MousePressed[0]);
    EXPECT_TRUE(ctx.MouseDown[0]);
    EXPECT_EQ(ctx.ScrollDelta, 1.f);
    EXPECT_EQ(ctx.MousePos[0], 50.f);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, used);
    EXPECT_EQ(probe.LastPoint[0], 60.f);
}

TEST_F(ZUIHitTest, ReadOnlyQueryCannotDismissPopupOrAllocateScrollState)
{
    Viewport();
    auto* popup            = Add(4, 200.f, 200.f, 100.f, 100.f, ZUI_Clickable | ZUI_Scrollable);
    ctx.PopupStackSize     = 1;
    ctx.PopupStack[0].Box  = popup;
    ctx.MousePressed[0]    = true;
    const float outside[2] = {50.f, 50.f};
    EXPECT_EQ(ZUIQueryPointer(&ctx, outside).HotBox, nullptr);
    EXPECT_EQ(ctx.PopupStackSize, 1u);
    const float inside[2] = {250.f, 250.f};
    const auto  states    = ctx.StateStore.Count;
    const auto  query     = ZUIQueryPointer(&ctx, inside);
    EXPECT_EQ(query.HotBox, popup);
    EXPECT_EQ(query.ScrollBox, popup);
    EXPECT_EQ(ctx.StateStore.Count, states);
    EXPECT_EQ(ctx.PopupStackSize, 1u);
}

TEST_F(ZUIHitTest, ReadOnlyQueryPreservesPaintOrderClippingAndCustomRejection)
{
    const auto* image = Viewport();
    Overlay();
    auto* toolbar = Add(4, 45.f, 45.f, 20.f, 20.f);
    EXPECT_EQ(ZUIQueryPointer(&ctx, ctx.MousePos).HotBox, toolbar);
    toolbar->Flags = ZUI_Disabled;
    EXPECT_EQ(ZUIQueryPointer(&ctx, ctx.MousePos).HotBox->Key, 3u);
    probe.Accept = false;
    EXPECT_EQ(ZUIQueryPointer(&ctx, ctx.MousePos).HotBox, image);
    auto* clip = Add(5, 200.f, 200.f, 20.f, 20.f, ZUI_ClipChildren);
    Overlay(clip);
    probe.Accept = true;
    EXPECT_EQ(ZUIQueryPointer(&ctx, ctx.MousePos).HotBox->Key, 3u);
}

TEST_F(ZUIHitTest, ReadOnlyQueryRejectsMissingTreeAndNonfinitePoint)
{
    EXPECT_EQ(ZUIQueryPointer(nullptr, ctx.MousePos).HotBox, nullptr);
    EXPECT_EQ(ZUIQueryPointer(&ctx, nullptr).HotBox, nullptr);
    const float point[2] = {std::numeric_limits<float>::infinity(), 50.f};
    EXPECT_EQ(ZUIQueryPointer(&ctx, point).HotBox, nullptr);
    ctx.Root = nullptr;
    EXPECT_EQ(ZUIQueryPointer(&ctx, ctx.MousePos).HotBox, nullptr);
}

TEST_F(ZUIHitTest, RejectingShapeLeavesTheViewportAndCameraAvailable)
{
    Viewport();
    Overlay();
    probe.Accept = false;
    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.HotKey, ctx.ViewportInputKey);
    EXPECT_FALSE(ZUIGetInputCapture(&ctx).Pointer);
    EXPECT_FALSE(ZUIGetInputCapture(&ctx).Keyboard);
    EXPECT_EQ(probe.Calls, 1u);
}

TEST_F(ZUIHitTest, AcceptingShapeCapturesHoverWithoutTakingKeyboardFocus)
{
    Viewport();
    const auto* box = Overlay();
    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.HotKey, box->Key);
    EXPECT_TRUE(ZUIGetInputCapture(&ctx).Pointer);
    EXPECT_FALSE(ZUIGetInputCapture(&ctx).Keyboard);
    EXPECT_EQ(probe.Calls, 1u);
    EXPECT_EQ(probe.LastBox, box);
    EXPECT_FLOAT_EQ(probe.LastPoint[0], 50.f);
    EXPECT_FLOAT_EQ(probe.LastPoint[1], 50.f);
    ctx.MousePressed[0] = ctx.MouseDown[0] = true;
    ctx.FocusKey                           = 123;
    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.ActiveKey, box->Key);
    EXPECT_EQ(ctx.FocusKey, 0u);
}

TEST_F(ZUIHitTest, ShapeCannotEnlargeTheCoarseRectangle)
{
    auto* box              = Add(4, 100.f, 100.f, 30.f, 30.f);
    box->CustomHitTest     = HitProbe::Test;
    box->CustomHitTestData = &probe;
    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.HotKey, 0u);
    EXPECT_EQ(probe.Calls, 0u);
}

TEST_F(ZUIHitTest, MinimumHitSizeStillAppliesBeforeTheShapeFilter)
{
    auto* box          = Add(4, 50.f, 50.f, 6.f, 6.f);
    box->MinHitSize[0] = box->MinHitSize[1] = 22.f;
    box->CustomHitTest                      = HitProbe::Test;
    box->CustomHitTestData                  = &probe;
    ctx.MousePos[0] = ctx.MousePos[1] = 43.f;
    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.HotKey, box->Key);
    EXPECT_EQ(probe.Calls, 1u);
    probe.Accept = false;
    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.HotKey, 0u);
}

TEST_F(ZUIHitTest, ClippedShapeNeitherCallsTheFilterNorStealsInput)
{
    Viewport();
    auto* clip = Add(4, 100.f, 100.f, 20.f, 20.f, ZUI_ClipChildren);
    Overlay(clip);
    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.HotKey, ctx.ViewportInputKey);
    EXPECT_EQ(probe.Calls, 0u);
}

TEST_F(ZUIHitTest, ModalGateRunsBeforeTheShapeFilter)
{
    Viewport();
    Overlay();
    ctx.ModalBox = Add(4, 100.f, 100.f, 200.f, 100.f);
    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.HotKey, 0u);
    EXPECT_EQ(probe.Calls, 0u);
}

TEST_F(ZUIHitTest, PopupGateRunsBeforeTheShapeFilter)
{
    Viewport();
    Overlay();
    ctx.PopupStack[0].Box = Add(4, 100.f, 100.f, 200.f, 100.f);
    ctx.PopupStackSize    = 1;
    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.HotKey, 0u);
    EXPECT_EQ(probe.Calls, 0u);
}

TEST_F(ZUIHitTest, PortaledPopupShapeRemainsInteractiveInsideAModal)
{
    ctx.ModalBox          = Add(4, 100.f, 100.f, 200.f, 100.f);
    auto* popup           = Add(5, 0.f, 0.f, 400.f, 300.f, ZUI_None);
    ctx.PopupStack[0].Box = popup;
    ctx.PopupStackSize    = 1;
    auto* box             = Overlay(popup);
    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.HotKey, box->Key);
    EXPECT_EQ(probe.Calls, 1u);
}

TEST_F(ZUIHitTest, LaterPaintedControlsStillWinOverTheShape)
{
    Viewport();
    Overlay();
    auto* button = Add(4, 40.f, 40.f, 30.f, 30.f);
    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.HotKey, button->Key);
    EXPECT_EQ(probe.Calls, 1u);
}

TEST_F(ZUIHitTest, DisabledShapeIsNotQueriedAndDoesNotCaptureInput)
{
    Viewport();
    auto* box  = Overlay();
    box->Flags = box->Flags | ZUI_Disabled;
    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.HotKey, ctx.ViewportInputKey);
    EXPECT_EQ(probe.Calls, 0u);
}

TEST_F(ZUIHitTest, DisabledScrollRegionDoesNotConsumeWheel)
{
    auto* scroll = Add(4, 0.f, 0.f, 400.f, 300.f, ZUI_Scrollable | ZUI_Disabled);
    auto* state  = ZUIStateGetOrInsert(&ctx.StateStore, scroll->Key);
    ASSERT_NE(state, nullptr);
    state->MaxScrollY = 200.f;
    ctx.ScrollDelta   = -1.f;
    ZUIInteractionPass(&ctx);
    EXPECT_FLOAT_EQ(state->ScrollYTarget, 0.f);
}

TEST_F(ZUIHitTest, RejectedParentShapeDoesNotPruneClickableChildren)
{
    auto* overlay = Overlay();
    auto* button  = Add(4, 40.f, 40.f, 30.f, 30.f, ZUI_Clickable, overlay);
    probe.Accept  = false;
    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.HotKey, button->Key);
    EXPECT_EQ(probe.Calls, 1u);
}

TEST_F(ZUIHitTest, RejectedOverlayDoesNotBlockScrollingUnderneath)
{
    auto* scroll = Add(4, 0.f, 0.f, 400.f, 300.f, ZUI_Scrollable);
    auto* state  = ZUIStateGetOrInsert(&ctx.StateStore, scroll->Key);
    ASSERT_NE(state, nullptr);
    state->MaxScrollY = 200.f;
    ctx.ScrollDelta   = -1.f;
    Overlay();
    probe.Accept = false;
    ZUIInteractionPass(&ctx);
    EXPECT_FLOAT_EQ(state->ScrollYTarget, ctx.Style.MouseScrollSpeed);
    EXPECT_EQ(ctx.HotKey, 0u);
}

TEST_F(ZUIHitTest, ReleaseOutsideTheShapeClearsActiveWithoutANewHit)
{
    Viewport();
    const auto* box     = Overlay();
    ctx.MousePressed[0] = ctx.MouseDown[0] = true;
    ZUIInteractionPass(&ctx);
    ASSERT_EQ(ctx.ActiveKey, box->Key);
    ctx.MousePressed[0] = ctx.MouseDown[0] = false;
    ctx.MouseReleased[0]                   = true;
    probe.Accept                           = false;
    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_EQ(ctx.HotKey, ctx.ViewportInputKey);
    EXPECT_FALSE(ZUIGetInputCapture(&ctx).Pointer);
}

TEST_F(ZUIHitTest, ReusedFrameArenaDoesNotRetainTheOldCallbackOrData)
{
    const auto* previous = Overlay();
    ZUIBeginFrame(&ctx, 1.f / 60.f);
    Add(1, 0.f, 0.f, 400.f, 300.f, ZUI_None);
    auto* box = Add(3, 0.f, 0.f, 400.f, 300.f);
    EXPECT_EQ(box, previous);
    ASSERT_EQ(box->CustomHitTest, nullptr);
    ASSERT_EQ(box->CustomHitTestData, nullptr);
    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.HotKey, box->Key);
    EXPECT_EQ(probe.Calls, 0u);
}

TEST_F(ZUIHitTest, SessionCaptureSurvivesARejectedShapeAndUpdatesWithoutAnOverlay)
{
    Viewport();
    Overlay();
    probe.Accept           = false;
    ctx.InteractionCapture = {true, true};
    ZUIInteractionPass(&ctx);
    ASSERT_EQ(ctx.HotKey, ctx.ViewportInputKey);
    for (uint32_t update = 0; update < 10; ++update)
    {
        EXPECT_TRUE(ZUIGetInputCapture(&ctx).Pointer);
        EXPECT_TRUE(ZUIGetInputCapture(&ctx).Keyboard);
    }
    EXPECT_EQ(probe.Calls, 1u);
    ctx.InteractionCapture = {};
    EXPECT_FALSE(ZUIGetInputCapture(&ctx).Pointer);
    EXPECT_FALSE(ZUIGetInputCapture(&ctx).Keyboard);
}

TEST_F(ZUIHitTest, InvalidPointerCoordinatesCannotCaptureOrInvokeTheShape)
{
    Overlay();
    for (float value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
    {
        ctx.MousePos[0] = value;
        ZUIInteractionPass(&ctx);
        EXPECT_EQ(ctx.HotKey, 0u);
        EXPECT_EQ(probe.Calls, 0u);
    }
}
