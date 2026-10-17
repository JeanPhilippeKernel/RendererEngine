#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <limits>
#include <string>

using namespace ZEngine::UI;
using namespace ZEngine::UI::ColorMath;
using namespace ZEngine::Helpers;

class ZUIColorPickerTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager memory     = {};
    ZUIContext                           ctx        = {};
    ZUIGlyph                             glyphs[96] = {};
    ZUIFont                              font       = {};
    ZUIFontAtlas                         atlas      = {};
    float                                color[4]   = {87.f / 255.f, 157.f / 255.f, 227.f / 255.f, 1.f};
    bool                                 withModal  = false;

    void                                 SetUp() override
    {
        memory.Initialize(ZMega(8), {});
        ZUIContextInit(&ctx, &memory.MainArena, ZMega(4), ZMega(1), 1024, 1024);
        ctx.ScreenW     = 800;
        ctx.ScreenH     = 900;
        ctx.MousePos[0] = ctx.MousePos[1] = -100.f;
        for (auto& glyph : glyphs)
            glyph.AdvanceX = 7.f;
        font.Glyphs     = glyphs;
        font.GlyphCount = 96;
        font.LineHeight = 13.f;
        atlas.Body = atlas.Small = atlas.Large = &font;
        ctx.Atlas                              = &atlas;
    }
    void TearDown() override
    {
        ZUIContextDestroy(&ctx);
    }
    uint64_t Key(const char* part, int index = 0, const char* owner = "p")
    {
        char key[64] = {};
        snprintf(key, sizeof(key), "##color_%016llx_%s%d", static_cast<unsigned long long>(ZUIHashStr(owner, static_cast<uint32_t>(secure_strlen(owner)))), part, index);
        return ZUIHashStr(key, static_cast<uint32_t>(secure_strlen(key)));
    }
    ZUIColorWidgetState& State()
    {
        return *ZUIStateGetOrInsert(&ctx.StateStore, ZUIHashStr("p", 1))->ColorState;
    }
    ZUIBox* Find(uint64_t key, ZUIBox* root = nullptr)
    {
        if (!root)
            root = ctx.Root;
        if (root->Key == key)
            return root;
        for (auto* child = root->FirstChild; child; child = child->NextSib)
            if (auto* found = Find(key, child))
                return found;
        return nullptr;
    }
    std::string Label(const char* part, int index = 0)
    {
        auto* box = Find(Key(part, index));
        return box && box->Label.Ptr ? std::string(box->Label.Ptr, box->Label.Len) : "";
    }
    bool Frame(bool popup = false, ZUIColorSpace space = ZUIColorSpace::Srgb)
    {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(800), ZPx(900));
        const bool modal   = withModal && ZUIBeginModal(&ctx, "settings", "Settings");
        const bool changed = popup ? ZUIColorEdit4(&ctx, "p", color, space) : ZUIColorPicker4(&ctx, "p", color);
        if (modal)
            ZUIEndModal(&ctx);
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
        return changed;
    }
    void Move(const char* part, int index, float x, float y)
    {
        auto* box = Find(Key(part, index));
        ASSERT_NE(box, nullptr);
        ctx.MousePos[0] = box->ScreenMin[0] + x;
        ctx.MousePos[1] = box->ScreenMin[1] + y;
        Frame(); // Resolve hover at the new position before delivering press.
    }
    bool Press(const char* part, int index, float x, float y)
    {
        Move(part, index, x, y);
        ctx.MousePressed[0] = ctx.MouseDown[0] = true;
        return Frame();
    }
    bool Release()
    {
        ctx.MouseDown[0]     = false;
        ctx.MouseReleased[0] = true;
        return Frame();
    }
    void Focus(const char* part, int index = 0)
    {
        ctx.FocusKey      = Key(part, index);
        ctx.KeyboardFocus = true;
    }
    bool Nudge(const char* part, int index = 0, bool increase = true)
    {
        Focus(part, index);
        ctx.ArrowRightPressed = increase;
        ctx.ArrowLeftPressed  = !increase;
        return Frame();
    }
    void Type(const char* part, const char* text, int index = 0)
    {
        Focus(part, index);
        Frame();
        ctx.CtrlAPressed = true;
        Frame();
        snprintf(ctx.TextInput, sizeof(ctx.TextInput), "%s", text);
        ctx.TextInputLen = static_cast<uint32_t>(secure_strlen(ctx.TextInput));
        ctx.EnterPressed = true;
    }
};

TEST_F(ZUIColorPickerTest, FieldUsesFrozenOriginLivePreviewAndOneUndoEntryOnRelease)
{
    // A red field point must not overlap the green colour's chain legs.
    color[1] = .5f;
    color[0] = color[2] = 0.f;
    EXPECT_FALSE(Frame());
    EXPECT_TRUE(Press("field", 0, 144, 116));
    EXPECT_EQ(State().Hsb.B, 50);
    EXPECT_EQ(State().Hsb.S, 50);
    EXPECT_EQ(ctx.ColorUndoCount, 0u);
    EXPECT_EQ(ctx.RecentColorCount, 0u);
    ctx.MousePos[0] = 13 + 216;
    EXPECT_TRUE(Frame());
    EXPECT_EQ(State().Hsb.B, 100);
    ctx.MousePos[0] = 13 + 144;
    EXPECT_TRUE(Frame());
    EXPECT_EQ(State().Hsb.B, 50); // No ratcheting from the outer drag.
    ctx.MousePos[0] = 10000;
    EXPECT_TRUE(Release());
    EXPECT_EQ(State().Hsb.B, 100);
    EXPECT_EQ(ctx.ColorUndoCount, 1u);
    EXPECT_EQ(ctx.RecentColorCount, 1u);
    EXPECT_EQ(Label("hex"), "#FF0000");
    Focus("field");
    ctx.CtrlZPressed = true;
    EXPECT_TRUE(Frame());
    EXPECT_FLOAT_EQ(color[0], 0.f);
    EXPECT_FLOAT_EQ(color[1], .5f);
    EXPECT_EQ(ctx.ColorUndoCursor, 0u);
    Focus("field");
    ctx.CtrlYPressed = true;
    EXPECT_TRUE(Frame());
    EXPECT_FLOAT_EQ(color[0], 1.f);
    EXPECT_EQ(ctx.ColorUndoCursor, 1u);
}

TEST_F(ZUIColorPickerTest, HueBadgeKeepsSaturationBrightnessAlphaAndModelAndCommitsOneGesture)
{
    ctx.ColorModel = ZUIColorModel::HSL;
    color[3]       = .37f;
    Frame();
    const auto initial = State().Hsb;
    auto*      badge   = Find(Key("hue"));
    ASSERT_NE(badge, nullptr);
    const auto* field = Find(Key("field"));
    const float x = field->ScreenMin[0], y = field->ScreenMin[1];
    EXPECT_NEAR(std::hypot(badge->FloatPos[0] + badge->Size[0].Value * .5f - 120.f, badge->FloatPos[1] + 10.f - 116.f), 103.f, .001f);
    Press("hue", 0, badge->ComputedSize[0] * .5f, 10.f);
    ctx.MousePos[0] = x + 120.f;
    ctx.MousePos[1] = y + 219.f;
    EXPECT_TRUE(Frame());
    EXPECT_EQ(State().Hsb, (HSB{270, initial.S, initial.B}));
    EXPECT_EQ(ctx.ColorModel, ZUIColorModel::HSL);
    EXPECT_FLOAT_EQ(color[3], .37f);
    EXPECT_EQ(ctx.ColorUndoCount, 0u);
    EXPECT_EQ(ctx.RecentColorCount, 0u);
    // Release far outside still uses the pointer angle, not a clamped field position.
    ctx.MousePos[0] = x + 10000.f;
    ctx.MousePos[1] = y + 116.f;
    EXPECT_TRUE(Release());
    EXPECT_EQ(State().Hsb, (HSB{0, initial.S, initial.B}));
    EXPECT_EQ(ctx.ColorUndoCount, 1u);
    EXPECT_EQ(ctx.RecentColorCount, 1u);
    ctx.CtrlZPressed = true;
    Frame();
    EXPECT_EQ(ToByte(color[0]), 87);
    EXPECT_EQ(ToByte(color[1]), 157);
    EXPECT_EQ(ToByte(color[2]), 227);
}

TEST_F(ZUIColorPickerTest, AllSixVerticesSelectFullStrengthInEveryModelAndSupportUndo)
{
    color[3] = .37f;
    for (const auto model : {ZUIColorModel::RGB, ZUIColorModel::HSB, ZUIColorModel::HSL})
    {
        ctx.ColorModel = model;
        for (int i = 0; i < 6; ++i)
        {
            Frame();
            const auto before = std::array{color[0], color[1], color[2]};
            const auto undo   = ctx.ColorUndoCount;
            Press("vertex", i, 11.f, 11.f);
            EXPECT_TRUE(Release());
            const auto expected = HsbToRgb(i * 60, 100, 100);
            EXPECT_EQ(ToByte(color[0]), expected.R);
            EXPECT_EQ(ToByte(color[1]), expected.G);
            EXPECT_EQ(ToByte(color[2]), expected.B);
            EXPECT_FLOAT_EQ(color[3], .37f);
            EXPECT_EQ(ctx.ColorModel, model);
            EXPECT_EQ(ctx.ColorUndoCount, undo + 1);
            EXPECT_EQ(State().Hsl, (HSL{i * 60, 100, 50}));
            ctx.CtrlZPressed = true;
            EXPECT_TRUE(Frame()); // Pointer-only vertices still own mouse focus for undo.
            for (int channel = 0; channel < 3; ++channel)
                EXPECT_FLOAT_EQ(color[channel], before[channel]);
            ctx.CtrlYPressed = true;
            EXPECT_TRUE(Frame());
        }
    }
}

TEST_F(ZUIColorPickerTest, EarlierRingsSolveOnlyTheirPrefixAndClampOutsideOnRelease)
{
    color[3] = .37f;
    Frame();
    auto chain = BuildChain({87, 157, 227});
    Press("field", 0, static_cast<float>(chain[1].X), static_cast<float>(chain[1].Y));
    EXPECT_EQ(State().ChainTarget, 0);
    const float x = Find(Key("field"))->ScreenMin[0], y = Find(Key("field"))->ScreenMin[1];
    ctx.MousePos[0] = x + 120.f + 160.f * 96.f / 255.f;
    ctx.MousePos[1] = y + 30.f; // Perpendicular displacement cannot change R's projection.
    EXPECT_TRUE(Frame());
    EXPECT_EQ(ToByte(color[0]), 160);
    EXPECT_EQ(ToByte(color[1]), 157);
    EXPECT_EQ(ToByte(color[2]), 227);
    ctx.MousePos[0] = x + 10000.f;
    EXPECT_TRUE(Release());
    EXPECT_EQ(ToByte(color[0]), 255);
    EXPECT_EQ(ctx.ColorUndoCount, 1u);
    chain = BuildChain({255, 157, 227});
    Press("field", 0, static_cast<float>(chain[2].X), static_cast<float>(chain[2].Y));
    ASSERT_EQ(State().ChainTarget, 1);
    const auto target = BuildChain({100, 80, 227})[2];
    ctx.MousePos[0]   = x + static_cast<float>(target.X);
    ctx.MousePos[1]   = y + static_cast<float>(target.Y);
    EXPECT_TRUE(Release());
    EXPECT_EQ(ToByte(color[0]), 100);
    EXPECT_EQ(ToByte(color[1]), 80);
    EXPECT_EQ(ToByte(color[2]), 227);
    EXPECT_FLOAT_EQ(color[3], .37f);
    EXPECT_EQ(ctx.ColorUndoCount, 2u);
}

TEST_F(ZUIColorPickerTest, EachLegUsesFrozenProjectionAndChangesOnlyItsChannel)
{
    constexpr Point directions[] = {
        {  1,                  0},
        {-.5, -.8660254037844386},
        {-.5,  .8660254037844386}
    };
    const int start[] = {87, 157, 227};
    for (int channel = 0; channel < 3; ++channel)
    {
        for (int i = 0; i < 3; ++i)
            color[i] = start[i] / 255.f;
        color[3] = .37f;
        Frame();
        const auto  chain  = BuildChain({87, 157, 227});
        const Point middle = {(chain[channel].X + chain[channel + 1].X) * .5, (chain[channel].Y + chain[channel + 1].Y) * .5};
        EXPECT_FALSE(Press("field", 0, static_cast<float>(middle.X), static_cast<float>(middle.Y)));
        ASSERT_EQ(State().ChainTarget, channel + 3);
        const float x = ctx.MousePos[0], y = ctx.MousePos[1];
        // Extra perpendicular motion does not affect the channel.
        ctx.MousePos[0] = x + static_cast<float>(directions[channel].X * 10 * 96 / 255 - directions[channel].Y * 20);
        ctx.MousePos[1] = y + static_cast<float>(directions[channel].Y * 10 * 96 / 255 + directions[channel].X * 20);
        EXPECT_TRUE(Frame());
        for (int i = 0; i < 3; ++i)
            EXPECT_EQ(ToByte(color[i]), start[i] + (i == channel ? 10 : 0));
        ctx.MousePos[0] = x;
        ctx.MousePos[1] = y;
        EXPECT_TRUE(Frame());
        EXPECT_EQ(ToByte(color[channel]), start[channel]); // No ratcheting.
        ctx.MousePos[0] = x - static_cast<float>(directions[channel].X * 10000);
        ctx.MousePos[1] = y - static_cast<float>(directions[channel].Y * 10000);
        EXPECT_TRUE(Release());
        EXPECT_EQ(ToByte(color[channel]), 0);
        EXPECT_FLOAT_EQ(color[3], .37f);
    }
    EXPECT_EQ(ctx.ColorUndoCount, 3u);
}

TEST_F(ZUIColorPickerTest, LastRingUsesFrozenFieldOriginAndWinsCoincidentRings)
{
    Frame();
    const auto initial = State().Hsb;
    const auto chain   = BuildChain({87, 157, 227});
    Press("field", 0, static_cast<float>(chain[3].X), static_cast<float>(chain[3].Y));
    EXPECT_EQ(State().ChainTarget, 2);
    const float x = Find(Key("field"))->ScreenMin[0], y = Find(Key("field"))->ScreenMin[1];
    ctx.MousePos[0] = x + 216;
    ctx.MousePos[1] = y + 116;
    Frame();
    EXPECT_EQ(State().Hsb.B, 100);
    ctx.MousePos[0] = x + 144;
    Frame();
    EXPECT_EQ(State().Hsb.B, initial.B);
    Release();
    color[0] = color[1] = color[2] = 0;
    Frame();
    Press("field", 0, 120, 116);
    EXPECT_EQ(State().ChainTarget, 2);
    Release();
}

TEST_F(ZUIColorPickerTest, NewPointerActionsRespectDisabledAndDoNotChangeTabSequence)
{
    Frame();
    const auto original = std::array{color[0], color[1], color[2], color[3]};
    for (int i = 0; i < 6; ++i)
    {
        EXPECT_TRUE(Find(Key("vertex", i))->Flags & ZUI_NoKeyboardFocus);
        EXPECT_TRUE(Find(Key("vertex", i))->Flags & ZUI_DrawBackground);
        EXPECT_FLOAT_EQ(Find(Key("vertex", i))->Colors[0][3], 0.f);
    }
    EXPECT_TRUE(Find(Key("hue"))->Flags & ZUI_NoKeyboardFocus);
    ctx.Disabled = true;
    Press("vertex", 4, 11, 11);
    Release();
    Press("hue", 0, 10, 10);
    Release();
    const auto ring = BuildChain({87, 157, 227})[1];
    Press("field", 0, static_cast<float>(ring.X), static_cast<float>(ring.Y));
    ctx.MousePos[0] += 100;
    Release();
    for (int i = 0; i < 4; ++i)
        EXPECT_FLOAT_EQ(color[i], original[i]);
    EXPECT_EQ(ctx.ColorUndoCount, 0u);
    ctx.Disabled   = false;
    ctx.FocusKey   = Key("model", 2);
    ctx.TabPressed = true;
    Frame();
    EXPECT_EQ(ctx.FocusKey, Key("field"));
}

TEST_F(ZUIColorPickerTest, OutsideHexagonPressIsIgnoredAndDisabledInputCannotChangeColor)
{
    const auto original = std::array{color[0], color[1], color[2], color[3]};
    Frame();
    EXPECT_FALSE(Press("field", 0, 5, 5));
    EXPECT_EQ(State().DragKey, 0u);
    EXPECT_FALSE(Release());
    ctx.Disabled          = true;
    ctx.FocusKey          = Key("bar");
    ctx.ArrowRightPressed = true;
    EXPECT_FALSE(Frame());
    for (int i = 0; i < 4; ++i)
        EXPECT_FLOAT_EQ(color[i], original[i]);
    EXPECT_EQ(ctx.ColorUndoCount, 0u);
}

TEST_F(ZUIColorPickerTest, BarsAndTracksClampUseShiftAndDoNotNavigateFocus)
{
    Frame();
    ctx.ShiftDown = true;
    EXPECT_TRUE(Nudge("bar", 0));
    EXPECT_EQ(State().Hsb.B, 99);
    EXPECT_EQ(ctx.FocusKey, Key("bar", 0));
    ctx.EndPressed = true;
    EXPECT_TRUE(Frame());
    EXPECT_EQ(State().Hsb.B, 100);
    ctx.HomePressed = true;
    EXPECT_TRUE(Frame());
    EXPECT_EQ(State().Hsb.B, 0);
    EXPECT_TRUE(Nudge("bar", 0));
    EXPECT_EQ(State().Hsb.B, 10);
    const int brightness = State().Hsb.B;
    EXPECT_TRUE(Nudge("bar", 1, false));
    EXPECT_EQ(State().Hsb.B, brightness);
    EXPECT_EQ(ctx.FocusKey, Key("bar", 1));
    Focus("channelTrack", 3);
    ctx.EndPressed = true;
    Frame();
    EXPECT_FLOAT_EQ(color[3], 1.f);
    ctx.HomePressed = true;
    EXPECT_TRUE(Frame());
    EXPECT_FLOAT_EQ(color[3], 0.f);
    EXPECT_EQ(Label("channelValue", 3), "0");
    EXPECT_EQ(State().Hsb.B, brightness);
}

TEST_F(ZUIColorPickerTest, WholeNumberFieldsRoundClampAndRejectJunk)
{
    Frame();
    const char* inputs[]   = {"12.5", "99999999999999", "-42.5", "128junk", "nan", ".5"};
    const int   expected[] = {13, 255, 0, 0, 0, 1};
    for (int i = 0; i < 6; ++i)
    {
        Type("channelValue", inputs[i]);
        Frame();
        EXPECT_EQ(ToByte(color[0]), expected[i]) << inputs[i];
        EXPECT_EQ(Label("channelValue"), std::to_string(expected[i]));
    }
}

TEST_F(ZUIColorPickerTest, HexUsesRealTextEditorAppliesAlphaAndInvalidTextReverts)
{
    Frame();
    Type("hex", "#1aF");
    EXPECT_TRUE(Frame());
    EXPECT_EQ(Label("hex"), "#11AAFF");
    EXPECT_EQ(ctx.ColorUndoCount, 1u);
    EXPECT_EQ(ctx.RecentColorCount, 1u);
    Type("hex", "10203080");
    EXPECT_TRUE(Frame());
    EXPECT_EQ(Label("hex"), "#10203080");
    EXPECT_FLOAT_EQ(color[3], 128.f / 255.f);
    Type("hex", "123badjunk");
    EXPECT_FALSE(Frame());
    EXPECT_EQ(Label("hex"), "#10203080");
    EXPECT_EQ(ctx.ColorUndoCount, 2u);
}

TEST_F(ZUIColorPickerTest, AchromaticHueAndSaturationSurviveFramesAndModelSwitches)
{
    color[0] = color[1] = color[2] = 0.f;
    ctx.ColorModel                 = ZUIColorModel::HSB;
    Frame();
    Type("channelValue", "210", 0);
    Frame();
    Type("channelValue", "62", 1);
    Frame();
    EXPECT_EQ(State().Hsb.H, 210);
    EXPECT_EQ(State().Hsb.S, 62);
    for (int i = 0; i < 3; ++i)
        EXPECT_FALSE(Frame());
    EXPECT_EQ(State().Hsb.H, 210);
    EXPECT_EQ(State().Hsb.S, 62);
    Type("channelValue", "89", 2);
    Frame();
    EXPECT_EQ(State().Hsb, (HSB{210, 62, 89}));
    const auto expected = HsbToRgb(210, 62, 89);
    EXPECT_EQ(ToByte(color[0]), expected.R);
    EXPECT_EQ(ToByte(color[1]), expected.G);
    EXPECT_EQ(ToByte(color[2]), expected.B);
    const auto original = std::array{color[0], color[1], color[2]};
    Focus("model", 2);
    ctx.EnterPressed = true;
    EXPECT_FALSE(Frame());
    EXPECT_EQ(ctx.ColorModel, ZUIColorModel::HSL);
    for (int i = 0; i < 3; ++i)
        EXPECT_FLOAT_EQ(color[i], original[i]);
}

TEST_F(ZUIColorPickerTest, RecentIsDeduplicatedBoundedAndSharedBetweenWidgets)
{
    Frame();
    for (int i = 0; i < 28; ++i)
    {
        EXPECT_TRUE(Nudge("channelTrack"));
    }
    EXPECT_EQ(ctx.RecentColorCount, 24u);
    const RGBA newest = ctx.RecentColors[0];
    const RGBA older  = ctx.RecentColors[10];
    Frame();
    Focus("recent", 10);
    ctx.EnterPressed = true;
    EXPECT_TRUE(Frame());
    EXPECT_EQ(ctx.RecentColorCount, 24u);
    EXPECT_EQ(ctx.RecentColors[0], older);
    EXPECT_EQ(ctx.RecentColors[1], newest);
    ZUIBeginFrame(&ctx, .016f);
    ZUIBeginColumn(&ctx, "root", ZPx(800), ZPx(900));
    EXPECT_FALSE(ZUIColorPicker4(&ctx, "other", color));
    ZUIEndColumn(&ctx);
    ZUIEndFrame(&ctx);
    EXPECT_EQ(ctx.RecentColorCount, 24u);
}

TEST_F(ZUIColorPickerTest, OldColorAndUndoPreserveExactHdrAndNegativeStorage)
{
    color[0] = 4.5f;
    color[1] = .18f;
    color[2] = -.25f;
    color[3] = .37f;
    Frame();
    EXPECT_TRUE(Nudge("channelTrack", 0, false));
    EXPECT_FLOAT_EQ(color[1], .18f);
    EXPECT_FLOAT_EQ(color[2], -.25f);
    EXPECT_FLOAT_EQ(color[3], .37f);
    Press("preview", 0, 5, 5);
    Release();
    EXPECT_FLOAT_EQ(color[0], 4.5f);
    EXPECT_FLOAT_EQ(color[1], .18f);
    EXPECT_FLOAT_EQ(color[2], -.25f);
    EXPECT_FLOAT_EQ(color[3], .37f);
    Focus("field");
    ctx.CtrlZPressed = true;
    EXPECT_TRUE(Frame());
    EXPECT_EQ(ToByte(color[0]), 254);
    Focus("field");
    ctx.CtrlZPressed = true;
    EXPECT_TRUE(Frame());
    EXPECT_FLOAT_EQ(color[0], 4.5f);
    color[0] = .123456f; // External changes must not be overwritten by redo.
    Focus("field");
    ctx.CtrlYPressed = true;
    EXPECT_FALSE(Frame());
    EXPECT_FLOAT_EQ(color[0], .123456f);
}

TEST_F(ZUIColorPickerTest, PopupTabOrderFocusRingAndEscapeStayInsideTheParentModal)
{
    withModal = true;
    ZUIOpenModal(&ctx, "settings");
    ctx.KeyboardFocus = true;
    ZUIOpenPopup(&ctx, "p", 0, 22);
    Frame(true);
    Frame(true);
    const std::array<uint64_t, 15> order = {Key("model", 0), Key("model", 1), Key("model", 2), Key("field"), Key("bar", 0), Key("bar", 1), Key("channelTrack", 0), Key("channelValue", 0), Key("channelTrack", 1), Key("channelValue", 1), Key("channelTrack", 2), Key("channelValue", 2), Key("channelTrack", 3), Key("channelValue", 3), Key("hex")};
    EXPECT_EQ(ctx.FocusKey, order[0]);
    for (size_t i = 1; i < order.size(); ++i)
    {
        ctx.TabPressed = true;
        Frame(true);
        EXPECT_EQ(ctx.FocusKey, order[i]) << i;
        auto* box = Find(order[i]);
        ASSERT_NE(box, nullptr);
        EXPECT_TRUE(box->Flags & ZUI_KeyboardFocused);
    }
    ctx.TabPressed = true;
    Frame(true);
    EXPECT_EQ(ctx.FocusKey, order[0]);
    Focus("channelTrack");
    ctx.ArrowUpPressed = true;
    EXPECT_TRUE(Frame(true));
    EXPECT_EQ(ctx.FocusKey, Key("channelTrack"));
    EXPECT_EQ(ctx.PopupStackSize, 1u);
    Focus("channelValue");
    Frame(true);
    ctx.EscapePressed = true;
    Frame(true);
    EXPECT_EQ(ctx.PopupStackSize, 0u);
    EXPECT_EQ(ctx.ActiveModalKey, ZUIHashStr("settings", 8));
}

TEST_F(ZUIColorPickerTest, BadgeAndVerticesRemainInteractiveInAPopupInsideAModal)
{
    withModal = true;
    ZUIOpenModal(&ctx, "settings");
    ZUIOpenPopup(&ctx, "p", 0, 22);
    Frame(true);
    Frame(true);
    auto* badge = Find(Key("hue"));
    ASSERT_NE(badge, nullptr);
    ctx.MousePos[0] = (badge->ScreenMin[0] + badge->ScreenMax[0]) * .5f;
    ctx.MousePos[1] = (badge->ScreenMin[1] + badge->ScreenMax[1]) * .5f;
    Frame(true);
    ASSERT_EQ(ctx.HotKey, Key("hue"));
    ctx.MousePressed[0] = ctx.MouseDown[0] = true;
    Frame(true);
    ASSERT_EQ(State().DragKey, Key("hue"));
    const auto* field = Find(Key("field"));
    ctx.MousePos[0]   = field->ScreenMin[0] + 120;
    ctx.MousePos[1]   = field->ScreenMin[1] + 13;
    EXPECT_TRUE(Frame(true));
    EXPECT_EQ(State().Hsb.H, 90);
    ctx.MouseDown[0]     = false;
    ctx.MouseReleased[0] = true;
    Frame(true);
    EXPECT_EQ(ctx.ColorUndoCount, 1u);
    ctx.CtrlZPressed = true;
    EXPECT_TRUE(Frame(true));
    auto* vertex = Find(Key("vertex", 4));
    ASSERT_NE(vertex, nullptr);
    ctx.MousePos[0] = (vertex->ScreenMin[0] + vertex->ScreenMax[0]) * .5f;
    ctx.MousePos[1] = (vertex->ScreenMin[1] + vertex->ScreenMax[1]) * .5f;
    Frame(true);
    ASSERT_EQ(ctx.HotKey, vertex->Key);
    ctx.MousePressed[0] = ctx.MouseDown[0] = true;
    Frame(true);
    ctx.MouseDown[0]     = false;
    ctx.MouseReleased[0] = true;
    EXPECT_TRUE(Frame(true));
    EXPECT_EQ(State().Hsb, (HSB{240, 100, 100}));
    EXPECT_EQ(ctx.ColorUndoCount, 1u); // New edit replaces the undone branch.
}

TEST_F(ZUIColorPickerTest, PopupHexEnterAndOutsideBlurApplyBeforeDismissal)
{
    ZUIOpenPopup(&ctx, "p", 0, 22);
    Frame(true);
    Frame(true);
    Focus("hex");
    Frame(true);
    ctx.CtrlAPressed = true;
    Frame(true);
    snprintf(ctx.TextInput, sizeof(ctx.TextInput), "%s", "#C0FFEE");
    ctx.TextInputLen = 7;
    ctx.EnterPressed = true;
    EXPECT_TRUE(Frame(true));
    EXPECT_EQ(ctx.PopupStackSize, 0u);
    EXPECT_EQ(ToByte(color[0]), 192);
    EXPECT_EQ(ToByte(color[1]), 255);
    EXPECT_EQ(ToByte(color[2]), 238);
    ZUIOpenPopup(&ctx, "p", 0, 22);
    Frame(true);
    Frame(true);
    Focus("hex");
    Frame(true);
    snprintf(State().HexBuffer, sizeof(State().HexBuffer), "%s", "#010203");
    ctx.MousePos[0]     = 700;
    ctx.MousePos[1]     = 800;
    ctx.MousePressed[0] = ctx.MouseDown[0] = true;
    EXPECT_TRUE(Frame(true));
    EXPECT_EQ(ctx.PopupStackSize, 0u);
    EXPECT_EQ(ToByte(color[0]), 1);
    EXPECT_EQ(ctx.ColorUndoCount, 2u);
}

TEST_F(ZUIColorPickerTest, HslFieldUsesItsFrozenLightnessAndCenterKeepsHue)
{
    const auto rgb = HslToRgb(210, 70, 25);
    color[0]       = rgb.R / 255.f;
    color[1]       = rgb.G / 255.f;
    color[2]       = rgb.B / 255.f;
    ctx.ColorModel = ZUIColorModel::HSL;
    Frame();
    const int originalL   = State().Hsl.L;
    const int originalHue = State().Hsb.H;
    Press("field", 0, 144, 116);
    ctx.MousePos[0] = 13 + 216;
    Frame();
    EXPECT_EQ(State().Hsl.L, 50);
    ctx.MousePos[0] = 13 + 144;
    Frame();
    EXPECT_NEAR(State().Hsl.L, originalL, 1);
    ctx.MousePos[0] = 13 + 120;
    Frame();
    EXPECT_EQ(State().Hsb.H, originalHue);
    EXPECT_EQ(State().Hsb.S, 0);
    Release();
    EXPECT_EQ(ctx.ColorUndoCount, 1u);
}

TEST_F(ZUIColorPickerTest, SliderPressDragAndReleaseOutsideAreOneGesture)
{
    Frame();
    EXPECT_TRUE(Press("channelTrack", 0, 106, 11));
    EXPECT_EQ(ToByte(color[0]), 128);
    EXPECT_EQ(Label("channelValue", 0), "128");
    EXPECT_EQ(ctx.ColorUndoCount, 0u);
    ctx.MousePos[0] = -500;
    EXPECT_TRUE(Frame());
    EXPECT_EQ(ToByte(color[0]), 0);
    ctx.MousePos[0] = 5000;
    EXPECT_TRUE(Release());
    EXPECT_EQ(ToByte(color[0]), 255);
    EXPECT_EQ(ctx.ColorUndoCount, 1u);
    EXPECT_EQ(ctx.RecentColorCount, 1u);
}

TEST_F(ZUIColorPickerTest, UndoCapacityIsBoundedAndAnEditAfterUndoDropsRedo)
{
    Frame();
    for (int i = 0; i < 80; ++i)
        EXPECT_TRUE(Nudge("channelTrack"));
    EXPECT_EQ(ctx.ColorUndoCount, ZUIContext::ColorUndoCapacity);
    EXPECT_EQ(ctx.ColorUndoCursor, ctx.ColorUndoCount);
    ctx.CtrlZPressed = true;
    EXPECT_TRUE(Frame());
    EXPECT_EQ(ctx.ColorUndoCursor, ctx.ColorUndoCount - 1);
    EXPECT_TRUE(Nudge("channelTrack", 1));
    EXPECT_EQ(ctx.ColorUndoCursor, ctx.ColorUndoCount);
    ctx.CtrlYPressed = true;
    EXPECT_FALSE(Frame());
}

TEST_F(ZUIColorPickerTest, RebindingToAnotherObjectWithTheSameColorDoesNotReplayItsUndo)
{
    Frame();
    EXPECT_TRUE(Nudge("channelTrack", 0));
    ASSERT_EQ(ctx.ColorUndoCount, 1u);
    float       other[4] = {color[0], color[1], color[2], color[3]};
    const float expected = other[0];
    Focus("channelTrack", 0);
    ctx.CtrlZPressed = true;
    ZUIBeginFrame(&ctx, .016f);
    ZUIBeginColumn(&ctx, "root", ZPx(800), ZPx(900));
    EXPECT_FALSE(ZUIColorPicker4(&ctx, "p", other));
    ZUIEndColumn(&ctx);
    ZUIEndFrame(&ctx);
    EXPECT_FLOAT_EQ(other[0], expected);
    EXPECT_EQ(ctx.ColorUndoCursor, 1u);
    EXPECT_FLOAT_EQ(State().OldStored[0], expected);
    EXPECT_NE(State().BindingIdentity, ctx.ColorUndo[0].BindingIdentity);
}

TEST_F(ZUIColorPickerTest, PopupLinearBoundaryChangesOnlyTheEditedChannelAndRgbHasNoAlphaAccess)
{
    color[0] = .18f;
    color[1] = 4.5f;
    color[2] = -.25f;
    color[3] = .37f;
    ZUIOpenPopup(&ctx, "p", 0, 22);
    Frame(true, ZUIColorSpace::Linear);
    Frame(true, ZUIColorSpace::Linear);
    Focus("channelTrack");
    ctx.ArrowRightPressed = true;
    EXPECT_TRUE(Frame(true, ZUIColorSpace::Linear));
    EXPECT_NEAR(color[0], SrgbToLinear(119.0 / 255), 1e-7);
    EXPECT_FLOAT_EQ(color[1], 4.5f);
    EXPECT_FLOAT_EQ(color[2], -.25f);
    EXPECT_FLOAT_EQ(color[3], .37f);
    ctx.CtrlZPressed = true;
    EXPECT_TRUE(Frame(true, ZUIColorSpace::Linear));
    EXPECT_FLOAT_EQ(color[0], .18f);
    ZUIClosePopup(&ctx);
    struct GuardedRgb
    {
        float Rgb[3] = {.18f, .18f, .18f};
        float Guard  = 42.f;
    } guarded     = {};
    auto rgbFrame = [&] {
        ZUIBeginFrame(&ctx, .016f);
        ZUIBeginColumn(&ctx, "root", ZPx(800), ZPx(900));
        bool changed = ZUIColorEdit3(&ctx, "p", guarded.Rgb, ZUIColorSpace::Linear);
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
        return changed;
    };
    ZUIOpenPopup(&ctx, "p", 0, 22);
    rgbFrame();
    rgbFrame();
    EXPECT_EQ(Find(Key("channelTrack", 3)), nullptr);
    Focus("hex");
    rgbFrame();
    snprintf(State().HexBuffer, sizeof(State().HexBuffer), "%s", "#12345600");
    ctx.EnterPressed = true;
    EXPECT_TRUE(rgbFrame());
    EXPECT_FLOAT_EQ(guarded.Guard, 42.f);
    EXPECT_NEAR(guarded.Rgb[0], SrgbToLinear(18.0 / 255), 1e-7);
}

TEST_F(ZUIColorPickerTest, DragInterruptionAndExternalChangesCannotApplyStaleGestureState)
{
    Frame();
    Press("field", 0, 144, 116);
    EXPECT_TRUE(State().TransactionActive);
    color[0] = .123456f;
    color[1] = .654321f;
    color[2] = .345678f;
    EXPECT_FALSE(Frame());
    EXPECT_EQ(State().DragKey, 0u);
    EXPECT_FALSE(State().TransactionActive);
    EXPECT_EQ(ctx.ColorUndoCount, 0u);
    EXPECT_FLOAT_EQ(color[0], .123456f);
    Release();
    Press("channelTrack", 0, 150, 11);
    ctx.Disabled = true;
    Frame();
    EXPECT_EQ(State().DragKey, 0u);
    EXPECT_FALSE(State().TransactionActive);
    const auto committed = color[0];
    ctx.Disabled         = false;
    ctx.MousePos[0]      = -10000;
    EXPECT_FALSE(Frame());
    EXPECT_FLOAT_EQ(color[0], committed);
    Release();
}

TEST_F(ZUIColorPickerTest, NonFinitePointerNeverCreatesAnInvalidColor)
{
    Frame();
    Press("field", 0, 150, 100);
    const auto original = std::array{color[0], color[1], color[2], color[3]};
    ctx.MousePos[0]     = std::numeric_limits<float>::quiet_NaN();
    ctx.MousePos[1]     = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(Frame());
    EXPECT_FALSE(Release());
    for (int i = 0; i < 4; ++i)
        EXPECT_FLOAT_EQ(color[i], original[i]);
    EXPECT_EQ(State().DragKey, 0u);
}

TEST_F(ZUIColorPickerTest, HiddenWidgetCommitsItsInterruptedDragWhenTheUnchangedBindingReturns)
{
    Frame();
    const auto before = std::array{color[0], color[1], color[2], color[3]};
    EXPECT_TRUE(Press("channelTrack", 0, 150, 11));
    const auto after = std::array{color[0], color[1], color[2], color[3]};
    EXPECT_TRUE(State().TransactionActive);
    ctx.MouseDown[0]     = false;
    ctx.MouseReleased[0] = true;
    ZUIBeginFrame(&ctx, .016f);
    ZUIBeginColumn(&ctx, "root", ZPx(800), ZPx(900));
    ZUIEndColumn(&ctx); // The picker is hidden for the release frame.
    ZUIEndFrame(&ctx);
    EXPECT_FALSE(Frame());
    EXPECT_EQ(State().DragKey, 0u);
    EXPECT_FALSE(State().TransactionActive);
    EXPECT_EQ(ctx.ColorUndoCount, 1u);
    EXPECT_EQ(ctx.RecentColorCount, 1u);
    Focus("channelTrack");
    ctx.CtrlZPressed = true;
    EXPECT_TRUE(Frame());
    for (int i = 0; i < 4; ++i)
        EXPECT_FLOAT_EQ(color[i], before[i]);
    ctx.CtrlYPressed = true;
    EXPECT_TRUE(Frame());
    for (int i = 0; i < 4; ++i)
        EXPECT_FLOAT_EQ(color[i], after[i]);
}

TEST_F(ZUIColorPickerTest, ModelSwitchCommitsNumberInTheOriginalModelWithoutStealingActivation)
{
    Frame();
    Focus("channelValue");
    Frame();
    snprintf(State().Channels[0].Buffer, sizeof(State().Channels[0].Buffer), "%s", "128");
    Focus("model", 1);
    ctx.EnterPressed = true;
    EXPECT_TRUE(Frame());
    EXPECT_EQ(ctx.ColorModel, ZUIColorModel::HSB);
    EXPECT_EQ(ToByte(color[0]), 128);
    EXPECT_EQ(Label("channelValue", 0), std::to_string(State().Hsb.H));
    EXPECT_EQ(ctx.ColorUndoCount, 1u);
}

TEST_F(ZUIColorPickerTest, BlurAppliesTextBeforeTheNextSliderEditAndTextUndoDoesNotUndoTheColor)
{
    Frame();
    Focus("hex");
    Frame();
    snprintf(State().HexBuffer, sizeof(State().HexBuffer), "%s", "#102030");
    EXPECT_TRUE(Nudge("channelTrack", 0));
    EXPECT_EQ(ToByte(color[0]), 17);
    EXPECT_EQ(ToByte(color[1]), 32);
    EXPECT_EQ(ToByte(color[2]), 48);
    EXPECT_EQ(Label("hex"), "#112030");
    const auto committed = std::array{color[0], color[1], color[2], color[3]};
    Focus("hex");
    Frame();
    ctx.CtrlAPressed = true;
    Frame();
    snprintf(ctx.TextInput, sizeof(ctx.TextInput), "%s", "bad");
    ctx.TextInputLen = 3;
    EXPECT_FALSE(Frame());
    ctx.CtrlZPressed = true;
    EXPECT_FALSE(Frame());
    for (int i = 0; i < 4; ++i)
        EXPECT_FLOAT_EQ(color[i], committed[i]);
    EXPECT_EQ(ctx.ColorUndoCursor, ctx.ColorUndoCount);
}

TEST_F(ZUIColorPickerTest, SpaceOpensSwatchAndEscapeKeepsLiveEditsAndRestoresFocus)
{
    Frame(true);
    Focus("swatch");
    ctx.SpacePressed = true;
    EXPECT_FALSE(Frame(true));
    EXPECT_EQ(ctx.PopupStackSize, 1u);
    EXPECT_FALSE(Frame(true));
    Focus("channelTrack", 1);
    ctx.ArrowRightPressed = true;
    EXPECT_TRUE(Frame(true));
    const auto edited = color[1];
    ctx.EscapePressed = true;
    EXPECT_FALSE(Frame(true));
    EXPECT_EQ(ctx.PopupStackSize, 0u);
    EXPECT_EQ(ctx.FocusKey, Key("swatch"));
    EXPECT_FLOAT_EQ(color[1], edited);
    EXPECT_EQ(ctx.ColorUndoCount, 1u);
}

TEST_F(ZUIColorPickerTest, LongRunningInteractionsDoNotGrowPersistentOrFrameMemory)
{
    Frame();
    // Warm up every interactive subpart and fill Recent before measuring.
    for (int i = 0; i < 24; ++i)
        Nudge("channelTrack");
    for (int i = 0; i < 3; ++i)
    {
        ctx.ColorModel = static_cast<ZUIColorModel>(i);
        Frame();
    }
    Frame();
    const auto persistent = ctx.PersistentArena.m_current_offset;
    const auto stateCount = ctx.StateStore.Count;
    const auto frameBytes = ctx.FrameArena.m_current_offset;
    for (int i = 0; i < 1000; ++i)
    {
        Nudge("channelTrack", i % 4, i % 7 > 3);
        EXPECT_EQ(ctx.PersistentArena.m_current_offset, persistent);
        EXPECT_EQ(ctx.StateStore.Count, stateCount);
        EXPECT_LE(ctx.FrameArena.m_current_offset, frameBytes + 1024);
        EXPECT_LE(ctx.ColorUndoCount, ctx.ColorUndoCapacity);
    }
}

TEST_F(ZUIColorPickerTest, AllModelButtonsReflectTheNewSelectionInTheSameFrame)
{
    Frame();
    constexpr float selectedBackground[4] = ZUI_TOKEN(Tokens::AccentSelection);
    constexpr float normalBackground[4]   = ZUI_TOKEN(Tokens::BgControl);
    constexpr float selectedBorder[4]     = ZUI_TOKEN(Tokens::AccentBlue);
    constexpr float normalBorder[4]       = ZUI_TOKEN(Tokens::BorderField);
    for (const int selected : {2, 0, 1})
    {
        Focus("model", selected);
        ctx.EnterPressed = true;
        EXPECT_FALSE(Frame());
        ASSERT_EQ(ctx.ColorModel, static_cast<ZUIColorModel>(selected));
        for (int i = 0; i < 3; ++i)
        {
            const auto* button = Find(Key("model", i));
            ASSERT_NE(button, nullptr);
            for (int channel = 0; channel < 4; ++channel)
            {
                EXPECT_FLOAT_EQ(button->Colors[0][channel], (i == selected ? selectedBackground : normalBackground)[channel]);
                EXPECT_FLOAT_EQ(button->BorderColor[channel], (i == selected ? selectedBorder : normalBorder)[channel]);
            }
        }
    }
}

TEST_F(ZUIColorPickerTest, ColorStateConstructionRestoresDefaultsInDirtyArenaStorage)
{
    auto* storage = ctx.PersistentArena.Allocate(sizeof(ZUIColorWidgetState));
    ASSERT_NE(storage, nullptr);
    secure_memset(storage, 0xa5, sizeof(ZUIColorWidgetState), sizeof(ZUIColorWidgetState));
    const auto* state = new (storage) ZUIColorWidgetState{};
    EXPECT_EQ(state->ChainTarget, -1);
    EXPECT_TRUE(state->BoundAlpha);
    EXPECT_EQ(state->BoundSpace, ZUIColorSpace::Srgb);
    EXPECT_EQ(state->ChannelModel, ZUIColorModel::RGB);
    EXPECT_EQ(state->OldColor, (RGBA{0, 0, 0, 255}));
    EXPECT_EQ(state->Hsb, (HSB{}));
    EXPECT_EQ(state->Hsl, (HSL{}));
    EXPECT_DOUBLE_EQ(state->Origin.Brightness, 100.0);
    EXPECT_DOUBLE_EQ(state->Origin.Lightness, 50.0);
    EXPECT_EQ(state->DragKey, 0u);
    EXPECT_EQ(state->BindingIdentity, 0u);
    EXPECT_EQ(state->LastPickerFrame, 0u);
    EXPECT_FALSE(state->Initialized);
    EXPECT_FALSE(state->HexEditing);
    EXPECT_FALSE(state->TransactionActive);
    EXPECT_FALSE(state->RestoreOld);
    for (const char c : state->HexBuffer)
        EXPECT_EQ(c, '\0');
    const auto check_number = [](const ZUIColorNumberState& number) {
        EXPECT_FALSE(number.Editing);
        EXPECT_FALSE(number.Dragging);
        EXPECT_FALSE(number.Pressed);
        EXPECT_FLOAT_EQ(number.PressX, 0.f);
        EXPECT_EQ(number.PressValue, 0);
        for (const char c : number.Buffer)
            EXPECT_EQ(c, '\0');
    };
    for (const auto& number : state->Numbers)
        check_number(number);
    for (const auto& number : state->Channels)
        check_number(number);
}
