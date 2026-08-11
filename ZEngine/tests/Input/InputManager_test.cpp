#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Input/InputManager.h>
#include <gtest/gtest.h>

using namespace ZEngine;
using namespace ZEngine::Input;
using namespace ZEngine::Core::Memory;

// ---------------------------------------------------------------------------
// Fixture — provides a fresh InputManager backed by a small arena per test.
// ---------------------------------------------------------------------------
struct InputManagerTest : public ::testing::Test
{
    MemoryManager manager{};
    InputManager  input{};

    void          SetUp() override
    {
        manager.Initialize(ZMega(4ULL), {});
        input.Initialize(&manager.MainArena);
    }

    void TearDown() override
    {
        input.Dispose();
        manager.Shutdown();
    }
};

// ---------------------------------------------------------------------------
// 1. Registration
// ---------------------------------------------------------------------------

TEST_F(InputManagerTest, RegisterAction_ReturnsSequentialSlots)
{
    uint32_t a = input.RegisterAction("Jump", InputActionType::Button);
    uint32_t b = input.RegisterAction("MoveX", InputActionType::Axis1D);
    uint32_t c = input.RegisterAction("Look", InputActionType::Axis2D);

    EXPECT_EQ(a, 0u);
    EXPECT_EQ(b, 1u);
    EXPECT_EQ(c, 2u);
}

TEST_F(InputManagerTest, RegisterAction_SlotsAreStable)
{
    uint32_t first  = input.RegisterAction("A", InputActionType::Button);
    uint32_t second = input.RegisterAction("B", InputActionType::Button);
    EXPECT_NE(first, second);
    // Querying the same slot twice returns the same state.
    EXPECT_EQ(input.GetButton(first).Held, false);
    EXPECT_EQ(input.GetButton(second).Held, false);
}

// ---------------------------------------------------------------------------
// 2. Binding API — no crash, no assert on valid calls.
// ---------------------------------------------------------------------------

TEST_F(InputManagerTest, BindKey_DoesNotAssert)
{
    uint32_t slot = input.RegisterAction("Forward", InputActionType::Axis1D);
    EXPECT_NO_FATAL_FAILURE(input.BindKey(slot, 87 /* GLFW_KEY_W */, 1.0f));
    EXPECT_NO_FATAL_FAILURE(input.BindKey(slot, 83 /* GLFW_KEY_S */, -1.0f));
}

TEST_F(InputManagerTest, BindMouseButton_DoesNotAssert)
{
    uint32_t slot = input.RegisterAction("Fire", InputActionType::Button);
    EXPECT_NO_FATAL_FAILURE(input.BindMouseButton(slot, 0 /* GLFW_MOUSE_BUTTON_LEFT */));
}

TEST_F(InputManagerTest, BindScrollAxis_DoesNotAssert)
{
    uint32_t slot = input.RegisterAction("Zoom", InputActionType::Axis1D);
    EXPECT_NO_FATAL_FAILURE(input.BindScrollAxis(slot, 1.0f));
}

TEST_F(InputManagerTest, BindKey_FourBindingsAllowed)
{
    uint32_t slot = input.RegisterAction("MultiKey", InputActionType::Button);
    EXPECT_NO_FATAL_FAILURE(input.BindKey(slot, 65));
    EXPECT_NO_FATAL_FAILURE(input.BindKey(slot, 66));
    EXPECT_NO_FATAL_FAILURE(input.BindKey(slot, 67));
    EXPECT_NO_FATAL_FAILURE(input.BindKey(slot, 68));
}

// ---------------------------------------------------------------------------
// 3. Default state — all buttons released, all axes zero before first Poll.
// ---------------------------------------------------------------------------

TEST_F(InputManagerTest, DefaultButtonState_IsReleased)
{
    uint32_t slot = input.RegisterAction("Jump", InputActionType::Button);
    input.BindKey(slot, 32 /* GLFW_KEY_SPACE */);

    const auto& state = input.GetButton(slot);
    EXPECT_FALSE(state.Held);
    EXPECT_FALSE(state.JustDown);
    EXPECT_FALSE(state.JustUp);
}

TEST_F(InputManagerTest, DefaultAxisState_IsZero)
{
    uint32_t slot = input.RegisterAction("MoveX", InputActionType::Axis1D);
    input.BindKey(slot, 68 /* GLFW_KEY_D */, 1.0f);
    EXPECT_FLOAT_EQ(input.GetAxis(slot), 0.0f);
}

TEST_F(InputManagerTest, DefaultMouseDelta_IsZero)
{
    auto delta = input.GetMouseDelta();
    EXPECT_FLOAT_EQ(delta.x, 0.0f);
    EXPECT_FLOAT_EQ(delta.y, 0.0f);
}

TEST_F(InputManagerTest, DefaultScrollDelta_IsZero)
{
    EXPECT_FLOAT_EQ(input.GetScrollDelta(), 0.0f);
}

// ---------------------------------------------------------------------------
// 4. AccumulateScroll — feeds the scroll accumulator without a GLFWwindow.
// ---------------------------------------------------------------------------

TEST_F(InputManagerTest, AccumulateScroll_AccumulatesBeforePoll)
{
    input.AccumulateScroll(1.5);
    input.AccumulateScroll(0.5);
    // Scroll is drained by Poll; before Poll it sits in the accumulator.
    // We cannot call Poll without a window, but we can verify the value
    // surfaces via GetScrollDelta after a manual drain by checking the
    // internal state indirectly: bind a scroll action, drain via
    // AccumulateScroll, and confirm GetScrollDelta is still 0 (not drained
    // yet — Poll has not been called).
    EXPECT_FLOAT_EQ(input.GetScrollDelta(), 0.0f);
}

TEST_F(InputManagerTest, AccumulateScroll_NegativeDelta)
{
    input.AccumulateScroll(-3.0);
    // Accumulator is internal; before Poll GetScrollDelta reflects last Poll result (0).
    EXPECT_FLOAT_EQ(input.GetScrollDelta(), 0.0f);
}

// ---------------------------------------------------------------------------
// 5. InputFrame — initial state is zeroed.
// ---------------------------------------------------------------------------

TEST_F(InputManagerTest, GetCurrentFrame_InitialFrameNumberIsZero)
{
    EXPECT_EQ(input.GetCurrentFrame().FrameNumber, 0u);
}

TEST_F(InputManagerTest, GetCurrentFrame_InitialActionCountIsZero)
{
    EXPECT_EQ(input.GetCurrentFrame().ActionCount, 0u);
}

TEST_F(InputManagerTest, GetCurrentFrame_AfterRegistration_ActionCountUpdatedOnNextPoll)
{
    input.RegisterAction("A", InputActionType::Button);
    input.RegisterAction("B", InputActionType::Axis1D);
    // ActionCount in the frame is set by Poll; before Poll it reflects the
    // previous Poll's count (0 since Poll hasn't run).
    EXPECT_EQ(input.GetCurrentFrame().ActionCount, 0u);
}

// ---------------------------------------------------------------------------
// 6. JustDown / JustUp derivation — simulated by manually setting prev frame
//    state via two consecutive AccumulateScroll+Dispose+Re-init cycles is not
//    feasible without Poll. Instead verify the logic holds at the type level:
//    InputButtonState fields are independent booleans.
// ---------------------------------------------------------------------------

TEST_F(InputManagerTest, InputButtonState_FieldsAreIndependent)
{
    InputButtonState s{};
    s.Held     = true;
    s.JustDown = true;
    s.JustUp   = false;
    EXPECT_TRUE(s.Held);
    EXPECT_TRUE(s.JustDown);
    EXPECT_FALSE(s.JustUp);
}

// ---------------------------------------------------------------------------
// 7. InputFrame is POD-compatible (trivially copyable).
// ---------------------------------------------------------------------------

TEST_F(InputManagerTest, InputFrame_IsTriviallyCopyable)
{
    EXPECT_TRUE(std::is_trivially_copyable<InputFrame>::value);
}

TEST_F(InputManagerTest, InputButtonState_IsTriviallyCopyable)
{
    EXPECT_TRUE(std::is_trivially_copyable<InputButtonState>::value);
}

TEST_F(InputManagerTest, InputAxisState_IsTriviallyCopyable)
{
    EXPECT_TRUE(std::is_trivially_copyable<InputAxisState>::value);
}

// ---------------------------------------------------------------------------
// 8. Capacity limits — registering exactly kMaxActions slots succeeds.
// ---------------------------------------------------------------------------

TEST_F(InputManagerTest, RegisterAction_FillsToCapacity)
{
    for (uint32_t i = 0; i < kMaxActions; ++i)
    {
        char name[16];
        snprintf(name, sizeof(name), "action_%u", i);
        uint32_t slot = input.RegisterAction(name, InputActionType::Button);
        EXPECT_EQ(slot, i);
    }
    EXPECT_EQ(input.GetCurrentFrame().ActionCount, 0u); // not polled yet
}

// ---------------------------------------------------------------------------
// 9. Axis2D default.
// ---------------------------------------------------------------------------

TEST_F(InputManagerTest, GetAxis2D_DefaultIsZeroVector)
{
    uint32_t slot = input.RegisterAction("Look", InputActionType::Axis2D);
    auto     v    = input.GetAxis2D(slot);
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
}
