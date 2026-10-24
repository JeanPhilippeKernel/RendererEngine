#include <Tetragrama/Layers/ZUILayer.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIInput.h>
#include <ZEngine/Windows/Events/KeyEvent.h>
#include <ZEngine/Windows/Events/MouseEvent.h>
#include <ZEngine/Windows/Events/TextInputEvent.h>
#include <ZEngine/Windows/Events/WindowEvent.h>
#include <ZEngine/Windows/Inputs/KeyCodeDefinition.h>
#include <gtest/gtest.h>

namespace Tetragrama::Layers
{
    struct ZUILayerInputTestAccess
    {
        static void Bind(ZUILayer& layer, ZEngine::UI::ZUIContext& ctx)
        {
            layer.m_ctx = &ctx;
        }
    };
} // namespace Tetragrama::Layers

class ZUILayerInputTest : public testing::Test
{
protected:
    ZEngine::UI::ZUIContext      ctx   = {};
    Tetragrama::Layers::ZUILayer layer = {};

    void                         SetUp() override
    {
        Tetragrama::Layers::ZUILayerInputTestAccess::Bind(layer, ctx);
    }
    void Press(ZENGINE_KEYCODE key)
    {
        ZEngine::Windows::Events::KeyPressedEvent event{key, 0};
        layer.OnKeyPressed(event);
    }
    void Release(ZENGINE_KEYCODE key)
    {
        ZEngine::Windows::Events::KeyReleasedEvent event{key};
        layer.OnKeyReleased(event);
    }
    void Text(std::string_view text)
    {
        ZEngine::Windows::Events::TextInputEvent event{text};
        layer.OnTextInputRaised(event);
    }
};

TEST_F(ZUILayerInputTest, ControlUndoRedoAndSelectAllReachTheUI)
{
    Press(ZENGINE_KEY_LEFT_CONTROL);
    Press(ZENGINE_KEY_Z);
    EXPECT_TRUE(ctx.CtrlZPressed);
    Press(ZENGINE_KEY_Y);
    EXPECT_TRUE(ctx.CtrlYPressed);
    Press(ZENGINE_KEY_A);
    EXPECT_TRUE(ctx.CtrlAPressed);
    Release(ZENGINE_KEY_LEFT_CONTROL);
    EXPECT_FALSE(ctx.CtrlDown);
}

TEST_F(ZUILayerInputTest, ShortcutEnterActivationAndPressTimeSelectionModifiersReachTheUI)
{
    using namespace ZEngine::Windows::Events;
#if defined(__APPLE__)
    constexpr auto shortcut = ZENGINE_KEY_LEFT_SUPER;
#else
    constexpr auto shortcut = ZENGINE_KEY_LEFT_CONTROL;
#endif
    Press(shortcut);
    Press(ZENGINE_KEY_ENTER);
    EXPECT_TRUE(ctx.CtrlDown);
    EXPECT_TRUE(ctx.EnterPressed);
    Press(ZENGINE_KEY_LEFT_SHIFT);
    MouseButtonPressedEvent press{ZENGINE_KEY_MOUSE_LEFT};
    layer.OnMouseButtonPressed(press);
    Release(shortcut);
    Release(ZENGINE_KEY_LEFT_SHIFT);
    MouseButtonReleasedEvent release{ZENGINE_KEY_MOUSE_LEFT};
    layer.OnMouseButtonReleased(release);
    EXPECT_FALSE(ctx.CtrlDown);
    EXPECT_FALSE(ctx.ShiftDown);
    EXPECT_TRUE(ctx.MousePressCtrl[0]);
    EXPECT_TRUE(ctx.MousePressShift[0]);
}

TEST_F(ZUILayerInputTest, ReleaseModifiersUseThePlatformShortcutAndSurviveLaterNativeKeyEvents)
{
    using namespace ZEngine::Windows::Events;
#if defined(__APPLE__)
    constexpr auto shortcut = ZENGINE_KEY_LEFT_SUPER;
#else
    constexpr auto shortcut = ZENGINE_KEY_LEFT_CONTROL;
#endif
    for (uint32_t modifiers = 0; modifiers < 4; ++modifiers)
    {
        SCOPED_TRACE(modifiers);
        const bool               control = (modifiers & 1) != 0;
        const bool               shift   = (modifiers & 2) != 0;
        MouseButtonPressedEvent  press{ZENGINE_KEY_MOUSE_LEFT};
        MouseButtonReleasedEvent release{ZENGINE_KEY_MOUSE_LEFT};
        layer.OnMouseButtonPressed(press);
        if (control)
        {
            Press(shortcut);
        }
        if (shift)
        {
            Press(ZENGINE_KEY_LEFT_SHIFT);
        }
        layer.OnMouseButtonReleased(release);
        if (control)
        {
            Release(shortcut);
        }
        else
        {
            Press(shortcut);
        }
        if (shift)
        {
            Release(ZENGINE_KEY_LEFT_SHIFT);
        }
        else
        {
            Press(ZENGINE_KEY_LEFT_SHIFT);
        }
        ZEngine::UI::ZUIFeedBeginFrame(&ctx);
        layer.OnMouseButtonReleased(release);
        EXPECT_EQ(ctx.CtrlDown, !control);
        EXPECT_EQ(ctx.ShiftDown, !shift);
        EXPECT_EQ(ctx.MouseReleaseCtrl[0], control);
        EXPECT_EQ(ctx.MouseReleaseShift[0], shift);
        layer.OnMouseButtonPressed(press);
        EXPECT_EQ(ctx.MouseReleaseCtrl[0], control);
        EXPECT_EQ(ctx.MouseReleaseShift[0], shift);
        Release(shortcut);
        Release(ZENGINE_KEY_LEFT_SHIFT);
        layer.OnMouseButtonReleased(release);
    }
}

TEST_F(ZUILayerInputTest, PointerPressPositionIsLatchedInLogicalCoordinatesUntilTheNextPhysicalPress)
{
    using namespace ZEngine::Windows::Events;
    ctx.InputScale[0] = .5f;
    ctx.InputScale[1] = .25f;
    MouseButtonMovedEvent start{200., 80., 0., 0.};
    layer.OnMouseButtonMoved(start);
    MouseButtonPressedEvent press{ZENGINE_KEY_MOUSE_LEFT};
    layer.OnMouseButtonPressed(press);
    EXPECT_FLOAT_EQ(ctx.MousePressPos[0][0], 100.f);
    EXPECT_FLOAT_EQ(ctx.MousePressPos[0][1], 20.f);
    EXPECT_EQ(ctx.MousePressSerial[0], 1u);
    MouseButtonMovedEvent moved{400., 240., 0., 0.};
    layer.OnMouseButtonMoved(moved);
    layer.OnMouseButtonPressed(press); // duplicate event must not reset the base
    EXPECT_FLOAT_EQ(ctx.MousePos[0], 200.f);
    EXPECT_FLOAT_EQ(ctx.MousePressPos[0][0], 100.f);
    EXPECT_FLOAT_EQ(ctx.MousePressPos[0][1], 20.f);
    MouseButtonPressedEvent right{ZENGINE_KEY_MOUSE_RIGHT};
    layer.OnMouseButtonPressed(right);
    EXPECT_FLOAT_EQ(ctx.MousePressPos[1][0], 200.f);
    EXPECT_FLOAT_EQ(ctx.MousePressPos[0][0], 100.f);
    MouseButtonReleasedEvent release{ZENGINE_KEY_MOUSE_LEFT};
    layer.OnMouseButtonReleased(release);
    EXPECT_FLOAT_EQ(ctx.MouseReleasePos[0][0], 200.f);
    EXPECT_FLOAT_EQ(ctx.MouseReleasePos[0][1], 60.f);
    EXPECT_EQ(ctx.MouseReleaseSerial[0], 1u);
    layer.OnMouseButtonPressed(press);
    EXPECT_EQ(ctx.MousePressSerial[0], 2u);
    EXPECT_FLOAT_EQ(ctx.MousePressPos[0][0], 200.f);
    EXPECT_FLOAT_EQ(ctx.MousePressPos[0][1], 60.f);
}

TEST_F(ZUILayerInputTest, RenameAndForwardDeleteReachTheEditorUI)
{
    EXPECT_FALSE(ctx.F2Pressed);
    Press(ZENGINE_KEY_F2);
    EXPECT_TRUE(ctx.F2Pressed);
    Press(ZENGINE_KEY_DELETE);
    EXPECT_TRUE(ctx.DeletePressed);
    EXPECT_TRUE(ctx.DeleteHeld);
    Release(ZENGINE_KEY_DELETE);
    EXPECT_FALSE(ctx.DeleteHeld);
}

TEST_F(ZUILayerInputTest, ReleasingOneSideKeepsTheOtherModifierHeld)
{
    const ZENGINE_KEYCODE pairs[][2] = {
        {ZENGINE_KEY_LEFT_CONTROL, ZENGINE_KEY_RIGHT_CONTROL},
        {  ZENGINE_KEY_LEFT_SHIFT,   ZENGINE_KEY_RIGHT_SHIFT},
        {    ZENGINE_KEY_LEFT_ALT,     ZENGINE_KEY_RIGHT_ALT}
    };
    bool* states[] = {&ctx.CtrlDown, &ctx.ShiftDown, &ctx.AltDown};
    for (int i = 0; i < 3; ++i)
    {
        Press(pairs[i][0]);
        Press(pairs[i][1]);
        Release(pairs[i][0]);
        EXPECT_TRUE(*states[i]);
        Release(pairs[i][1]);
        EXPECT_FALSE(*states[i]);
    }
}

TEST_F(ZUILayerInputTest, NativeFocusLossRetiresBothModifierSidesWithoutWaitingForKeyReleases)
{
    Press(ZENGINE_KEY_LEFT_CONTROL);
    Press(ZENGINE_KEY_RIGHT_CONTROL);
    Press(ZENGINE_KEY_LEFT_SHIFT);
    Press(ZENGINE_KEY_RIGHT_ALT);
    Press(ZENGINE_KEY_LEFT_SUPER);
    ZEngine::Windows::Events::WindowFocusLostEvent lost = {};
    layer.OnEvent(lost);
    EXPECT_FALSE(ctx.CtrlDown);
    EXPECT_FALSE(ctx.ShiftDown);
    EXPECT_FALSE(ctx.AltDown);
    ZEngine::Windows::Events::WindowFocusGainedEvent gained = {};
    layer.OnEvent(gained);
    Press(ZENGINE_KEY_RIGHT_SHIFT);
    EXPECT_TRUE(ctx.ShiftDown);
    EXPECT_FALSE(ctx.CtrlDown);
    EXPECT_FALSE(ctx.AltDown);
    Release(ZENGINE_KEY_RIGHT_SHIFT);
    Text("a");
    EXPECT_STREQ(ctx.TextInput, "a");
}

TEST_F(ZUILayerInputTest, NativeFocusLossRetiresRepeatAndActivationEdgesButPreservesTextOwnership)
{
    ctx.FocusKey        = 42;
    ctx.TextInputActive = true;
    ctx.UndoFieldKey    = 42;
    ctx.UndoTop         = 1;
    Press(ZENGINE_KEY_BACKSPACE);
    Press(ZENGINE_KEY_LEFT);
    Press(ZENGINE_KEY_DELETE);
    Press(ZENGINE_KEY_ENTER);
    Press(ZENGINE_KEY_F2);
    Text("a");
    ZEngine::Windows::Events::WindowFocusLostEvent lost = {};
    layer.OnEvent(lost);
    EXPECT_FALSE(ctx.BackspaceHeld);
    EXPECT_FALSE(ctx.BackspacePressed);
    EXPECT_FALSE(ctx.ArrowLeftHeld);
    EXPECT_FALSE(ctx.ArrowLeftPressed);
    EXPECT_FALSE(ctx.DeleteHeld);
    EXPECT_FALSE(ctx.DeletePressed);
    EXPECT_FALSE(ctx.EnterPressed);
    EXPECT_FALSE(ctx.F2Pressed);
    EXPECT_EQ(ctx.TextInputLen, 0u);
    EXPECT_EQ(ctx.FocusKey, 42u);
    EXPECT_TRUE(ctx.TextInputActive);
    EXPECT_EQ(ctx.UndoFieldKey, 42u);
    EXPECT_EQ(ctx.UndoTop, 1);
}

TEST_F(ZUILayerInputTest, MinimizeRetiresTheDragWithoutPublishingASyntheticDrop)
{
    ZEngine::Windows::Events::MouseButtonPressedEvent press{ZENGINE_KEY_MOUSE_LEFT};
    layer.OnMouseButtonPressed(press);
    ctx.ActiveKey = ctx.DragSourceKey                        = 42;
    ctx.DragPayloadLen                                       = 4;
    ctx.DragDropFired                                        = true;
    ctx.DragTargetKey                                        = 99;
    const auto                                     serial    = ctx.MousePressSerial[0];
    ZEngine::Windows::Events::WindowMinimizedEvent minimized = {};
    layer.OnEvent(minimized);
    EXPECT_FALSE(ctx.MouseDown[0]);
    EXPECT_FALSE(ctx.MousePressed[0]);
    EXPECT_FALSE(ctx.MouseReleased[0]);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_EQ(ctx.DragSourceKey, 0u);
    EXPECT_EQ(ctx.DragPayloadLen, 0u);
    EXPECT_FALSE(ctx.DragDropFired);
    EXPECT_EQ(ctx.DragTargetKey, 0u);
    EXPECT_EQ(ctx.MousePressSerial[0], serial);
    ZEngine::Windows::Events::MouseButtonReleasedEvent release{ZENGINE_KEY_MOUSE_LEFT};
    layer.OnMouseButtonReleased(release);
    EXPECT_FALSE(ctx.MouseReleased[0]);
    layer.OnMouseButtonPressed(press);
    EXPECT_EQ(ctx.MousePressSerial[0], serial + 1);
}

TEST_F(ZUILayerInputTest, CommandIsAShortcutModifierOnlyOnMacOS)
{
    Press(ZENGINE_KEY_LEFT_SUPER);
    Press(ZENGINE_KEY_Z);
    Press(ZENGINE_KEY_A);
    Text("a");
#if defined(__APPLE__)
    EXPECT_TRUE(ctx.CtrlDown);
    EXPECT_TRUE(ctx.CtrlZPressed);
    EXPECT_TRUE(ctx.CtrlAPressed);
    EXPECT_EQ(ctx.TextInputLen, 0u);
    ctx.CtrlZPressed = ctx.CtrlYPressed = false;
    Press(ZENGINE_KEY_RIGHT_SHIFT);
    Press(ZENGINE_KEY_Z);
    EXPECT_TRUE(ctx.CtrlYPressed);
    EXPECT_FALSE(ctx.CtrlZPressed);
    Release(ZENGINE_KEY_RIGHT_SHIFT);
#else
    EXPECT_FALSE(ctx.CtrlDown);
    EXPECT_FALSE(ctx.CtrlZPressed);
    EXPECT_FALSE(ctx.CtrlAPressed);
    EXPECT_STREQ(ctx.TextInput, "a");
#endif
    Press(ZENGINE_KEY_RIGHT_CONTROL);
    Release(ZENGINE_KEY_LEFT_SUPER);
    EXPECT_TRUE(ctx.CtrlDown);
    Release(ZENGINE_KEY_RIGHT_CONTROL);
    EXPECT_FALSE(ctx.CtrlDown);
}

TEST_F(ZUILayerInputTest, ShortcutCharactersAreNotInsertedAndReleaseRestoresTyping)
{
    Press(ZENGINE_KEY_LEFT_CONTROL);
    Press(ZENGINE_KEY_A);
    Text("a");
    EXPECT_TRUE(ctx.CtrlAPressed);
    EXPECT_EQ(ctx.TextInputLen, 0u);
    EXPECT_STREQ(ctx.TextInput, "");
    Release(ZENGINE_KEY_LEFT_CONTROL);
    Text("a");
    EXPECT_EQ(ctx.TextInputLen, 1u);
    EXPECT_STREQ(ctx.TextInput, "a");
}

TEST_F(ZUILayerInputTest, ShiftAndAltTextRemainAvailable)
{
    Press(ZENGINE_KEY_LEFT_SHIFT);
    Text("A");
    Release(ZENGINE_KEY_LEFT_SHIFT);
    Press(ZENGINE_KEY_LEFT_ALT);
    Text("@");
    EXPECT_EQ(ctx.TextInputLen, 2u);
    EXPECT_STREQ(ctx.TextInput, "A@");
}

TEST_F(ZUILayerInputTest, AltGrTextRemainsAvailableOnWindowsAndLinux)
{
    Press(ZENGINE_KEY_LEFT_CONTROL);
    Press(ZENGINE_KEY_RIGHT_ALT);
    Press(ZENGINE_KEY_A);
    EXPECT_FALSE(ctx.CtrlAPressed);
    Text("@");
#if defined(__APPLE__)
    EXPECT_EQ(ctx.TextInputLen, 0u);
#else
    EXPECT_EQ(ctx.TextInputLen, 1u);
    EXPECT_STREQ(ctx.TextInput, "@");
#endif
}
