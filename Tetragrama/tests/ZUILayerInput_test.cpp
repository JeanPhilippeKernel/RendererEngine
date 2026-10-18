#include <Tetragrama/Layers/ZUILayer.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/Windows/Events/KeyEvent.h>
#include <ZEngine/Windows/Events/TextInputEvent.h>
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
