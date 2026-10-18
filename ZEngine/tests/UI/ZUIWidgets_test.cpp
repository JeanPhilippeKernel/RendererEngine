#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIInput.h>
#include <ZEngine/UI/ZUILayout.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <gtest/gtest.h>
#include <limits>

using namespace ZEngine::UI;
using namespace ZEngine::Helpers;

class ZUIBehaviorTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager memory     = {};
    ZUIContext                           ctx        = {};
    ZUIGlyph                             glyphs[96] = {};
    ZUIFont                              font       = {};
    ZUIFontAtlas                         atlas      = {};

    void                                 SetUp() override
    {
        memory.Initialize(ZMega(8), {});
        ZUIContextInit(&ctx, &memory.MainArena, ZMega(4), ZMega(1), 1024, 1024);
        ctx.ScreenW     = 400;
        ctx.ScreenH     = 300;
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
    void Begin(float dt = 1.f / 60.f)
    {
        ZUIBeginFrame(&ctx, dt);
        ZUIBeginColumn(&ctx, "root", ZPx(400.f), ZPx(300.f));
    }
    void End()
    {
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
    }
    ZUIBox* Control(const char* key, float x, float y, float w = 40.f, float h = 22.f, ZUIBoxFlags extra = ZUI_None)
    {
        auto* box        = ZUIPushBox(&ctx, key, (uint32_t) secure_strlen(key), ZUI_Clickable | ZUI_FloatX | ZUI_FloatY | extra);
        box->Size[0]     = ZPx(w);
        box->Size[1]     = ZPx(h);
        box->FloatPos[0] = x;
        box->FloatPos[1] = y;
        ZUIPopBox(&ctx);
        return box;
    }
    uint64_t ColorKey(const char* owner, const char* part, int index = 0)
    {
        char key[64] = {};
        snprintf(key, sizeof(key), "##color_%016llx_%s%d", static_cast<unsigned long long>(ZUIHashStr(owner, static_cast<uint32_t>(secure_strlen(owner)))), part, index);
        return ZUIHashStr(key, static_cast<uint32_t>(secure_strlen(key)));
    }
    ZUIBox* Find(ZUIBox* root, uint64_t key)
    {
        if (root->Key == key)
            return root;
        for (auto* child = root->FirstChild; child; child = child->NextSib)
            if (auto* found = Find(child, key))
                return found;
        return nullptr;
    }
    std::string Label(const ZUIBox* box)
    {
        return box && box->Label.Ptr ? std::string(box->Label.Ptr, box->Label.Len) : std::string{};
    }
    ZUIColorWidgetState* ColorState(const char* owner)
    {
        return ZUIStateGetOrInsert(&ctx.StateStore, ZUIHashStr(owner, static_cast<uint32_t>(secure_strlen(owner))))->ColorState;
    }
    void StartColorText(const char* owner, int index)
    {
        ctx.FocusKey      = ColorKey(owner, "number", index);
        ctx.KeyboardFocus = true;
    }
};

TEST_F(ZUIBehaviorTest, CollapsingHeaderUsesLucideChevronAndUpdatesItOnKeyboardToggle)
{
    bool open = true;
    for (int frame = 0; frame < 3; ++frame)
    {
        ctx.FocusKey     = ZUIHashStr("##ch_Section", 12);
        ctx.EnterPressed = frame == 1;
        ctx.SpacePressed = frame == 2;
        Begin();
        ZUICollapsingHeader(&ctx, "Section", &open);
        const auto* header = ctx.Current->LastChild;
        const auto* arrow  = header->FirstChild;
        End();
        EXPECT_EQ(open, frame != 1);
        ASSERT_NE(arrow, nullptr);
        EXPECT_TRUE(arrow->Flags & ZUI_DrawIcon);
        EXPECT_FALSE(arrow->Flags & ZUI_DrawTriArrow);
        EXPECT_EQ(arrow->IconId, (uint16_t) (open ? ZUIIcon::ChevronDown : ZUIIcon::ChevronRight));
        EXPECT_FLOAT_EQ(arrow->ComputedSize[0], 11.f);
        EXPECT_FLOAT_EQ(arrow->ComputedSize[1], 11.f);
        EXPECT_FLOAT_EQ(arrow->ScreenMin[1] + arrow->ScreenMax[1], header->ScreenMin[1] + header->ScreenMax[1]);
    }
}

TEST_F(ZUIBehaviorTest, RenameInputEdgeSurvivesBeginFrameAndClearsAtEndFrame)
{
    EXPECT_FALSE(ctx.F2Pressed);
    ZUIFeedKey(&ctx, ZUIKey::F2, true, false, false, false);
    ZUIFeedKey(&ctx, ZUIKey::F2, false, false, false, false);
    Begin();
    EXPECT_TRUE(ctx.F2Pressed);
    End();
    EXPECT_FALSE(ctx.F2Pressed);
}

TEST_F(ZUIBehaviorTest, ColorEditSwatchAndFieldsUseTheSameDisplayBytesWithoutChangingStorage)
{
    for (const auto space : {ZUIColorSpace::Srgb, ZUIColorSpace::Linear})
    {
        float color[4] = {0.18f, 0.18f, 0.18f, 0.5f};
        Begin();
        EXPECT_FALSE(ZUIColorEdit4(&ctx, "gray", color, space));
        const auto* swatch = Find(ctx.Root, ColorKey("gray", "swatch"));
        ASSERT_NE(swatch, nullptr);
        for (int i = 0; i < 3; ++i)
        {
            EXPECT_EQ(Label(Find(ctx.Root, ColorKey("gray", "number", i))), space == ZUIColorSpace::Linear ? "118" : "46");
            EXPECT_EQ(ColorMath::ToByte(swatch->Colors[0][i]), space == ZUIColorSpace::Linear ? 118 : 46);
            EXPECT_FLOAT_EQ(color[i], 0.18f);
        }
        EXPECT_EQ(ColorMath::ToByte(swatch->Colors[0][3]), 128);
        EXPECT_FLOAT_EQ(color[3], 0.5f);
        EXPECT_EQ(Label(Find(ctx.Root, ColorKey("gray", "number", 3))), "50%");
        End();
    }
}

TEST_F(ZUIBehaviorTest, DisplayEncodedGridColorsKeepTheirExistingHexBytes)
{
    float color[4] = {0.9f, 0.2f, 0.2f, 1.0f};
    Begin();
    EXPECT_FALSE(ZUIColorEdit4(&ctx, "axis", color, ZUIColorSpace::Srgb));
    EXPECT_EQ(Label(Find(ctx.Root, ColorKey("axis", "number", 0))), "230");
    EXPECT_EQ(Label(Find(ctx.Root, ColorKey("axis", "number", 1))), "51");
    EXPECT_FLOAT_EQ(color[0], 0.9f);
    End();
}

TEST_F(ZUIBehaviorTest, OpeningColorPopupDoesNotQuantizeOrClampStoredHdrValues)
{
    float color[4] = {4.5f, 0.18f, -0.25f, 0.37f};
    Begin();
    ZUIOpenPopup(&ctx, "linear");
    EXPECT_FALSE(ZUIColorEdit4(&ctx, "linear", color, ZUIColorSpace::Linear));
    EXPECT_EQ(ctx.PendingPopupKey, ZUIHashStr("linear", 6));
    End(); // Popup requests are applied at the end of the frame.
    Begin();
    EXPECT_FALSE(ZUIColorEdit4(&ctx, "linear", color, ZUIColorSpace::Linear));
    EXPECT_EQ(ctx.PopupStackSize, 1u);
    ASSERT_NE(ctx.PopupStack[0].Box, nullptr);
    EXPECT_FLOAT_EQ(color[0], 4.5f);
    EXPECT_FLOAT_EQ(color[1], 0.18f);
    EXPECT_FLOAT_EQ(color[2], -0.25f);
    EXPECT_FLOAT_EQ(color[3], 0.37f);
    End();
}

TEST_F(ZUIBehaviorTest, ColorFieldConvertsEditedRgbOnceAndPreservesUntouchedChannels)
{
    float color[4] = {0.18f, 2.0f, -0.25f, 0.37f};
    Begin();
    StartColorText("linear", 0);
    EXPECT_FALSE(ZUIColorEdit4(&ctx, "linear", color, ZUIColorSpace::Linear));
    End();
    Begin();
    snprintf(ColorState("linear")->Numbers[0].Buffer, 16, "%s", "128");
    ctx.EnterPressed = true;
    EXPECT_TRUE(ZUIColorEdit4(&ctx, "linear", color, ZUIColorSpace::Linear));
    EXPECT_NEAR(color[0], ColorMath::SrgbToLinear(128.f / 255.f), 1.0e-7);
    EXPECT_FLOAT_EQ(color[1], 2.0f);
    EXPECT_FLOAT_EQ(color[2], -0.25f);
    EXPECT_FLOAT_EQ(color[3], 0.37f);
    End();
}

TEST_F(ZUIBehaviorTest, ColorFieldAlphaRemainsLinearAndDisabledFieldsCannotEdit)
{
    float color[4] = {0.18f, 2.0f, -0.25f, 0.37f};
    Begin();
    StartColorText("linear", 3);
    EXPECT_FALSE(ZUIColorEdit4(&ctx, "linear", color, ZUIColorSpace::Linear));
    End();
    Begin();
    snprintf(ColorState("linear")->Numbers[3].Buffer, 16, "%s", "38");
    ctx.EnterPressed = true;
    EXPECT_TRUE(ZUIColorEdit4(&ctx, "linear", color, ZUIColorSpace::Linear));
    EXPECT_FLOAT_EQ(color[3], 0.38f);
    EXPECT_FLOAT_EQ(color[0], 0.18f);
    EXPECT_FLOAT_EQ(color[1], 2.0f);
    EXPECT_FLOAT_EQ(color[2], -0.25f);
    End();
    Begin();
    StartColorText("linear", 3);
    ZUIBeginDisabled(&ctx);
    EXPECT_FALSE(ZUIColorEdit4(&ctx, "linear", color, ZUIColorSpace::Linear));
    EXPECT_FALSE(ZUIColorEdit4(&ctx, "null", nullptr));
    ZUIEndDisabled(&ctx);
    EXPECT_FLOAT_EQ(color[3], 0.38f);
    EXPECT_EQ(ctx.PopupStackSize, 0u);
    End();
}

TEST_F(ZUIBehaviorTest, ColorEditDimensionsMatchFigmaAndRgbNeverAccessesAlpha)
{
    struct GuardedColor
    {
        float Rgb[3] = {0.18f, 0.18f, 0.18f};
        float Guard  = 42.f;
    } color       = {};
    float rgba[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    Begin();
    EXPECT_FALSE(ZUIColorEdit3(&ctx, "rgb", color.Rgb, ZUIColorSpace::Linear));
    EXPECT_FALSE(ZUIColorEdit4(&ctx, "rgba", rgba));
    auto* rgbRow  = Find(ctx.Root, ColorKey("rgb", "edit"));
    auto* rgbaRow = Find(ctx.Root, ColorKey("rgba", "edit"));
    End();
    ASSERT_NE(rgbRow, nullptr);
    ASSERT_NE(rgbaRow, nullptr);
    EXPECT_FLOAT_EQ(rgbRow->ComputedSize[0], 262.f);
    EXPECT_FLOAT_EQ(rgbaRow->ComputedSize[0], 342.f);
    for (const char* owner : {"rgb", "rgba"})
    {
        auto* row = Find(ctx.Root, ColorKey(owner, "edit"));
        EXPECT_FLOAT_EQ(row->ComputedSize[1], 22.f);
        EXPECT_FLOAT_EQ(row->FirstChild->ComputedSize[0], 22.f);
        for (auto* field = row->FirstChild->NextSib; field; field = field->NextSib)
        {
            EXPECT_FLOAT_EQ(field->ComputedSize[0], 72.f);
            EXPECT_FLOAT_EQ(field->ScreenMin[0] - field->PrevSib->ScreenMax[0], 8.f);
            EXPECT_NE(field->CustomDraw, nullptr);
        }
    }
    EXPECT_EQ(Find(ctx.Root, ColorKey("rgb", "number", 3)), nullptr);
    EXPECT_FLOAT_EQ(color.Guard, 42.f);
}

TEST_F(ZUIBehaviorTest, ColorTextUsesBoundedWholeIntegersAndCommitCancelBlurRules)
{
    float       color[4]   = {0.18f, 2.f, -0.25f, 0.37f};
    const char* inputs[]   = {"999999999999999", "-99999", "128junk", "nan", "12.5", "", "+42"};
    const int   expected[] = {255, 0, 0, 0, 0, 0, 42};
    for (int i = 0; i < 7; ++i)
    {
        Begin();
        StartColorText("linear", 0);
        EXPECT_FALSE(ZUIColorEdit4(&ctx, "linear", color, ZUIColorSpace::Linear));
        End();
        Begin();
        snprintf(ColorState("linear")->Numbers[0].Buffer, 16, "%s", inputs[i]);
        ctx.EnterPressed   = true;
        const bool changed = ZUIColorEdit4(&ctx, "linear", color, ZUIColorSpace::Linear);
        EXPECT_EQ(changed, i == 0 || i == 1 || i == 6);
        EXPECT_EQ(ColorMath::ToByte(static_cast<float>(ColorMath::LinearToSrgb(color[0]))), expected[i]);
        EXPECT_FLOAT_EQ(color[1], 2.f);
        EXPECT_FLOAT_EQ(color[2], -0.25f);
        EXPECT_FLOAT_EQ(color[3], 0.37f);
        End();
    }
    Begin();
    StartColorText("linear", 0);
    ZUIColorEdit4(&ctx, "linear", color, ZUIColorSpace::Linear);
    End();
    const float previous = color[0];
    Begin();
    snprintf(ColorState("linear")->Numbers[0].Buffer, 16, "%s", "200");
    ctx.EscapePressed = true;
    EXPECT_FALSE(ZUIColorEdit4(&ctx, "linear", color, ZUIColorSpace::Linear));
    EXPECT_FALSE(ctx.EscapePressed);
    EXPECT_FLOAT_EQ(color[0], previous);
    End();
    Begin();
    StartColorText("linear", 0);
    ZUIColorEdit4(&ctx, "linear", color, ZUIColorSpace::Linear);
    End();
    Begin();
    snprintf(ColorState("linear")->Numbers[0].Buffer, 16, "%s", "200");
    ctx.FocusKey = 0; // Outside focus commits once.
    EXPECT_TRUE(ZUIColorEdit4(&ctx, "linear", color, ZUIColorSpace::Linear));
    EXPECT_NEAR(color[0], ColorMath::SrgbToLinear(200.f / 255.f), 1.e-7);
    End();
}

TEST_F(ZUIBehaviorTest, ColorTypingSelectsAllAndTabCommitsThenAdvancesInChannelOrder)
{
    float color[4] = {0.18f, 0.2f, 0.3f, 0.4f};
    Begin();
    StartColorText("c", 0);
    ZUIColorEdit4(&ctx, "c", color);
    Control("next", 0, 40);
    End();
    Begin();
    snprintf(ctx.TextInput, sizeof(ctx.TextInput), "%s", "128");
    ctx.TextInputLen = 3;
    EXPECT_FALSE(ZUIColorEdit4(&ctx, "c", color)); // Typing is not a stored edit yet.
    EXPECT_STREQ(ColorState("c")->Numbers[0].Buffer, "128");
    Control("next", 0, 40);
    End();
    Begin();
    ctx.TabPressed = true;
    EXPECT_TRUE(ZUIColorEdit4(&ctx, "c", color));
    Control("next", 0, 40);
    End();
    EXPECT_FLOAT_EQ(color[0], 128.f / 255.f);
    EXPECT_EQ(ctx.FocusKey, ColorKey("c", "number", 1));
    for (int i = 1; i < 4; ++i)
    {
        Begin();
        ZUIColorEdit4(&ctx, "c", color);
        Control("next", 0, 40);
        End();
        EXPECT_TRUE(ColorState("c")->Numbers[i].Editing);
        Begin();
        ctx.TabPressed = true;
        ZUIColorEdit4(&ctx, "c", color);
        Control("next", 0, 40);
        End();
        EXPECT_EQ(ctx.FocusKey, i == 3 ? ZUIHashStr("next", 4) : ColorKey("c", "number", i + 1));
    }
}

TEST_F(ZUIBehaviorTest, ColorScrubUsesTotalLogicalDistanceAndReleaseDoesNotEnterTyping)
{
    float      color[4] = {100.f / 255.f, 0.2f, 0.3f, 1.f};
    const auto key      = ColorKey("c", "number", 0);
    ctx.MousePos[0]     = 40.f;
    ctx.MousePos[1]     = 10.f;
    Begin();
    ctx.HotKey          = key;
    ctx.MousePressed[0] = ctx.MouseDown[0] = true;
    EXPECT_FALSE(ZUIColorEdit4(&ctx, "c", color));
    End();
    Begin();
    ctx.MousePos[0] = 50.4f;
    EXPECT_TRUE(ZUIColorEdit4(&ctx, "c", color));
    EXPECT_EQ(ColorMath::ToByte(color[0]), 110);
    End();
    Begin();
    ctx.MouseDown[0]     = false;
    ctx.MouseReleased[0] = true;
    ctx.MousePos[0]      = -500.f; // Release outside still applies/clamps and ends the gesture.
    EXPECT_TRUE(ZUIColorEdit4(&ctx, "c", color));
    EXPECT_FLOAT_EQ(color[0], 0.f);
    EXPECT_FALSE(ColorState("c")->Numbers[0].Editing);
    EXPECT_FALSE(ColorState("c")->Numbers[0].Pressed);
    End();
}

TEST_F(ZUIBehaviorTest, ColorClickToTypeSelectsAllAndQueuedEnterCommitsTheLatestCharacters)
{
    float      color[4] = {0.18f, 0.2f, 0.3f, 1.f};
    const auto key      = ColorKey("c", "number", 0);
    ctx.MousePos[0]     = 40;
    ctx.MousePos[1]     = 10;
    Begin();
    ctx.HotKey          = key;
    ctx.MousePressed[0] = ctx.MouseDown[0] = true;
    EXPECT_FALSE(ZUIColorEdit4(&ctx, "c", color, ZUIColorSpace::Linear));
    End();
    Begin();
    ctx.MouseDown[0]     = false;
    ctx.MouseReleased[0] = true;
    EXPECT_FALSE(ZUIColorEdit4(&ctx, "c", color, ZUIColorSpace::Linear));
    EXPECT_TRUE(ColorState("c")->Numbers[0].Editing);
    End();
    Begin();
    snprintf(ctx.TextInput, sizeof(ctx.TextInput), "%s", "128");
    ctx.TextInputLen = 3;
    ctx.EnterPressed = true;
    EXPECT_TRUE(ZUIColorEdit4(&ctx, "c", color, ZUIColorSpace::Linear));
    EXPECT_NEAR(color[0], ColorMath::SrgbToLinear(128.f / 255.f), 1.e-7);
    EXPECT_FALSE(ColorState("c")->Numbers[0].Editing);
    End();
}

TEST_F(ZUIBehaviorTest, ConfirmingTheDisplayedBytePreservesUnquantizedLinearStorage)
{
    float color[4] = {0.18f, 2.f, -0.25f, 0.37f};
    Begin();
    StartColorText("c", 0);
    EXPECT_FALSE(ZUIColorEdit4(&ctx, "c", color, ZUIColorSpace::Linear));
    End();
    Begin();
    snprintf(ctx.TextInput, sizeof(ctx.TextInput), "%s", "118");
    ctx.TextInputLen = 3;
    ctx.EnterPressed = true;
    EXPECT_FALSE(ZUIColorEdit4(&ctx, "c", color, ZUIColorSpace::Linear));
    EXPECT_FLOAT_EQ(color[0], 0.18f);
    EXPECT_FLOAT_EQ(color[1], 2.f);
    EXPECT_FLOAT_EQ(color[2], -0.25f);
    EXPECT_FLOAT_EQ(color[3], 0.37f);
    End();
}

TEST_F(ZUIBehaviorTest, ColorPickerHasExactStaticLayoutAndSessionModelNeverChangesStorage)
{
    ctx.ScreenW               = 800;
    ctx.ScreenH               = 900;
    float       color[4]      = {87.f / 255.f, 157.f / 255.f, 227.f / 255.f, 1.f};
    const char* expected[][4] = {
        { "87", "157", "227", "100"},
        {"210",  "62",  "89", "100"},
        {"210",  "71",  "62", "100"}
    };
    for (int model = 0; model < 3; ++model)
    {
        Begin();
        ctx.ColorModel = static_cast<ZUIColorModel>(model);
        EXPECT_FALSE(ZUIColorPicker4(&ctx, "p", color));
        auto* canvas = Find(ctx.Root, ColorKey("p", "picker"));
        End();
        ASSERT_NE(canvas, nullptr);
        EXPECT_FLOAT_EQ(canvas->ComputedSize[0], 320.f);
        EXPECT_FLOAT_EQ(canvas->ComputedSize[1], 567.f);
        for (int i = 0; i < 4; ++i)
        {
            auto* number = Find(ctx.Root, ColorKey("p", "channelValue", i));
            ASSERT_NE(number, nullptr);
            EXPECT_EQ(Label(number), expected[model][i]);
            EXPECT_TRUE(number->Flags & ZUI_Clickable);
            EXPECT_FLOAT_EQ(number->ScreenMin[1], 339.f + 28.f * i);
        }
        EXPECT_EQ(Label(Find(ctx.Root, ColorKey("p", "hex"))), "#579DE3");
        for (int i = 0; i < 24; ++i)
        {
            auto* recent = Find(ctx.Root, ColorKey("p", "recent", i));
            ASSERT_NE(recent, nullptr);
            EXPECT_TRUE(recent->Flags & ZUI_DashedBorder);
            EXPECT_FLOAT_EQ(recent->ScreenMin[0], 13.f + 24.f * (i % 12));
            EXPECT_FLOAT_EQ(recent->ScreenMin[1], 508.f + 26.f * (i / 12));
        }
        EXPECT_FLOAT_EQ(color[0], 87.f / 255.f);
        EXPECT_FLOAT_EQ(color[1], 157.f / 255.f);
        EXPECT_FLOAT_EQ(color[2], 227.f / 255.f);
    }
    Begin();
    ctx.FocusKey      = ColorKey("p", "model", 1);
    ctx.KeyboardFocus = true;
    ctx.EnterPressed  = true;
    EXPECT_FALSE(ZUIColorPicker4(&ctx, "p", color));
    EXPECT_EQ(ctx.ColorModel, ZUIColorModel::HSB);
    End();
    Begin();
    EXPECT_FALSE(ZUIColorPicker4(&ctx, "another", color));
    EXPECT_EQ(ctx.ColorModel, ZUIColorModel::HSB);
    End();
}

TEST_F(ZUIBehaviorTest, ColorPopupFlipsAboveAndClampsToTheWindow)
{
    ctx.ScreenW         = 800;
    ctx.ScreenH         = 900;
    float      color[4] = {};
    const auto key      = ColorKey("c", "swatch");
    auto*      bounds   = ZUIStateGetOrInsert(&ctx.StateStore, key);
    bounds->ScreenMinX  = 760;
    bounds->ScreenMinY  = 800;
    bounds->ScreenMaxY  = 822;
    Begin();
    ctx.FocusKey      = key;
    ctx.KeyboardFocus = true;
    ctx.EnterPressed  = true;
    EXPECT_FALSE(ZUIColorEdit4(&ctx, "c", color));
    EXPECT_FLOAT_EQ(ctx.PendingPopupPosX, 480.f);
    EXPECT_FLOAT_EQ(ctx.PendingPopupPosY, 233.f);
    End();
    Begin();
    EXPECT_FALSE(ZUIColorEdit4(&ctx, "c", color));
    End();
    ASSERT_EQ(ctx.PopupStackSize, 1u);
    auto* popup = ctx.PopupStack[0].Box;
    ASSERT_NE(popup, nullptr);
    EXPECT_FLOAT_EQ(popup->ScreenMin[0], 480.f);
    EXPECT_FLOAT_EQ(popup->ScreenMin[1], 233.f);
    EXPECT_FLOAT_EQ(popup->ComputedSize[0], 320.f);
    EXPECT_FLOAT_EQ(popup->ComputedSize[1], 567.f);
    EXPECT_TRUE(popup->Flags & ZUI_DropShadow);
}

TEST_F(ZUIBehaviorTest, PopupBoundsKeepTheLastCategoryReachableAtWindowEdges)
{
    constexpr float anchors[][2] = {
        { 10.f,  10.f},
        {390.f, 290.f},
        {-20.f, -10.f}
    };
    for (const auto& anchor : anchors)
    {
        ctx.PopupStackSize     = 1;
        ctx.PopupStack[0].Key  = ZUIHashStr("categories", 10);
        ctx.PopupStack[0].PosX = anchor[0];
        ctx.PopupStack[0].PosY = anchor[1];
        Begin();
        ASSERT_TRUE(ZUIBeginPopup(&ctx, "categories"));
        auto* popup = ctx.Current;
        for (const char* category : {"All", "Folders", "Scenes", "Models", "Materials", "Textures", "Scripts", "Shaders", "Other"})
            ZUIMenuItemEx(&ctx, category);
        const auto* last_item = popup->LastChild;
        ZUIEndPopup(&ctx);
        End();
        EXPECT_GE(popup->ScreenMin[0], 0.f);
        EXPECT_GE(popup->ScreenMin[1], 0.f);
        EXPECT_LE(popup->ScreenMax[0], ctx.ScreenW);
        EXPECT_LE(popup->ScreenMax[1], ctx.ScreenH);
        EXPECT_GE(last_item->ScreenMin[1], popup->ScreenMin[1]);
        EXPECT_LE(last_item->ScreenMax[1], ctx.ScreenH);
        EXPECT_FLOAT_EQ(last_item->ComputedSize[1], 26.f);
        EXPECT_FLOAT_EQ(ctx.PopupStack[0].ScreenMax[1], popup->ScreenMax[1]);
        if (anchor[0] == 10.f)
        {
            EXPECT_FLOAT_EQ(popup->ScreenMin[0], anchor[0]);
            EXPECT_FLOAT_EQ(popup->ScreenMin[1], anchor[1]);
        }
    }
}

TEST_F(ZUIBehaviorTest, PanelPopupsAndSubmenusRiseAboveLaterChromeButBelowForeground)
{
    ctx.PopupStackSize     = 2;
    ctx.PopupStack[0].Key  = ZUIHashStr("popup", 5);
    ctx.PopupStack[1].Key  = ZUIHashStr("child-popup", 11);
    ctx.PopupStack[0].PosX = ctx.PopupStack[0].PosY = 10.f;
    ctx.PopupStack[1].PosX                          = 200.f;
    ctx.PopupStack[1].PosY                          = 10.f;
    for (bool foreground : {false, true})
    {
        Begin();
        auto* root     = ctx.Current;
        auto* clipped  = ZUIBeginColumn(&ctx, "panel", ZPx(80.f), ZPx(40.f));
        clipped->Flags = ZUI_ClipChildren;
        ASSERT_TRUE(ZUIBeginPopup(&ctx, "popup"));
        auto* popup = ctx.Current;
        ZUIButton(&ctx, "Parent item");
        ASSERT_TRUE(ZUIBeginPopup(&ctx, "child-popup"));
        auto* child_popup = ctx.Current;
        ZUIButton(&ctx, "Child item");
        ZUIEndPopup(&ctx);
        EXPECT_EQ(ctx.Current, popup);
        ZUIEndPopup(&ctx);
        EXPECT_EQ(ctx.Current, clipped);
        ZUIEndColumn(&ctx);
        if (foreground)
        {
            auto* saved = ZUIBeginForeground(&ctx);
            ZUILabel(&ctx, "Drag decoration");
            ZUIEndForeground(&ctx, saved);
        }
        auto* chrome = Control("late-chrome", 0.f, 0.f, 400.f, 100.f, ZUI_DrawBackground);
        End();
        EXPECT_EQ(root->LastChild, foreground ? ctx.ForegroundRoot : child_popup);
        EXPECT_EQ(popup->Parent, root);
        EXPECT_EQ(child_popup->Parent, root);
        EXPECT_EQ(chrome->NextSib, popup);
        EXPECT_EQ(popup->NextSib, child_popup);
        EXPECT_EQ(child_popup->PrevSib, popup);
        EXPECT_EQ(child_popup->NextSib, foreground ? ctx.ForegroundRoot : nullptr);
        EXPECT_EQ(root->LastChild->NextSib, nullptr);
        EXPECT_EQ(root->FirstChild, clipped);
        EXPECT_EQ(clipped->PrevSib, nullptr);
    }
}

TEST_F(ZUIBehaviorTest, ColorIdsUseFullCallerKeyAndEditStateIsCallerLocal)
{
    const char* first    = "shared-very-long-caller-key-prefix-that-exceeds-the-old-widget-buffer-1";
    const char* second   = "shared-very-long-caller-key-prefix-that-exceeds-the-old-widget-buffer-2";
    float       color[4] = {0.2f, 0.3f, 0.4f, 1.f};
    Begin();
    ZUIColorEdit4(&ctx, first, color);
    ZUIColorEdit4(&ctx, second, color);
    EXPECT_NE(ColorState(first), ColorState(second));
    EXPECT_NE(ColorKey(first, "number"), ColorKey(second, "number"));
    End();
    EXPECT_FALSE(ZUIColorEdit3(&ctx, "null", nullptr));
}

TEST_F(ZUIBehaviorTest, FocusRingIsKeyboardOnlyAndEnterKeepsButtonFocused)
{
    ctx.MousePos[0] = ctx.MousePos[1] = 10.f;
    ctx.MousePressed[0] = ctx.MouseDown[0] = true;
    Begin();
    auto* mouse = Control("first", 0.f, 0.f);
    Control("second", 50.f, 0.f);
    End();
    EXPECT_EQ(ctx.FocusKey, mouse->Key);
    EXPECT_FALSE(mouse->Flags & ZUI_KeyboardFocused);
    ctx.MouseDown[0] = false;
    ctx.TabPressed   = true;
    Begin();
    Control("first", 0.f, 0.f);
    auto* keyboard = Control("second", 50.f, 0.f, 40.f, 22.f, ZUI_KeyboardActivate);
    End();
    EXPECT_EQ(ctx.FocusKey, keyboard->Key);
    EXPECT_TRUE(keyboard->Flags & ZUI_KeyboardFocused);
    ctx.EnterPressed = true;
    Begin();
    auto* activated = Control("second", 50.f, 0.f, 40.f, 22.f, ZUI_KeyboardActivate);
    EXPECT_TRUE(ZUISignalFromBox(&ctx, activated).Flags & ZUI_SignalClicked);
    End();
    EXPECT_EQ(ctx.FocusKey, activated->Key);
}

TEST_F(ZUIBehaviorTest, TabUsesVisualRowsAndShiftTabMovesBackAndWraps)
{
    const auto build = [&] {
        Begin();
        Control("right", 80.f, 8.f, 40.f, 26.f);
        Control("bottom", 0.f, 50.f);
        Control("left", 0.f, 10.f);
        End();
    };
    ctx.TabPressed = true;
    build();
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("left", 4));
    ctx.TabPressed = true;
    build();
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("right", 5));
    ctx.ShiftTabPressed = true;
    build();
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("left", 4));
    ctx.ShiftTabPressed = true;
    build();
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("bottom", 6));
}

TEST_F(ZUIBehaviorTest, TabGroupsPanelsInDockOrderAndStartsInActivePanel)
{
    const auto build = [&] {
        Begin();
        for (uint32_t order : {3u, 1u, 2u})
        {
            const char* key    = order == 1 ? "left" : order == 2 ? "center" : "right";
            auto*       scope  = ZUIBeginColumn(&ctx, key, ZPx(120.f), ZPx(100.f));
            scope->Flags       = ZUI_NavScope | ZUI_FloatX;
            scope->NavOrder    = order;
            scope->FloatPos[0] = (order - 1) * 120.f;
            Control(order == 1 ? "a" : order == 2 ? "b" : "c", 0.f, 0.f);
            ZUIEndColumn(&ctx);
        }
        End();
    };
    ctx.PreferredFocusScope = ZUIHashStr("center", 6);
    ctx.TabPressed          = true;
    build();
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("b", 1));
    ctx.TabPressed = true;
    build();
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("c", 1));
    ctx.TabPressed = true;
    build();
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("a", 1));
}

TEST_F(ZUIBehaviorTest, TabSkipsDisabledClippedAndPointerOnlyControls)
{
    Begin();
    Control("disabled", 0.f, 0.f, 40.f, 22.f, ZUI_Disabled);
    Control("pointer", 50.f, 0.f, 40.f, 22.f, ZUI_NoKeyboardFocus);
    auto* clip  = ZUIBeginColumn(&ctx, "clip", ZPx(40.f), ZPx(22.f));
    clip->Flags = ZUI_ClipChildren;
    Control("hidden", 0.f, 30.f);
    ZUIEndColumn(&ctx);
    auto* target   = Control("visible", 100.f, 0.f);
    ctx.TabPressed = true;
    End();
    EXPECT_EQ(ctx.FocusKey, target->Key);
}

TEST_F(ZUIBehaviorTest, ModalTrapsTabAndEscapeUntilItClosesThenRestoresFocus)
{
    ctx.FocusKey     = ZUIHashStr("outside", 7);
    const auto build = [&](bool modal) {
        Begin();
        Control("outside", 0.f, 0.f);
        if (modal)
        {
            ctx.ModalBox = ZUIBeginColumn(&ctx, "modal", ZPx(200.f), ZPx(100.f));
            Control("inside", 100.f, 40.f);
            ZUIEndColumn(&ctx);
        }
        End();
    };
    ctx.TabPressed = true;
    build(true);
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("inside", 6));
    ctx.ShiftTabPressed = true;
    build(true);
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("inside", 6));
    ctx.EscapePressed = true;
    build(true);
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("inside", 6));
    build(false);
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("outside", 7));
}

TEST_F(ZUIBehaviorTest, PopupTrapsTabSupportsArrowsAndEscapeRestoresOwner)
{
    ctx.FocusKey      = ZUIHashStr("owner", 5);
    ctx.KeyboardFocus = true;
    const auto build  = [&](bool open) {
        Begin();
        Control("owner", 0.f, 0.f);
        if (open)
            ZUIOpenPopup(&ctx, "popup", 100.f, 100.f);
        if (ZUIBeginPopup(&ctx, "popup"))
        {
            ZUIMenuItem(&ctx, "first");
            ZUIMenuItem(&ctx, "disabled", false);
            ZUIMenuItem(&ctx, "last");
            ZUIEndPopup(&ctx);
        }
        End();
    };
    build(true);
    build(false);
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("first", 5));
    ctx.ArrowDownPressed = true;
    build(false);
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("last", 4));
    ctx.TabPressed = true;
    build(false);
    EXPECT_EQ(ctx.PopupStackSize, 1u);
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("first", 5));
    ctx.EscapePressed = true;
    build(false);
    EXPECT_EQ(ctx.PopupStackSize, 0u);
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("owner", 5));
}

TEST_F(ZUIBehaviorTest, ConfirmingPopupRestoresBuilderParentEvenAfterClosingTheStack)
{
    Begin();
    auto* owner           = ZUIBeginColumn(&ctx, "owner", ZPx(200.f), ZPx(200.f));
    ctx.PopupStackSize    = 1;
    ctx.PopupStack[0].Key = ZUIHashStr("popup", 5);
    ASSERT_TRUE(ZUIBeginPopup(&ctx, "popup"));
    ctx.KeyboardFocus = true;
    ctx.FocusKey      = ZUIHashStr("Import", 6);
    ctx.EnterPressed  = true;
    EXPECT_TRUE(ZUIMenuItem(&ctx, "Import"));
    EXPECT_EQ(ctx.PopupStackSize, 0u);
    ZUIEndPopup(&ctx);
    EXPECT_EQ(ctx.Current, owner);
    EXPECT_EQ(ctx.PopupBuildDepth, 0u);
    auto* after = Control("after", 0.f, 0.f);
    EXPECT_EQ(after->Parent, owner);
    ZUIEndColumn(&ctx);
    End();
}

TEST_F(ZUIBehaviorTest, ClosingComboInsideModalRestoresTheComboNotTheFirstControl)
{
    ctx.KeyboardFocus = true;
    ctx.FocusKey      = ZUIHashStr("outside", 7);
    const auto build  = [&](bool open, bool close) {
        Begin();
        Control("outside", 0.f, 0.f);
        ctx.ModalBox = ZUIBeginColumn(&ctx, "modal", ZPx(200.f), ZPx(200.f));
        Control("first", 0.f, 0.f);
        Control("combo", 0.f, 30.f);
        if (open)
            ZUIOpenPopup(&ctx, "combo-popup", 50.f, 50.f);
        if (ZUIBeginPopup(&ctx, "combo-popup"))
        {
            ZUIMenuItem(&ctx, "choice");
            if (close)
                ZUIClosePopup(&ctx);
            ZUIEndPopup(&ctx);
        }
        ZUIEndColumn(&ctx);
        End();
    };
    build(false, false);
    ctx.FocusKey = ZUIHashStr("combo", 5);
    build(true, false);
    build(false, false);
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("choice", 6));
    build(false, true);
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("combo", 5));
    EXPECT_EQ(ctx.SavedFocusKey, ZUIHashStr("outside", 7));
}

TEST_F(ZUIBehaviorTest, ClosingNestedPopupUnwindsEverySavedParent)
{
    Begin();
    auto* owner           = ZUIBeginColumn(&ctx, "owner", ZPx(200.f), ZPx(200.f));
    ctx.PopupStackSize    = 2;
    ctx.PopupStack[0].Key = ZUIHashStr("popup", 5);
    ctx.PopupStack[1].Key = ZUIHashStr("nested", 6);
    ASSERT_TRUE(ZUIBeginPopup(&ctx, "popup"));
    auto* popup = ctx.Current;
    ASSERT_TRUE(ZUIBeginPopup(&ctx, "nested"));
    ZUIClosePopup(&ctx);
    ZUIEndPopup(&ctx);
    EXPECT_EQ(ctx.Current, popup);
    EXPECT_EQ(ctx.PopupBuildDepth, 1u);
    ZUIEndPopup(&ctx);
    EXPECT_EQ(ctx.Current, owner);
    EXPECT_EQ(ctx.PopupBuildDepth, 0u);
    ZUIEndColumn(&ctx);
    End();
}

TEST_F(ZUIBehaviorTest, PopupWhoseOwnerDisappearsDoesNotCaptureInput)
{
    ctx.PopupStackSize    = 1;
    ctx.PopupStack[0].Key = ZUIHashStr("missing", 7);
    ctx.MousePos[0] = ctx.MousePos[1] = 10.f;
    Begin();
    auto* control = Control("visible", 0.f, 0.f);
    End();
    EXPECT_EQ(ctx.PopupStackSize, 0u);
    EXPECT_EQ(ctx.HotKey, control->Key);
}

TEST_F(ZUIBehaviorTest, PopupDepthLimitDoesNotQueueAnOutOfBoundsEntry)
{
    Begin();
    ctx.PopupBuildDepth = ZUIContext::kMaxPopupDepth;
    ZUIOpenPopup(&ctx, "too deep");
    EXPECT_EQ(ctx.PendingPopupKey, 0u);
    ctx.PopupBuildDepth = 0;
    End();
}

class ZUISubMenuTest : public ZUIBehaviorTest
{
protected:
    void Build(bool open_parent = false, bool nested = false)
    {
        Begin();
        if (open_parent)
            ZUIOpenPopup(&ctx, "menu", 10.f, 10.f);
        if (ZUIBeginPopup(&ctx, "menu"))
        {
            ZUIMenuItem(&ctx, "Before");
            if (ZUIBeginSubMenu(&ctx, "Recent"))
            {
                if (nested && ZUIBeginSubMenu(&ctx, "Nested"))
                {
                    ZUIMenuItem(&ctx, "Leaf");
                    ZUIEndSubMenu(&ctx);
                }
                ZUIMenuItem(&ctx, "Child");
                ZUIEndSubMenu(&ctx);
            }
            ZUIMenuItem(&ctx, "After");
            if (ZUIBeginSubMenu(&ctx, "Other"))
            {
                ZUIMenuItem(&ctx, "Other child");
                ZUIEndSubMenu(&ctx);
            }
            ZUIEndPopup(&ctx);
        }
        End();
    }
    void PointAt(const char* key)
    {
        const auto* state = ZUIStateGetOrInsert(&ctx.StateStore, ZUIHashStr(key, (uint32_t) secure_strlen(key)));
        ASSERT_NE(state, nullptr);
        ASSERT_GT(state->ScreenMaxX, state->ScreenMinX);
        ctx.MousePos[0] = (state->ScreenMinX + state->ScreenMaxX) * 0.5f;
        ctx.MousePos[1] = (state->ScreenMinY + state->ScreenMaxY) * 0.5f;
    }
    void OpenHovered(bool nested = false)
    {
        Build(true, nested);
        Build(false, nested);
        ASSERT_EQ(ctx.PopupStackSize, 1u);
        PointAt("##smrow_Recent");
        Build(false, nested); // Hit-test the row.
        Build(false, nested); // Queue its hover-open popup.
        Build(false, nested); // Lay it out without closing its fresh entry.
        ASSERT_EQ(ctx.PopupStackSize, 2u);
    }
};

TEST_F(ZUISubMenuTest, HoveredSubmenuClosesOnLeavingItsOwnerWithoutClosingParent)
{
    OpenHovered();
    ASSERT_EQ(ctx.PopupStackSize, 2u);
    EXPECT_GT(ctx.PopupStack[1].ScreenMax[0], ctx.PopupStack[1].ScreenMin[0]);
    // Return to another item in the parent, then stop moving entirely.
    PointAt("After");
    Build();
    EXPECT_EQ(ctx.PopupStackSize, 1u);
    Build();
    EXPECT_EQ(ctx.PopupStackSize, 1u);
    EXPECT_EQ(ctx.PopupBuildDepth, 0u);
}

TEST_F(ZUISubMenuTest, StationaryCursorOutsideDoesNotCountAsTravelTowardSubmenu)
{
    OpenHovered();
    ASSERT_EQ(ctx.PopupStackSize, 2u);
    PointAt("After");
    ctx.PrevMousePos[0] = ctx.MousePos[0];
    ctx.PrevMousePos[1] = ctx.MousePos[1];
    Build();
    EXPECT_EQ(ctx.PopupStackSize, 1u);
}

TEST_F(ZUISubMenuTest, CursorMayCrossDiagonallyToChildButCannotLingerOutside)
{
    OpenHovered();
    ASSERT_EQ(ctx.PopupStackSize, 2u);
    const auto submenu = ctx.PopupStack[1];
    // Move through the small gap toward the popup, just below its owner row.
    ctx.MousePos[0]    = submenu.ScreenMin[0] - 1.f;
    ctx.MousePos[1]    = submenu.ScreenMin[1] + 28.f;
    Build();
    EXPECT_EQ(ctx.PopupStackSize, 2u);
    Build(); // No further progress toward the popup: do not retain it forever.
    EXPECT_EQ(ctx.PopupStackSize, 1u);
}

TEST_F(ZUISubMenuTest, CursorInsideChildKeepsItOpenAndLeavingAllMenusClosesOnlyChild)
{
    OpenHovered();
    ASSERT_EQ(ctx.PopupStackSize, 2u);
    PointAt("Child");
    Build();
    Build();
    EXPECT_EQ(ctx.PopupStackSize, 2u);
    ctx.MousePos[0] = -100.f;
    ctx.MousePos[1] = -100.f;
    Build();
    EXPECT_EQ(ctx.PopupStackSize, 1u);
}

TEST_F(ZUISubMenuTest, CursorInsideNestedDescendantDoesNotCloseItsAncestors)
{
    OpenHovered(true);
    ASSERT_EQ(ctx.PopupStackSize, 2u);
    PointAt("##smrow_Nested");
    Build(false, true);
    Build(false, true);
    Build(false, true);
    ASSERT_EQ(ctx.PopupStackSize, 3u);
    PointAt("Leaf");
    Build(false, true);
    Build(false, true);
    EXPECT_EQ(ctx.PopupStackSize, 3u);
    PointAt("Child");
    Build(false, true);
    EXPECT_EQ(ctx.PopupStackSize, 2u);
}

TEST_F(ZUISubMenuTest, HoveringSiblingReplacesThePreviousSubmenu)
{
    OpenHovered();
    PointAt("##smrow_Other");
    Build();
    Build();
    Build();
    ASSERT_EQ(ctx.PopupStackSize, 2u);
    EXPECT_EQ(ctx.PopupStack[1].Key, ZUIHashStr("##smpop_Other", 13));
}

TEST_F(ZUISubMenuTest, KeyboardSubmenuStaysOpenWithMouseElsewhereAndLeftRestoresOwner)
{
    Build(true);
    Build();
    ctx.FocusKey          = ZUIHashStr("##smrow_Recent", 14);
    ctx.KeyboardFocus     = true;
    ctx.ArrowRightPressed = true;
    Build();
    Build();
    Build();
    ASSERT_EQ(ctx.PopupStackSize, 2u);
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("Child", 5));
    ctx.ArrowLeftPressed = true;
    Build();
    EXPECT_EQ(ctx.PopupStackSize, 1u);
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("##smrow_Recent", 14));
}

TEST_F(ZUISubMenuTest, MouseTakesOverDismissalAfterKeyboardNavigation)
{
    Build(true);
    Build();
    ctx.FocusKey          = ZUIHashStr("##smrow_Recent", 14);
    ctx.KeyboardFocus     = true;
    ctx.ArrowRightPressed = true;
    Build();
    Build();
    Build();
    ASSERT_EQ(ctx.PopupStackSize, 2u);
    PointAt("Child");
    Build();
    EXPECT_EQ(ctx.PopupStackSize, 2u);
    PointAt("After");
    Build();
    EXPECT_EQ(ctx.PopupStackSize, 1u);
    EXPECT_TRUE(ctx.KeyboardFocus); // Pointer takeover need not alter focus-ring modality.
}

TEST_F(ZUIBehaviorTest, MinimumHitHeightDoesNotEnlargeDrawGeometryOrStealNeighbor)
{
    ctx.MousePos[0] = 10.f;
    ctx.MousePos[1] = 2.f;
    Begin();
    auto* small = Control("small", 0.f, 4.f, 20.f, 16.f);
    End();
    EXPECT_FLOAT_EQ(small->ComputedSize[1], 16.f);
    EXPECT_EQ(ctx.HotKey, small->Key);
    Begin();
    small          = Control("small", 0.f, 4.f, 20.f, 16.f);
    auto* neighbor = Control("neighbor", 0.f, 0.f, 20.f, 4.f);
    End();
    EXPECT_EQ(ctx.HotKey, neighbor->Key);
}

TEST_F(ZUIBehaviorTest, TooltipWaits400MillisecondsAndUsesStableControlIdentity)
{
    ctx.MousePos[0] = ctx.MousePos[1] = 10.f;
    const auto build                  = [&] {
        Begin(0.1f);
        auto*      box      = Control("source", 0.f, 0.f);
        const auto sig      = ZUISignalFromBox(&ctx, box);
        char       text[64] = "Full label"; // A fresh buffer is not the tooltip identity.
        ZUISetTooltip(&ctx, sig, text);
        auto* tip = ctx.ForegroundRoot ? ctx.ForegroundRoot->FirstChild : nullptr;
        End();
        return tip;
    };
    for (int frame = 0; frame < 5; ++frame)
        EXPECT_EQ(build(), nullptr);
    const auto* tip = build();
    ASSERT_NE(tip, nullptr);
    EXPECT_FLOAT_EQ(tip->Padding[0], 8.f);
    EXPECT_FLOAT_EQ(tip->Padding[1], 6.f);
    EXPECT_FLOAT_EQ(tip->CornerRadii[0], 4.f);
    EXPECT_FALSE(tip->Flags & ZUI_DropShadow);
    ctx.MousePos[0] = 200.f;
    EXPECT_EQ(build(), nullptr); // Hide immediately even though HotKey is one frame old.
    EXPECT_EQ(build(), nullptr);
}

TEST_F(ZUIBehaviorTest, KeyboardTooltipIsImmediateAndFlipsUsingMeasuredSize)
{
    ctx.FocusKey      = ZUIHashStr("source", 6);
    ctx.KeyboardFocus = true;
    auto* state       = ZUIStateGetOrInsert(&ctx.StateStore, ctx.FocusKey);
    state->ScreenMinX = 390.f;
    state->ScreenMaxY = 290.f;
    Begin();
    auto* box = Control("source", 360.f, 270.f);
    ZUISetTooltip(&ctx, ZUISignalFromBox(&ctx, box), "Full tooltip label");
    auto* tip = ctx.ForegroundRoot->FirstChild;
    End();
    EXPECT_LT(tip->ScreenMax[0], 390.f);
    EXPECT_LT(tip->ScreenMax[1], 290.f);
    EXPECT_GE(tip->ScreenMin[0], 0.f);
    EXPECT_LE(tip->ScreenMax[0], (float) ctx.ScreenW);
    EXPECT_LE(tip->ScreenMax[1], (float) ctx.ScreenH);
}

TEST_F(ZUIBehaviorTest, ReducedMotionSnapsHoverPressScrollAndToggle)
{
    ctx.Style.ReducedMotion = true;
    ctx.HotKey = ctx.ActiveKey = ZUIHashStr("button", 6);
    Begin();
    auto* box = Control("button", 0.f, 0.f);
    ZUISignalFromBox(&ctx, box);
    auto* state = ZUIStateGetOrInsert(&ctx.StateStore, box->Key);
    EXPECT_FLOAT_EQ(state->HotT, 1.f);
    EXPECT_FLOAT_EQ(state->ActiveT, 1.f);
    auto* scroll          = ZUIStateGetOrInsert(&ctx.StateStore, ZUIHashStr("scroll", 6));
    scroll->MaxScrollY    = 200.f;
    scroll->ScrollYTarget = 100.f;
    ZUIBeginScrollRegion(&ctx, "scroll", ZPx(100.f), ZPx(40.f));
    EXPECT_FLOAT_EQ(scroll->ScrollY, 100.f);
    ZUIEndScrollRegion(&ctx);
    bool on = true;
    ZUIToggle(&ctx, "toggle", &on);
    EXPECT_FLOAT_EQ(ZUIStateGetOrInsert(&ctx.StateStore, ZUIHashStr("toggle", 6))->ToggleT, 1.f);
    End();
    on = false;
    Begin();
    ZUIToggle(&ctx, "toggle", &on);
    EXPECT_FLOAT_EQ(ZUIStateGetOrInsert(&ctx.StateStore, ZUIHashStr("toggle", 6))->ToggleT, 0.f);
    End();
}

TEST_F(ZUIBehaviorTest, ButtonsAndPopupItemsAccommodateThirtyPercentWiderText)
{
    const auto build = [&] {
        Begin();
        ZUIButton(&ctx, "Long action label", ZPx(80.f));
        auto* button          = ctx.Root->LastChild;
        ctx.PopupStackSize    = 1;
        ctx.PopupStack[0].Key = ZUIHashStr("popup", 5);
        ZUIBeginPopup(&ctx, "popup");
        ZUIMenuItemEx(&ctx, "A longer menu action label", "Ctrl+Shift+A");
        auto* popup = ctx.PopupStack[0].Box;
        ZUIEndPopup(&ctx);
        End();
        return std::pair{button->ComputedSize[0], popup->ComputedSize[0]};
    };
    const auto original = build();
    for (auto& glyph : glyphs)
        glyph.AdvanceX *= 1.3f;
    const auto expanded = build();
    EXPECT_GT(expanded.first, original.first);
    EXPECT_GT(expanded.second, original.second);
    EXPECT_GE(expanded.first, 17.f * 7.f * 1.3f + 16.f);
}

TEST_F(ZUIBehaviorTest, SearchFieldHasOneTabStopAndFocusRingOnItsFullFrame)
{
    char       text[32] = {};
    const auto build    = [&] {
        Begin();
        ZUISearchBox(&ctx, "search", text, sizeof(text), "Search", ZPx(200.f));
        auto* frame = ctx.Root->LastChild;
        Control("after", 220.f, 0.f);
        End();
        return frame;
    };
    ctx.TabPressed = true;
    auto* frame    = build();
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("search", 6));
    EXPECT_TRUE(frame->Flags & ZUI_KeyboardFocused);
    EXPECT_FALSE(frame->LastChild->Flags & ZUI_KeyboardFocused);
    ctx.TabPressed = true;
    build();
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("after", 5));
}

TEST_F(ZUIBehaviorTest, ListArrowsStayInsideTheList)
{
    const auto build = [&] {
        Begin();
        auto* list  = ZUIBeginColumn(&ctx, "list", ZPx(100.f), ZPx(100.f));
        list->Flags = ZUI_NavList;
        Control("first", 0.f, 0.f);
        Control("last", 0.f, 30.f);
        ZUIEndColumn(&ctx);
        Control("outside", 200.f, 0.f);
        End();
    };
    ctx.FocusKey         = ZUIHashStr("first", 5);
    ctx.ArrowDownPressed = true;
    build();
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("last", 4));
    ctx.ArrowDownPressed = true;
    build();
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("first", 5));
}

TEST_F(ZUIBehaviorTest, KeyboardFocusDoesNotChangeMenuItemFill)
{
    ctx.KeyboardFocus = true;
    ctx.FocusKey      = ZUIHashStr("item", 4);
    Begin();
    ZUIMenuItem(&ctx, "item");
    auto* item = ctx.Root->LastChild;
    End();
    EXPECT_TRUE(item->Flags & ZUI_KeyboardFocused);
    EXPECT_FLOAT_EQ(item->Colors[0][3], 0.f);
}

TEST_F(ZUIBehaviorTest, EnterCommitsTextEditingWithoutBreakingButtonFocus)
{
    char text[32]    = "Rename";
    ctx.FocusKey     = ZUIHashStr("rename", 6);
    ctx.EnterPressed = true;
    Begin();
    ZUITextField(&ctx, "rename", text, sizeof(text), 150.f);
    End();
    EXPECT_EQ(ctx.FocusKey, 0u);
}

class ZUIWidgetsTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager memory = {};
    ZUIContext                           ctx    = {};
    static constexpr const char*         Key    = "##widget_input";

    void                                 SetUp() override
    {
        memory.Initialize(ZMega(8), {});
        ZUIContextInit(&ctx, &memory.MainArena, ZMega(4), ZMega(1), 1024, 1024);
        ctx.FocusKey = ZUIHashStr(Key, (uint32_t) secure_strlen(Key));
    }
    void TearDown() override
    {
        ZUIContextDestroy(&ctx);
    }
    ZUIPersistentState* State()
    {
        return ZUIStateGetOrInsert(&ctx.StateStore, ctx.FocusKey);
    }
    void ExpectColor(const float actual[4], Tokens::Color expected)
    {
        EXPECT_FLOAT_EQ(actual[0], expected.R);
        EXPECT_FLOAT_EQ(actual[1], expected.G);
        EXPECT_FLOAT_EQ(actual[2], expected.B);
        EXPECT_FLOAT_EQ(actual[3], expected.A);
    }
    void Begin(const char* input = "")
    {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        secure_strcpy(ctx.TextInput, sizeof(ctx.TextInput), input);
        ctx.TextInputLen = (uint32_t) secure_strlen(input);
    }
};

TEST_F(ZUIWidgetsTest, ButtonDefaultsUseControlHeightAndButtonRadiiNotFieldRounding)
{
    Begin();
    auto* root = ZUIBeginColumn(&ctx, "root", ZPx(300), ZPx(200));
    ZUIButton(&ctx, "Button");
    auto* button = root->LastChild;
    ZUISmallButton(&ctx, "Small");
    auto* small  = root->LastChild;
    bool  active = false;
    ZUIToggleButton(&ctx, "Toggle", &active);
    auto* toggle = root->LastChild;
    ZUIEndColumn(&ctx);
    ZUIEndFrame(&ctx);
    EXPECT_FLOAT_EQ(button->ComputedSize[1], 26.f);
    for (const auto* box : {button, small, toggle})
    {
        EXPECT_FLOAT_EQ(box->ComputedSize[1], Metrics::HeightControl);
        for (float radius : box->CornerRadii)
            EXPECT_FLOAT_EQ(radius, 4.f);
    }
    EXPECT_FLOAT_EQ(ctx.Style.FrameRounding, 3.f);
}

TEST_F(ZUIWidgetsTest, HeadingHasItsOwnAtlasSlotWithoutChangingLargeTitles)
{
    ZUIFont      body    = {};
    ZUIFont      header  = {};
    ZUIFont      heading = {};
    ZUIFontAtlas atlas   = {};
    atlas.Body           = &body;
    atlas.Header         = &header;
    atlas.Heading        = &heading;
    ctx.Atlas            = &atlas;
    EXPECT_EQ(ctx.GetFont(ZUIFontSize::Body), &body);
    EXPECT_EQ(ctx.GetFont(ZUIFontSize::Header), &header);
    EXPECT_EQ(ctx.GetFont(ZUIFontSize::Heading), &heading);
    EXPECT_FLOAT_EQ(heading.FontScale, 1.f);
    atlas.Heading = nullptr;
    EXPECT_EQ(ctx.GetFont(ZUIFontSize::Heading), &body);
    ctx.Atlas = nullptr;
}

TEST_F(ZUIWidgetsTest, DockGhostHugsActiveTabContentWithOneOutlineAndMenuShadow)
{
    ZUIGlyph glyphs[96] = {};
    for (auto& glyph : glyphs)
        glyph.AdvanceX = 7.f;
    ZUIFont font       = {};
    font.Glyphs        = glyphs;
    font.GlyphCount    = 96;
    font.LineHeight    = 13.f;
    ZUIFontAtlas atlas = {};
    atlas.Body         = &font;
    ctx.Atlas          = &atlas;
    Begin();
    auto* root = ZUIBeginColumn(&ctx, "root", ZPx(300), ZPx(300));
    ZUIDockGhostHeader(&ctx, "ghost", "Tab", 100.f, 100.f, (int) ZUIIcon::Folder);
    EXPECT_EQ(ctx.Current, root);
    auto* ghost   = ctx.ForegroundRoot->LastChild;
    auto* header  = ghost->FirstChild;
    auto* outline = ghost->LastChild;
    EXPECT_TRUE(ghost->Flags & ZUI_DropShadow);
    EXPECT_EQ(ghost->ShadowKind, ZUIShadowKind::Menu);
    EXPECT_FALSE(header->Flags & ZUI_DrawBorder);
    EXPECT_FLOAT_EQ(header->Size[1].Value, 30.f);
    EXPECT_FLOAT_EQ(header->Padding[0], 8.f);
    EXPECT_FLOAT_EQ(header->Padding[2], 10.f);
    EXPECT_FLOAT_EQ(header->Gap, 8.f);
    EXPECT_EQ(header->FirstChild->NextSib->IconId, (uint16_t) ZUIIcon::Folder);
    EXPECT_EQ(header->LastChild->IconId, (uint16_t) ZUIIcon::X);
    ExpectColor(outline->BorderColor, Tokens::AccentBlue);
    EXPECT_FLOAT_EQ(outline->BorderThickness, 1.f);
    ZUIEndColumn(&ctx);
    ZUIEndFrame(&ctx);
    EXPECT_FLOAT_EQ(ghost->ComputedSize[0], 78.f);
    EXPECT_FLOAT_EQ(ghost->ComputedSize[1], 69.f);
    EXPECT_FLOAT_EQ(ghost->ScreenMin[0], 100.f - 78.f * 0.3f);
    EXPECT_FLOAT_EQ(ghost->ScreenMin[1], 85.f);
    EXPECT_FLOAT_EQ(outline->ComputedSize[0], 78.f);
    EXPECT_FLOAT_EQ(outline->ComputedSize[1], 69.f);
    EXPECT_FLOAT_EQ(outline->ScreenMin[0], ghost->ScreenMin[0]);
    EXPECT_FLOAT_EQ(outline->ScreenMin[1], ghost->ScreenMin[1]);
    for (float radius : ghost->CornerRadii)
        EXPECT_FLOAT_EQ(radius, 0.f);
    ctx.Atlas = nullptr;
}

TEST_F(ZUIWidgetsTest, ButtonKindsUseExactFigmaStateTokens)
{
    for (const auto kind : {ZUIButtonKind::Primary, ZUIButtonKind::Secondary, ZUIButtonKind::Ghost})
    {
        for (int state = 0; state < 4; ++state)
        {
            Begin();
            auto*      root = ZUIBeginColumn(&ctx, "root", ZPx(300), ZPx(200));
            const auto key  = ZUIHashStr("button", 6);
            ctx.FocusKey    = 0;
            ctx.HotKey      = state == 1 ? key : 0;
            ctx.ActiveKey   = state == 2 ? key : 0;
            auto* ps        = ZUIStateGetOrInsert(&ctx.StateStore, key);
            ps->HotT        = state == 1 ? 1.f : 0.f;
            ps->ActiveT     = state == 2 ? 1.f : 0.f;
            if (state == 3)
                ZUIBeginDisabled(&ctx);
            ZUIButton(&ctx, "button", ZText(), ZPx(Metrics::HeightControl), kind);
            auto* button = root->LastChild;
            if (state == 3)
                ZUIEndDisabled(&ctx);
            const bool          primary = kind == ZUIButtonKind::Primary;
            const bool          ghost   = kind == ZUIButtonKind::Ghost;
            const Tokens::Color fill    = state == 3 ? (ghost ? Tokens::Color{} : (primary ? Tokens::BgControl : Tokens::BgPanel)) : state == 2 ? (primary ? Tokens::AccentSelectionPressed : Tokens::BgSurface) : state == 1 ? (primary ? Tokens::AccentSelectionHover : Tokens::BgHover) : primary ? Tokens::AccentSelection : (ghost ? Tokens::Color{} : Tokens::BgControl);
            ExpectColor(button->Colors[0], fill);
            ExpectColor(button->TextColor, state == 3 ? Tokens::TextDisabled : Tokens::TextPrimary);
            EXPECT_EQ(bool(button->Flags & ZUI_DrawBorder), !primary && !ghost);
            if (!primary && !ghost)
                ExpectColor(button->BorderColor, state == 3 ? Tokens::BorderSubtle : Tokens::BorderField);
            ZUIEndColumn(&ctx);
        }
    }
}

TEST_F(ZUIWidgetsTest, ToggleButtonUsesSelectionAndUpdatesItsStyleOnTheClickFrame)
{
    Begin();
    auto* root = ZUIBeginColumn(&ctx, "root", ZPx(300), ZPx(200));
    ctx.HotKey = ctx.ActiveKey = ZUIHashStr("toggle", 6);
    ctx.MouseReleased[0]       = true;
    bool active                = false;
    EXPECT_TRUE(ZUIToggleButton(&ctx, "toggle", &active));
    EXPECT_TRUE(active);
    ExpectColor(root->LastChild->Colors[0], Tokens::AccentSelection);
    ExpectColor(root->LastChild->BorderColor, Tokens::AccentBlue);
    ExpectColor(root->LastChild->TextColor, Tokens::TextPrimary);
    ZUIEndColumn(&ctx);
}

TEST_F(ZUIWidgetsTest, ToolbarUsesBodyLabelPrimaryTextBlueActiveIconAndInsideBorder)
{
    Begin();
    auto* root = ZUIBeginColumn(&ctx, "root", ZPx(300), ZPx(200));
    ZUIEditorControl(&ctx, "toolbar", ZUIIcon::Plus, "Label", true, true);
    auto* button  = root->LastChild;
    auto* icon    = button->FirstChild;
    auto* label   = icon->NextSib;
    auto* chevron = button->LastChild;
    EXPECT_FLOAT_EQ(button->Size[1].Value, 26.f);
    EXPECT_FLOAT_EQ(button->Padding[0], 8.f);
    EXPECT_FLOAT_EQ(button->Padding[2], 8.f);
    EXPECT_FLOAT_EQ(button->Gap, 6.f);
    EXPECT_FLOAT_EQ(button->BorderThickness, 1.f);
    EXPECT_FLOAT_EQ(icon->Size[0].Value, 16.f);
    EXPECT_FLOAT_EQ(chevron->Size[0].Value, 10.f);
    EXPECT_EQ(label->FontSize, ZUIFontSize::Body);
    ExpectColor(label->TextColor, Tokens::TextPrimary);
    ExpectColor(icon->TextColor, Tokens::AccentBlue);
    ExpectColor(chevron->TextColor, Tokens::TextSecondary);
    ExpectColor(button->Colors[0], Tokens::AccentSelection);
    ExpectColor(button->BorderColor, Tokens::AccentBlue);
    ZUIEditorControl(&ctx, "icon-only", ZUIIcon::Plus, nullptr, false);
    auto* icon_only = root->LastChild;
    ZUIEndColumn(&ctx);
    ZUIEndFrame(&ctx);
    EXPECT_FLOAT_EQ(icon_only->ComputedSize[0], 32.f);
    EXPECT_FLOAT_EQ(icon_only->ComputedSize[1], 26.f);
}

TEST_F(ZUIWidgetsTest, DisabledToolbarOverridesActiveIconTintAndRejectsStaleMouseAndKeyboardInput)
{
    for (bool primary : {false, true})
    {
        Begin();
        auto* root   = ZUIBeginColumn(&ctx, "root", ZPx(300), ZPx(200));
        ctx.FocusKey = ctx.HotKey = ctx.ActiveKey = ZUIHashStr("disabled", 8);
        ctx.EnterPressed = ctx.SpacePressed = ctx.MouseReleased[0] = true;
        const float tint[4]                                        = ZUI_TOKEN(Tokens::AccentGreen);
        const auto  sig                                            = ZUIEditorControl(&ctx, "disabled", ZUIIcon::Plus, "Disabled", true, true, tint, true, primary);
        auto*       button                                         = root->LastChild;
        EXPECT_EQ(sig.Flags, 0u);
        EXPECT_FALSE(button->Flags & ZUI_Clickable);
        EXPECT_EQ(ctx.FocusKey, 0u);
        EXPECT_EQ(ctx.ActiveKey, 0u);
        ExpectColor(button->Colors[0], primary ? Tokens::BgControl : Tokens::BgPanel);
        for (auto* child = button->FirstChild; child; child = child->NextSib)
            ExpectColor(child->TextColor, Tokens::TextDisabled);
        ZUIEndColumn(&ctx);
    }
}

TEST_F(ZUIWidgetsTest, DisabledFieldsRejectTypingDraggingAndKeyboardWithDedicatedColors)
{
    for (int widget = 0; widget < 6; ++widget)
    {
        Begin("9");
        auto* root   = ZUIBeginColumn(&ctx, "root", ZPx(300), ZPx(200));
        ctx.FocusKey = ctx.HotKey = ctx.ActiveKey = ZUIHashStr(Key, (uint32_t) secure_strlen(Key));
        State()->UserData                         = 1.f;
        ctx.MouseDown[0] = ctx.MouseReleased[0] = ctx.EnterPressed = ctx.BackspacePressed = ctx.ArrowUpPressed = true;
        ctx.MousePos[0]                                                                                        = 50.f;
        ctx.PrevMousePos[0]                                                                                    = 0.f;
        float value                                                                                            = 1.f;
        int   integer                                                                                          = 1;
        char  buffer[64]                                                                                       = "value";
        ZUIBeginDisabled(&ctx);
        bool changed = false;
        switch (widget)
        {
            case 0:
                changed = ZUITextField(&ctx, Key, buffer, sizeof(buffer), 100.f);
                break;
            case 1:
                changed = ZUIDragFloat(&ctx, Key, &value);
                break;
            case 2:
                changed = ZUIDragInt(&ctx, Key, &integer);
                break;
            case 3:
                changed = ZUIInputInt(&ctx, Key, &integer);
                break;
            case 4:
                changed = ZUIInputFloat(&ctx, Key, &value);
                break;
            case 5:
                changed = ZUIInputTextMultiline(&ctx, Key, buffer, sizeof(buffer));
                break;
        }
        ZUIEndDisabled(&ctx);
        EXPECT_FALSE(changed);
        EXPECT_EQ(integer, 1);
        EXPECT_FLOAT_EQ(value, 1.f);
        EXPECT_STREQ(buffer, "value");
        EXPECT_FALSE(ctx.TextInputActive);
        const auto* field = root->LastChild;
        ExpectColor(field->Colors[0], Tokens::BgPanel);
        ExpectColor(field->BorderColor, Tokens::BorderSubtle);
        ExpectColor(field->TextColor, Tokens::TextDisabled);
        EXPECT_FALSE(field->Flags & ZUI_Clickable);
        ZUIEndColumn(&ctx);
    }
}

TEST_F(ZUIWidgetsTest, DisabledSearchAndComboUseFieldRecipeWithoutOpeningOrEditing)
{
    Begin("x");
    auto* root       = ZUIBeginColumn(&ctx, "root", ZPx(300), ZPx(200));
    char  buffer[64] = "value";
    ctx.FocusKey     = ZUIHashStr(Key, (uint32_t) secure_strlen(Key));
    ZUIBeginDisabled(&ctx);
    EXPECT_FALSE(ZUISearchBox(&ctx, Key, buffer, sizeof(buffer), "Search"));
    auto* search = root->LastChild;
    EXPECT_FALSE(ZUIBeginCombo(&ctx, "combo", "Value"));
    auto* combo = root->LastChild;
    ZUIEndDisabled(&ctx);
    EXPECT_STREQ(buffer, "value");
    EXPECT_FALSE(ctx.TextInputActive);
    EXPECT_EQ(ctx.PendingPopupKey, 0u);
    for (const auto* field : {search, combo})
    {
        ExpectColor(field->Colors[0], Tokens::BgPanel);
        ExpectColor(field->BorderColor, Tokens::BorderSubtle);
        EXPECT_FLOAT_EQ(field->CornerRadii[0], 3.f);
        EXPECT_FALSE(field->Flags & ZUI_Clickable);
        ExpectColor(field->LastChild->TextColor, Tokens::TextDisabled);
    }
    ExpectColor(search->FirstChild->TextColor, Tokens::TextSecondary);
    ZUIEndColumn(&ctx);
}

TEST_F(ZUIWidgetsTest, DisabledCheckboxAndToggleHaveDedicatedTokensAndCannotChange)
{
    for (bool on : {false, true})
    {
        Begin();
        auto* root    = ZUIBeginColumn(&ctx, "root", ZPx(300), ZPx(200));
        bool  checked = on;
        bool  toggled = on;
        ctx.FocusKey = ctx.HotKey = ctx.ActiveKey = ZUIHashStr("toggle", 6);
        ctx.MouseReleased[0] = ctx.SpacePressed = true;
        ZUIBeginDisabled(&ctx);
        EXPECT_FALSE(ZUICheckbox(&ctx, "Check", &checked));
        auto* checkbox = root->LastChild;
        EXPECT_FALSE(ZUIToggle(&ctx, "toggle", &toggled));
        auto* toggle = root->LastChild;
        ZUIEndDisabled(&ctx);
        EXPECT_EQ(checked, on);
        EXPECT_EQ(toggled, on);
        ExpectColor(checkbox->FirstChild->Colors[0], Tokens::BgPanel);
        ExpectColor(checkbox->FirstChild->BorderColor, Tokens::BorderSubtle);
        ExpectColor(checkbox->LastChild->TextColor, Tokens::TextDisabled);
        ExpectColor(toggle->Colors[0], on ? Tokens::BgHover : Tokens::BgControl);
        ExpectColor(toggle->FirstChild->Colors[0], Tokens::TextDisabled);
        if (!on)
            ExpectColor(toggle->BorderColor, Tokens::BorderField);
        ZUIEndColumn(&ctx);
    }
}

TEST_F(ZUIWidgetsTest, DisabledMenuCannotActivateFromStaleDragReleaseOrKeyboardNavigation)
{
    Begin();
    auto* root           = ZUIBeginColumn(&ctx, "root", ZPx(300), ZPx(200));
    ctx.MouseReleased[0] = ctx.EnterPressed = true;
    ctx.ActiveKey                           = 123;
    ctx.HotKey                              = ZUIHashStr("##miex_Disabled", 15);
    ctx.PopupNavIdx = ctx.PopupBuildIdx = 0;
    EXPECT_FALSE(ZUIMenuItemEx(&ctx, "Disabled", nullptr, true, false, -1, true));
    ExpectColor(root->LastChild->FirstChild->TextColor, Tokens::TextDisabled);
    ExpectColor(root->LastChild->LastChild->TextColor, Tokens::TextDisabled);
    ZUIBeginDisabled(&ctx);
    EXPECT_FALSE(ZUIMenuItem(&ctx, "Item"));
    EXPECT_FALSE(ZUIComboItem(&ctx, "Combo item", true));
    EXPECT_FALSE(ZUIBeginSubMenu(&ctx, "Submenu"));
    EXPECT_FALSE(ZUIBeginMenu(&ctx, "Menu"));
    ZUIOpenPopup(&ctx, "Disabled popup");
    ZUIEndDisabled(&ctx);
    EXPECT_EQ(ctx.PopupBuildIdx, 0);
    EXPECT_EQ(ctx.PendingPopupKey, 0u);
    ZUIEndColumn(&ctx);
}

TEST_F(ZUIWidgetsTest, SliderGeometryResolvesOnFirstFrameAndAfterResizing)
{
    float value = 0.25f;
    for (float width : {160.f, 320.f})
    {
        Begin();
        auto* root = ZUIBeginColumn(&ctx, "root", ZPx(width), ZPx(100));
        EXPECT_FALSE(ZUISliderFloat(&ctx, "slider", &value, 0.f, 1.f));
        auto* slider = root->LastChild;
        auto* rail   = slider->FirstChild;
        auto* fill   = rail->NextSib;
        auto* thumb  = slider->LastChild;
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
        EXPECT_FLOAT_EQ(slider->ComputedSize[1], 16.f);
        EXPECT_FLOAT_EQ(fill->ComputedSize[0], width * value);
        EXPECT_FLOAT_EQ(fill->ComputedSize[1], 4.f);
        EXPECT_FLOAT_EQ(rail->ComputedSize[1], 4.f);
        EXPECT_FLOAT_EQ(rail->ComputedSize[0], width);
        EXPECT_FLOAT_EQ(rail->ScreenMin[0], fill->ScreenMin[0]);
        EXPECT_FLOAT_EQ(rail->ScreenMin[1], fill->ScreenMin[1]);
        EXPECT_FLOAT_EQ(thumb->ComputedSize[0], 12.f);
        EXPECT_FLOAT_EQ(thumb->ComputedSize[1], 12.f);
        EXPECT_FLOAT_EQ(thumb->ScreenMin[0] + 6.f, slider->ScreenMin[0] + width * value);
        EXPECT_FLOAT_EQ(thumb->ScreenMin[1] + 6.f, slider->ScreenMin[1] + 8.f);
        EXPECT_FLOAT_EQ(thumb->CornerRadii[0], 6.f);
    }
}

TEST_F(ZUIWidgetsTest, SliderHoverAndDragUseBlueRingWithoutFillingTheHitBox)
{
    for (bool dragging : {false, true})
    {
        Begin();
        auto*      root = ZUIBeginColumn(&ctx, "root", ZPx(160), ZPx(100));
        const auto key  = ZUIHashStr("slider", 6);
        ctx.HotKey      = key;
        ctx.ActiveKey   = dragging ? key : 0;
        float value     = 0.5f;
        ZUISliderFloat(&ctx, "slider", &value, 0.f, 1.f);
        auto* slider = root->LastChild;
        auto* thumb  = slider->LastChild;
        EXPECT_TRUE(thumb->Flags & ZUI_DrawBorder);
        EXPECT_FLOAT_EQ(thumb->BorderThickness, 2.f);
        EXPECT_FLOAT_EQ(thumb->BorderColor[0], Tokens::AccentBlue.R);
        EXPECT_FLOAT_EQ(thumb->BorderColor[3], 1.f);
        EXPECT_FLOAT_EQ(slider->Colors[0][3], 0.f);
        ZUIEndColumn(&ctx);
    }
}

TEST_F(ZUIWidgetsTest, DisabledSliderUsesDedicatedColorsAndCannotEditOrActivate)
{
    Begin();
    auto* root   = ZUIBeginColumn(&ctx, "root", ZPx(160), ZPx(100));
    ctx.FocusKey = ZUIHashStr("slider", 6);
    ctx.HotKey = ctx.ActiveKey = ctx.FocusKey;
    ctx.MouseDown[0] = ctx.MousePressed[0] = true;
    ctx.ArrowRightPressed                  = true;
    float value                            = 0.5f;
    ZUIBeginDisabled(&ctx);
    EXPECT_FALSE(ZUISliderFloat(&ctx, "slider", &value, 0.f, 1.f));
    ZUIEndDisabled(&ctx);
    const auto* slider = root->LastChild;
    EXPECT_FALSE(slider->Flags & ZUI_Clickable);
    for (const auto* box : {slider->FirstChild->NextSib, slider->LastChild})
    {
        EXPECT_FLOAT_EQ(box->Colors[0][0], Tokens::TextDisabled.R);
        EXPECT_FLOAT_EQ(box->Colors[0][1], Tokens::TextDisabled.G);
        EXPECT_FLOAT_EQ(box->Colors[0][2], Tokens::TextDisabled.B);
        EXPECT_FLOAT_EQ(box->Colors[0][3], 1.f);
        EXPECT_FALSE(box->Flags & ZUI_DrawBorder);
    }
    EXPECT_FLOAT_EQ(value, 0.5f);
    ZUIEndColumn(&ctx);
}

TEST_F(ZUIWidgetsTest, SliderKeyboardEditsAreClampedAndRenderedWithoutAFrameOfLag)
{
    float value = 0.995f;
    for (int frame = 0; frame < 2; ++frame)
    {
        Begin();
        auto* root            = ZUIBeginColumn(&ctx, "root", ZPx(160), ZPx(100));
        ctx.FocusKey          = ZUIHashStr("slider", 6);
        ctx.ArrowRightPressed = true;
        EXPECT_EQ(ZUISliderFloat(&ctx, "slider", &value, 0.f, 1.f), frame == 0);
        EXPECT_FLOAT_EQ(value, 1.f);
        EXPECT_FLOAT_EQ(root->LastChild->LastChild->FloatAnchor[0], 1.f);
        ZUIEndColumn(&ctx);
    }
}

TEST_F(ZUIWidgetsTest, InvalidSliderRangesDoNotProduceNanGeometryOrMutateValues)
{
    for (float maximum : {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        Begin();
        auto* root            = ZUIBeginColumn(&ctx, "root", ZPx(160), ZPx(100));
        ctx.FocusKey          = ZUIHashStr("slider", 6);
        ctx.ArrowRightPressed = true;
        float value           = 0.5f;
        EXPECT_FALSE(ZUISliderFloat(&ctx, "slider", &value, 0.f, maximum));
        EXPECT_FLOAT_EQ(value, 0.5f);
        EXPECT_FLOAT_EQ(root->LastChild->LastChild->FloatAnchor[0], 0.f);
        ZUIEndColumn(&ctx);
    }
}

TEST_F(ZUIWidgetsTest, ScrollbarHasSixPixelThumbInTenPixelHitLaneAndNoTrack)
{
    // First frame solves overflow; the second builds the scrollbar from it.
    for (int frame = 0; frame < 2; ++frame)
    {
        Begin();
        ctx.MousePos[0] = 191.f; // in the hit lane, outside the visible 192..198 thumb
        ctx.MousePos[1] = 25.f;
        ZUIBeginColumn(&ctx, "root", ZPx(200), ZPx(100));
        auto* region  = ZUIBeginScrollRegion(&ctx, "scroll", ZFill(), ZFill());
        auto* content = ZUIBeginColumn(&ctx, "content", ZPx(200), ZPx(200));
        ZUIEndColumn(&ctx);
        ZUIEndScrollRegion(&ctx);
        auto* lane = region->LastChild;
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
        if (frame == 1)
        {
            ASSERT_NE(lane, content);
            EXPECT_EQ(content->NextSib, lane); // no track box
            EXPECT_EQ(lane->NextSib, nullptr);
            EXPECT_TRUE(lane->Flags & ZUI_Clickable);
            EXPECT_FLOAT_EQ(lane->ComputedSize[0], 10.f);
            EXPECT_FLOAT_EQ(lane->Colors[0][3], 0.f);
            ASSERT_NE(lane->FirstChild, nullptr);
            const auto* visual = lane->FirstChild;
            EXPECT_FALSE(visual->Flags & ZUI_Clickable);
            EXPECT_FLOAT_EQ(visual->ComputedSize[0], 6.f);
            EXPECT_FLOAT_EQ(visual->ScreenMin[0] - lane->ScreenMin[0], 2.f);
            EXPECT_FLOAT_EQ(visual->CornerRadii[0], 3.f);
            EXPECT_FLOAT_EQ(visual->Colors[0][0], Tokens::BgHover.R);
            EXPECT_EQ(ctx.HotKey, lane->Key);
        }
    }
}

TEST_F(ZUIWidgetsTest, HorizontalOnlyScrollbarShowTimerStillExpires)
{
    Begin();
    ZUIBeginColumn(&ctx, "root", ZPx(200), ZPx(100));
    auto* region              = ZUIBeginScrollRegion(&ctx, "scroll", ZFill(), ZFill());
    auto* state               = ZUIStateGetOrInsert(&ctx.StateStore, region->Key);
    state->ScreenMaxX         = 200.f;
    state->ScreenMaxY         = 100.f;
    state->MaxScrollX         = 100.f;
    state->ScrollbarShowTimer = 0.5f;
    ZUIEndScrollRegion(&ctx);
    EXPECT_NEAR(state->ScrollbarShowTimer, 0.5f - 1.f / 60.f, 0.00001f);
    ASSERT_NE(region->LastChild, nullptr);
    EXPECT_FLOAT_EQ(region->LastChild->Size[1].Value, 10.f);
    EXPECT_FLOAT_EQ(region->LastChild->FirstChild->Size[1].Value, 6.f);
    ZUIEndColumn(&ctx);
}

TEST_F(ZUIWidgetsTest, OverlayScrollbarWinsHitTestingOverClickableContentBehindIt)
{
    for (int frame = 0; frame < 3; ++frame)
    {
        Begin();
        ctx.MousePos[0] = 195.f;
        ctx.MousePos[1] = 25.f;
        if (frame == 2)
            ctx.MouseDown[0] = ctx.MousePressed[0] = true;
        ZUIBeginColumn(&ctx, "root", ZPx(200.f), ZPx(100.f));
        auto* region     = ZUIBeginScrollRegion(&ctx, "scroll", ZFill(), ZFill());
        auto* content    = ZUIPushBox(&ctx, "covered", 7, ZUI_Clickable | ZUI_DrawBackground);
        content->Size[0] = ZFill();
        content->Size[1] = ZPx(200.f);
        ZUIPopBox(&ctx);
        ZUIEndScrollRegion(&ctx);
        const auto* thumb = region->LastChild;
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
        if (frame > 0)
        {
            ASSERT_NE(thumb, content);
            EXPECT_EQ(ctx.HotKey, thumb->Key);
            if (frame == 2)
                EXPECT_EQ(ctx.ActiveKey, thumb->Key);
        }
    }
}

TEST_F(ZUIWidgetsTest, ArenaBackedStateAppliesEveryDeclaredDefaultOnlyOnInsertion)
{
    auto* state = State();
    ASSERT_NE(state, nullptr);
    EXPECT_FLOAT_EQ(state->UserData, -1.f);
    EXPECT_EQ(state->SelectStart, -1);
    EXPECT_FLOAT_EQ(state->ScrollY, 0.f);
    state->UserData    = 4.f;
    state->SelectStart = 2;
    EXPECT_EQ(State(), state);
    EXPECT_FLOAT_EQ(State()->UserData, 4.f);
    EXPECT_EQ(State()->SelectStart, 2);
    const auto* other = ZUIStateGetOrInsert(&ctx.StateStore, ctx.FocusKey + ctx.StateStore.Capacity);
    EXPECT_EQ(other->SelectStart, -1);
    EXPECT_FLOAT_EQ(other->UserData, -1.f);
}

TEST_F(ZUIWidgetsTest, FirstTextInputAppendsWithoutAnImplicitSelection)
{
    char buffer[8] = "abc";
    Begin("X");
    EXPECT_TRUE(ZUITextField(&ctx, Key, buffer, sizeof(buffer), 100.f));
    EXPECT_STREQ(buffer, "abcX");
}

TEST_F(ZUIBehaviorTest, EllipsizedLabelsFitMeasuredWidthRatherThanCharacterCount)
{
    glyphs['W' - font.FirstCodepoint].AdvanceX = 14.f;
    glyphs['i' - font.FirstCodepoint].AdvanceX = 2.f;
    Begin();
    auto* wide     = ZUILabelEllipsized(&ctx, "WWWWW", 35.f);
    auto* narrow   = ZUILabelEllipsized(&ctx, "iiiiiiiiiiiiiiiiiiii", 35.f);
    auto* fits     = ZUILabelEllipsized(&ctx, "abc", 21.f);
    auto* tiny     = ZUILabelEllipsized(&ctx, "abc", 2.f);
    auto* negative = ZUILabelEllipsized(&ctx, "abc", -1.f);
    EXPECT_EQ(Label(wide), "W...");
    EXPECT_EQ(Label(narrow), "iiiiiii...");
    EXPECT_EQ(Label(fits), "abc");
    EXPECT_EQ(Label(tiny), "");
    EXPECT_EQ(Label(negative), "");
    EXPECT_EQ(wide->Key, ZUIHashStr("WWWWW", 5));
    for (const auto* label : {wide, narrow, fits, tiny, negative})
        EXPECT_LE(label->Size[0].Value, 35.f);
    End();
}

TEST_F(ZUIBehaviorTest, EllipsizedLabelsUseTheAtlasEllipsisAndLogicalFontScale)
{
    ZUIGlyph extended[224] = {};
    for (auto& glyph : extended)
        glyph.AdvanceX = 10.f;
    font.Glyphs         = extended;
    font.GlyphCount     = 224;
    font.FontScale      = .5f;
    const auto retained = ctx.PersistentArena.m_current_offset;
    Begin();
    auto* label = ZUILabelEllipsized(&ctx, "abcdef", 20.f);
    EXPECT_EQ(Label(label), "abc\x85");
    EXPECT_FLOAT_EQ(label->Size[0].Value, 20.f);
    EXPECT_EQ(ctx.PersistentArena.m_current_offset, retained);
    End();
}

TEST_F(ZUIBehaviorTest, EllipsizedLabelsReuseFrameStorageAndLeaveCallerTextUnchanged)
{
    const char text[] = "abcdefghij";
    Begin();
    ZUILabelTight(&ctx, text);
    const auto tight_bytes = ctx.FrameArena.m_current_offset;
    End();

    Begin();
    auto* label = ZUILabelEllipsized(&ctx, text, 35.f);
    EXPECT_EQ(Label(label), "ab...");
    EXPECT_EQ(ctx.FrameArena.m_current_offset, tight_bytes);
    EXPECT_STREQ(text, "abcdefghij");
    End();
}

TEST_F(ZUIBehaviorTest, EllipsizedLabelsHandleWideGlyphsEmptyInputAndMissingFontMetrics)
{
    glyphs['W' - font.FirstCodepoint].AdvanceX = 100.f;
    Begin();
    auto* suffix_only = ZUILabelEllipsized(&ctx, "W", 21.f);
    auto* expanded    = ZUILabelEllipsized(&ctx, "iW", 28.f);
    EXPECT_EQ(Label(suffix_only), "...");
    EXPECT_EQ(Label(expanded), "i...");
    EXPECT_FLOAT_EQ(expanded->Size[0].Value, 28.f);
    EXPECT_EQ(Label(ZUILabelEllipsized(&ctx, "", 10.f)), "");
    EXPECT_EQ(Label(ZUILabelEllipsized(&ctx, nullptr, 10.f)), "");
    EXPECT_EQ(Label(ZUILabelEllipsized(&ctx, "abc", std::numeric_limits<float>::quiet_NaN())), "");
    font.Glyphs      = nullptr;
    auto* no_metrics = ZUILabelEllipsized(&ctx, "abc", 10.f);
    EXPECT_EQ(Label(no_metrics), "abc");
    EXPECT_FLOAT_EQ(no_metrics->Size[0].Value, 0.f);
    End();
}

TEST_F(ZUIBehaviorTest, TextScalingKeepsTightLabelsEllipsisAndIntrinsicLayoutInAgreement)
{
    for (float scale : {.75f, 1.f, 1.5f})
    {
        Begin();
        const auto* tight   = ZUILabelTight(&ctx, "abcdef", nullptr, ZUIFontSize::Body, "tight", scale);
        const auto* clipped = ZUILabelEllipsized(&ctx, "abcdef", 35.f * scale, nullptr, ZUIFontSize::Body, "clipped", scale);
        EXPECT_FLOAT_EQ(tight->Size[0].Value, 42.f * scale);
        EXPECT_FLOAT_EQ(tight->Size[1].Value, 13.f * scale);
        EXPECT_FLOAT_EQ(clipped->Size[0].Value, 35.f * scale);
        EXPECT_EQ(Label(clipped), "ab...");
        auto* intrinsic    = ZUIPushBox(&ctx, "abc", 3, ZUI_DrawText);
        intrinsic->Size[0] = intrinsic->Size[1] = ZText();
        intrinsic->TextScale                    = scale;
        ZUIPopBox(&ctx);
        ZUILayoutSolve(&ctx);
        EXPECT_FLOAT_EQ(intrinsic->ComputedSize[0], 21.f * scale);
        EXPECT_FLOAT_EQ(intrinsic->ComputedSize[1], 13.f * scale);
        End();
    }
}

TEST_F(ZUIBehaviorTest, InvalidTextScalesFallBackToOneAndDoNotAllocateAdditionalStorage)
{
    Begin();
    ZUILabelEllipsized(&ctx, "abcdefghij", 35.f);
    const auto frame_bytes = ctx.FrameArena.m_current_offset;
    End();
    for (float scale : {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        Begin();
        const auto* label = ZUILabelEllipsized(&ctx, "abcdefghij", 35.f, nullptr, ZUIFontSize::Body, nullptr, scale);
        EXPECT_EQ(Label(label), "ab...");
        EXPECT_FLOAT_EQ(label->TextScale, 1.f);
        EXPECT_EQ(ctx.FrameArena.m_current_offset, frame_bytes);
        End();
    }
}

TEST_F(ZUIBehaviorTest, TightLabelsCanUseStableKeysAndLiteralHashCharacters)
{
    Begin();
    const auto* label = ZUILabelTight(&ctx, "Folder##name", nullptr, ZUIFontSize::Body, "##literal_label");
    EXPECT_EQ(Label(label), "Folder##name");
    EXPECT_EQ(label->Key, ZUIHashStr("##literal_label", 15));
    const auto* clipped = ZUILabelEllipsized(&ctx, "Folder##name", 70.f, nullptr, ZUIFontSize::Body, "##literal_clipped");
    EXPECT_EQ(Label(clipped), "Folder#...");
    EXPECT_EQ(clipped->Key, ZUIHashStr("##literal_clipped", 17));
    End();
}

TEST_F(ZUIBehaviorTest, MenuItemsCanUseStableKeysAndLiteralHashCharacters)
{
    for (const auto* name : {"Folder##one", "Folder##two"})
    {
        Begin();
        EXPECT_FALSE(ZUIMenuItem(&ctx, name, true, "##literal_menu_item"));
        EXPECT_EQ(ctx.Current->LastChild->Key, ZUIHashStr("##literal_menu_item", 19));
        EXPECT_EQ(Label(ctx.Current->LastChild), name);
        End();
    }
}

TEST_F(ZUIBehaviorTest, SearchFieldsWithLongCommonKeyPrefixesKeepIndependentFramesAndCarets)
{
    char first_key[128]  = {};
    char second_key[128] = {};
    secure_memset(first_key, 'k', 100, sizeof(first_key));
    secure_strcpy(second_key, sizeof(second_key), first_key);
    first_key[99]             = '1';
    second_key[99]            = '2';
    char     first_query[16]  = "First";
    char     second_query[16] = "Second";
    uint64_t frame_keys[2]    = {};
    uint64_t caret_keys[2]    = {};
    for (int index = 0; index < 2; ++index)
    {
        ctx.FocusKey = ZUIHashStr(index ? second_key : first_key, 100);
        Begin();
        ZUISearchBox(&ctx, first_key, first_query, sizeof(first_query), nullptr, ZPx(180.f));
        const auto* first = ctx.Current->LastChild;
        ZUISearchBox(&ctx, second_key, second_query, sizeof(second_query), nullptr, ZPx(180.f));
        const auto* second = ctx.Current->LastChild;
        EXPECT_NE(first->Key, second->Key);
        const auto* frame = index ? second : first;
        const auto* field = frame->LastChild;
        ASSERT_NE(field->FirstChild, nullptr);
        frame_keys[index] = frame->Key;
        caret_keys[index] = field->FirstChild->Key;
        EXPECT_TRUE(field->Flags & ZUI_TextNoInset);
        EXPECT_FLOAT_EQ(field->Padding[0], 0.f);
        EXPECT_TRUE(field->Flags & ZUI_DrawBackground);
        EXPECT_FALSE(field->Flags & ZUI_DrawBorder);
        for (const auto& corner : field->Colors)
            EXPECT_FLOAT_EQ(corner[3], 0.f);
        End();
    }
    EXPECT_NE(frame_keys[0], frame_keys[1]);
    EXPECT_NE(caret_keys[0], caret_keys[1]);
}

TEST_F(ZUIBehaviorTest, SearchClearButtonResetsTheQueryAndReturnsFocusToTheField)
{
    char       query[32] = "Materials";
    char       key[64]   = {};
    const auto field     = ZUIHashStr("search", 6);
    snprintf(key, sizeof(key), "##sb_clear_%016llx", static_cast<unsigned long long>(field));
    const auto clear = ZUIHashStr(key, static_cast<uint32_t>(secure_strlen(key)));
    ctx.HotKey = ctx.ActiveKey = clear;
    ctx.MouseReleased[0]       = true;
    Begin();
    EXPECT_TRUE(ZUISearchBox(&ctx, "search", query, sizeof(query), "Search Content", ZPx(300.f), true));
    EXPECT_STREQ(query, "");
    EXPECT_EQ(ctx.FocusKey, field);
    EXPECT_TRUE(ctx.TextInputActive);
    End();
    Begin();
    EXPECT_FALSE(ZUISearchBox(&ctx, "search", query, sizeof(query), "Search Content", ZPx(300.f), true));
    EXPECT_EQ(Find(ctx.Root, clear), nullptr);
    End();
}

TEST_F(ZUIBehaviorTest, DisabledSearchCannotBeClearedByStaleInput)
{
    char       query[32] = "Materials";
    char       key[64]   = {};
    const auto field     = ZUIHashStr("search", 6);
    snprintf(key, sizeof(key), "##sb_clear_%016llx", static_cast<unsigned long long>(field));
    ctx.HotKey = ctx.ActiveKey = ZUIHashStr(key, static_cast<uint32_t>(secure_strlen(key)));
    ctx.MouseReleased[0]       = true;
    Begin();
    ZUIBeginDisabled(&ctx);
    EXPECT_FALSE(ZUISearchBox(&ctx, "search", query, sizeof(query), "Search Content", ZPx(300.f), true));
    ZUIEndDisabled(&ctx);
    EXPECT_STREQ(query, "Materials");
    EXPECT_FALSE(ctx.TextInputActive);
    End();
}

TEST_F(ZUIBehaviorTest, SearchUsesSharedSelectionUndoAndRestoresTheCallerStyle)
{
    char query[32]        = "Materials";
    ctx.FocusKey          = ZUIHashStr("search", 6);
    const float padding_x = ctx.Style.FramePadding[0];
    const float padding_y = ctx.Style.FramePadding[1];
    Begin();
    ctx.CtrlAPressed = true;
    EXPECT_FALSE(ZUISearchBox(&ctx, "search", query, sizeof(query), "Search Content", ZPx(300.f), true));
    EXPECT_STREQ(query, "Materials");
    EXPECT_EQ(ZUIStateGetOrInsert(&ctx.StateStore, ctx.FocusKey)->SelectStart, 0);
    EXPECT_FLOAT_EQ(ctx.Style.FramePadding[0], padding_x);
    EXPECT_FLOAT_EQ(ctx.Style.FramePadding[1], padding_y);
    EXPECT_EQ(ctx.StyleStackDepth, 0u);
    End();
    Begin();
    secure_strcpy(ctx.TextInput, sizeof(ctx.TextInput), "New");
    ctx.TextInputLen = 3;
    EXPECT_TRUE(ZUISearchBox(&ctx, "search", query, sizeof(query), "Search Content", ZPx(300.f), true));
    EXPECT_STREQ(query, "New");
    End();
    Begin();
    ctx.CtrlZPressed = true;
    EXPECT_TRUE(ZUISearchBox(&ctx, "search", query, sizeof(query), "Search Content", ZPx(300.f), true));
    EXPECT_STREQ(query, "Materials");
    End();
}

class ZUIFloatInputTest : public ZUIBehaviorTest
{
protected:
    bool Draw(float& value, const char* key = "float")
    {
        Begin();
        const bool changed = ZUIInputFloat(&ctx, key, &value);
        End();
        return changed;
    }
    void Focus(float& value, const char* key = "float")
    {
        ctx.FocusKey = ZUIHashStr(key, static_cast<uint32_t>(secure_strlen(key)));
        EXPECT_FALSE(Draw(value, key));
    }
    void Type(float& value, const char* text, const char* key = "float")
    {
        ctx.CtrlAPressed = true;
        EXPECT_FALSE(Draw(value, key));
        ctx.BackspacePressed = true;
        EXPECT_FALSE(Draw(value, key));
        secure_strcpy(ctx.TextInput, sizeof(ctx.TextInput), text);
        ctx.TextInputLen = static_cast<uint32_t>(secure_strlen(text));
        EXPECT_FALSE(Draw(value, key));
    }
};

TEST_F(ZUIFloatInputTest, TextSurvivesIdleFramesAndCommitsOnBlur)
{
    float value = 1.f;
    Focus(value);
    Type(value, "2.5");
    for (int frame = 0; frame < 5; ++frame)
    {
        EXPECT_FALSE(Draw(value));
        EXPECT_EQ(Label(Find(ctx.Root, ZUIHashStr("float", 5))), "2.5");
        EXPECT_FLOAT_EQ(value, 1.f);
    }
    ctx.FocusKey = 0;
    EXPECT_TRUE(Draw(value));
    EXPECT_FLOAT_EQ(value, 2.5f);
    EXPECT_FALSE(Draw(value));
}

TEST_F(ZUIFloatInputTest, DisplayOnlyAndDisabledFieldsDoNotAllocateEditBuffers)
{
    const auto initial_bytes = ctx.PersistentArena.m_current_offset;
    float      value         = 1.f;
    for (int i = 0; i < 8; ++i)
    {
        Begin();
        EXPECT_FALSE(ZUIInputFloat(&ctx, "first", &value));
        EXPECT_FALSE(ZUIInputFloat(&ctx, "second", &value));
        ctx.Disabled = true;
        ctx.FocusKey = ZUIHashStr("disabled", 8);
        EXPECT_FALSE(ZUIInputFloat(&ctx, "disabled", &value));
        ctx.Disabled = false;
        End();
        EXPECT_EQ(ctx.PersistentArena.m_current_offset, initial_bytes);
    }
    for (const char* key : {"first", "second", "disabled"})
        EXPECT_EQ(ZUIStateGetOrInsert(&ctx.StateStore, ZUIHashStr(key, static_cast<uint32_t>(secure_strlen(key))))->FloatInputState, nullptr);
}

TEST_F(ZUIFloatInputTest, FirstFocusInitializesRetainedStateAndReusesItAcrossEditingSessions)
{
    const auto initial_bytes = ctx.PersistentArena.m_current_offset;
    float      value         = 1.f;
    Focus(value);
    const auto* state = ZUIStateGetOrInsert(&ctx.StateStore, ZUIHashStr("float", 5))->FloatInputState;
    ASSERT_NE(state, nullptr);
    EXPECT_STREQ(state->Buffer, "1");
    EXPECT_TRUE(state->Editing);
    EXPECT_FALSE(state->Edited);
    EXPECT_EQ(state->BindingIdentity, reinterpret_cast<uintptr_t>(&value));
    EXPECT_EQ(state->LastFrame, ctx.FrameIndex);
    EXPECT_FLOAT_EQ(state->LastValue, value);
    const auto allocated_bytes = ctx.PersistentArena.m_current_offset;
    EXPECT_GE(allocated_bytes - initial_bytes, sizeof(ZUIFloatInputState));
    for (int i = 0; i < 8; ++i)
    {
        ctx.EscapePressed = true;
        EXPECT_FALSE(Draw(value));
        Focus(value);
        EXPECT_EQ(ctx.PersistentArena.m_current_offset, allocated_bytes);
        EXPECT_EQ(ZUIStateGetOrInsert(&ctx.StateStore, ZUIHashStr("float", 5))->FloatInputState, state);
    }
}

TEST_F(ZUIFloatInputTest, ExhaustedPersistentArenaDoesNotCrashOrModifyTheModel)
{
    // Limit the arena to its already-used space to force allocation failure.
    ctx.PersistentArena.m_total_size = ctx.PersistentArena.m_current_offset;
    float value                      = 1.f;
    ctx.FocusKey                     = ZUIHashStr("float", 5);
    secure_strcpy(ctx.TextInput, sizeof(ctx.TextInput), "3");
    ctx.TextInputLen = 1;
    EXPECT_FALSE(Draw(value));
    EXPECT_FLOAT_EQ(value, 1.f);
    EXPECT_EQ(ctx.FocusKey, 0u);
    EXPECT_FALSE(ctx.TextInputActive);
    EXPECT_EQ(ZUIStateGetOrInsert(&ctx.StateStore, ZUIHashStr("float", 5))->FloatInputState, nullptr);
    EXPECT_EQ(Label(Find(ctx.Root, ZUIHashStr("float", 5))), "1");
}

TEST_F(ZUIFloatInputTest, EnterTabAndShiftTabCommitCompleteFiniteNumbers)
{
    for (int finish = 0; finish < 3; ++finish)
    {
        float value = 1.f;
        Focus(value);
        Type(value, "-2.5e+2");
        ctx.EnterPressed    = finish == 0;
        ctx.TabPressed      = finish == 1;
        ctx.ShiftTabPressed = finish == 2;
        EXPECT_TRUE(Draw(value));
        EXPECT_FLOAT_EQ(value, -250.f);
        EXPECT_FALSE(Draw(value));
    }
}

TEST_F(ZUIFloatInputTest, InvalidTextCannotSilentlyBecomeZeroOrAPartialNumber)
{
    for (const char* text : {"", "-", ".", "2junk", "2 3", "nan", "inf", "1e100", "1e-100"})
    {
        SCOPED_TRACE(text);
        float value = 1.f;
        Focus(value);
        Type(value, text);
        ctx.EnterPressed = true;
        EXPECT_FALSE(Draw(value));
        EXPECT_FLOAT_EQ(value, 1.f);
        EXPECT_EQ(Label(Find(ctx.Root, ZUIHashStr("float", 5))), "1");
    }
}

TEST_F(ZUIFloatInputTest, EscapeDiscardsTheEditWithoutChangingTheModel)
{
    float value = 1.f;
    Focus(value);
    Type(value, "3");
    ctx.EscapePressed = true;
    EXPECT_FALSE(Draw(value));
    EXPECT_EQ(ctx.FocusKey, 0u);
    EXPECT_FLOAT_EQ(value, 1.f);
    EXPECT_EQ(Label(Find(ctx.Root, ZUIHashStr("float", 5))), "1");
}

TEST_F(ZUIFloatInputTest, ReopeningDoesNotReusePreviousCursorSelectionOrUndoHistory)
{
    float value = 12.f;
    Focus(value);
    Type(value, "9");
    ctx.CtrlAPressed = true;
    EXPECT_FALSE(Draw(value));
    ctx.EscapePressed = true;
    EXPECT_FALSE(Draw(value));
    Focus(value);
    const auto* state = ZUIStateGetOrInsert(&ctx.StateStore, ZUIHashStr("float", 5));
    EXPECT_EQ(state->SelectStart, -1);
    EXPECT_FLOAT_EQ(state->UserData, 2.f);
    EXPECT_EQ(ctx.UndoTop, 0u);
    EXPECT_EQ(ctx.RedoTop, 0u);
    secure_strcpy(ctx.TextInput, sizeof(ctx.TextInput), "3");
    ctx.TextInputLen = 1;
    EXPECT_FALSE(Draw(value));
    ctx.EnterPressed = true;
    EXPECT_TRUE(Draw(value));
    EXPECT_FLOAT_EQ(value, 123.f);
}

TEST_F(ZUIFloatInputTest, FirstFocusPreservesTheNewClickCaretPosition)
{
    float value = 123.f;
    EXPECT_FALSE(Draw(value));
    const auto key   = ZUIHashStr("float", 5);
    auto*      state = ZUIStateGetOrInsert(&ctx.StateStore, key);
    // ZUITextField resolves the first click's caret before the input's next
    // focused build. Starting an edit must preserve that new cursor position.
    state->UserData  = 1.f;
    ctx.FocusKey     = key;
    EXPECT_FALSE(Draw(value));
    EXPECT_FLOAT_EQ(state->UserData, 1.f);
    secure_strcpy(ctx.TextInput, sizeof(ctx.TextInput), "4");
    ctx.TextInputLen = 1;
    EXPECT_FALSE(Draw(value));
    ctx.EnterPressed = true;
    EXPECT_TRUE(Draw(value));
    EXPECT_FLOAT_EQ(value, 1423.f);
}

TEST_F(ZUIFloatInputTest, NoOpEditingPreservesExactExtremeAndFractionalValues)
{
    for (float value : {0.123456789f, std::numeric_limits<float>::max(), std::numeric_limits<float>::denorm_min(), -0.f})
    {
        const float original = value;
        Focus(value);
        EXPECT_FALSE(Draw(value));
        ctx.EnterPressed = true;
        EXPECT_FALSE(Draw(value));
        EXPECT_EQ(secure_memcmp(&value, sizeof(value), &original, sizeof(original), sizeof(value)), 0);
    }
}

TEST_F(ZUIFloatInputTest, RebindingExternalUpdatesAndHiddenFieldsDiscardStaleEdits)
{
    float first = 1.f, second = 7.f;
    Focus(first);
    Type(first, "3");
    ctx.EnterPressed = true;
    EXPECT_FALSE(Draw(second));
    EXPECT_FLOAT_EQ(first, 1.f);
    EXPECT_FLOAT_EQ(second, 7.f);

    Focus(first);
    Type(first, "3");
    first            = 9.f;
    ctx.EnterPressed = true;
    EXPECT_FALSE(Draw(first));
    EXPECT_FLOAT_EQ(first, 9.f);

    Focus(first);
    Type(first, "3");
    Begin();
    End(); // A closed/hidden panel must not revive an old edit on return.
    ctx.EnterPressed = true;
    EXPECT_FALSE(Draw(first));
    EXPECT_FLOAT_EQ(first, 9.f);
}

TEST_F(ZUIFloatInputTest, DisablingAnActiveEditDiscardsIt)
{
    float value = 1.f;
    Focus(value);
    Type(value, "3");
    ctx.Disabled = true;
    EXPECT_FALSE(Draw(value));
    EXPECT_FALSE(ctx.TextInputActive);
    ctx.Disabled     = false;
    ctx.EnterPressed = true;
    EXPECT_FALSE(Draw(value));
    EXPECT_FLOAT_EQ(value, 1.f);
}

TEST_F(ZUIFloatInputTest, FieldsKeepIndependentBuffersRegardlessOfBuildOrder)
{
    float a = 1.f, b = 2.f;
    auto  draw = [&](bool reverse) {
        Begin();
        bool changed = false;
        if (reverse)
            changed |= ZUIInputFloat(&ctx, "b", &b);
        changed |= ZUIInputFloat(&ctx, "a", &a);
        if (!reverse)
            changed |= ZUIInputFloat(&ctx, "b", &b);
        End();
        return changed;
    };
    ctx.FocusKey = ZUIHashStr("a", 1);
    EXPECT_FALSE(draw(false));
    ctx.CtrlAPressed = true;
    EXPECT_FALSE(draw(false));
    secure_strcpy(ctx.TextInput, sizeof(ctx.TextInput), "4");
    ctx.TextInputLen = 1;
    EXPECT_FALSE(draw(false));
    ctx.FocusKey = ZUIHashStr("b", 1);
    EXPECT_TRUE(draw(true));
    EXPECT_FLOAT_EQ(a, 4.f);
    EXPECT_FLOAT_EQ(b, 2.f);
    ctx.EnterPressed = true;
    EXPECT_FALSE(draw(false));
}

TEST_F(ZUIBehaviorTest, OpaqueTextSelectionRedrawsTheSelectedGlyphsAboveTheHighlight)
{
    char buffer[8]     = "abcd";
    ctx.FocusKey       = ZUIHashStr("input", 5);
    auto* state        = ZUIStateGetOrInsert(&ctx.StateStore, ctx.FocusKey);
    state->UserData    = 3.f;
    state->SelectStart = 1;
    Begin();
    ZUITextField(&ctx, "input", buffer, sizeof(buffer), 100.f);
    const auto* field     = ctx.Current->LastChild;
    const auto* selection = field->FirstChild;
    End();
    ASSERT_NE(selection, nullptr);
    EXPECT_TRUE(selection->Flags & ZUI_DrawBackground);
    EXPECT_TRUE(selection->Flags & ZUI_DrawText);
    EXPECT_EQ(Label(selection), "bc");
    EXPECT_EQ(selection->TextAlign, ZUITextAlign::Center);
    EXPECT_FLOAT_EQ(selection->ComputedSize[0], 14.f);
    EXPECT_FLOAT_EQ(selection->ScreenMin[0], field->ScreenMin[0] + ZUIGetFramePadX(&ctx) + 7.f);
    EXPECT_FLOAT_EQ(selection->TextColor[3], 1.f);
    EXPECT_FLOAT_EQ(selection->Colors[0][3], 1.f);
}

TEST_F(ZUIWidgetsTest, BoundedTextMovesPreserveTerminatorAtCapacity)
{
    char buffer[5] = "abcd";
    Begin("X");
    EXPECT_FALSE(ZUITextField(&ctx, Key, buffer, sizeof(buffer), 100.f));
    EXPECT_STREQ(buffer, "abcd");
    State()->UserData = 2.f;
    Begin();
    ctx.BackspacePressed = true;
    EXPECT_TRUE(ZUITextField(&ctx, Key, buffer, sizeof(buffer), 100.f));
    EXPECT_STREQ(buffer, "acd");
    Begin();
    ctx.BackspacePressed = false;
    ctx.DeletePressed    = true;
    State()->UserData    = 2.f;
    EXPECT_TRUE(ZUITextField(&ctx, Key, buffer, sizeof(buffer), 100.f));
    EXPECT_STREQ(buffer, "ac");
}

TEST_F(ZUIWidgetsTest, CutCopiesOnlySelectedBytesAndUndoRestoresTheValue)
{
    char buffer[8]       = "abcd";
    State()->UserData    = 3.f;
    State()->SelectStart = 1;
    Begin();
    ctx.CtrlXPressed = true;
    EXPECT_TRUE(ZUITextField(&ctx, Key, buffer, sizeof(buffer), 100.f));
    EXPECT_STREQ(buffer, "ad");
    EXPECT_STREQ(ctx.ClipboardWrite, "bc");
    Begin();
    ctx.CtrlXPressed = false;
    ctx.CtrlZPressed = true;
    EXPECT_TRUE(ZUITextField(&ctx, Key, buffer, sizeof(buffer), 100.f));
    EXPECT_STREQ(buffer, "abcd");
}

TEST_F(ZUIWidgetsTest, StaleCursorAndSelectionClampToTheCurrentValue)
{
    char buffer[8]       = "abc";
    State()->UserData    = 100.f;
    State()->SelectStart = 100;
    Begin();
    ctx.BackspacePressed = true;
    EXPECT_TRUE(ZUITextField(&ctx, Key, buffer, sizeof(buffer), 100.f));
    EXPECT_STREQ(buffer, "ab");
}

TEST_F(ZUIWidgetsTest, SearchRetainsAllKeyBitsAndEditsAtTheCursor)
{
    ASSERT_NE(ctx.FocusKey >> 32, 0u);
    char buffer[8]    = "abc";
    State()->UserData = 1.f;
    Begin("X");
    EXPECT_TRUE(ZUISearchBox(&ctx, Key, buffer, sizeof(buffer), "Search", ZPx(100.f)));
    EXPECT_STREQ(buffer, "aXbc");
    EXPECT_TRUE(ctx.TextInputActive);
}

TEST_F(ZUIWidgetsTest, UndoHistoryRolloverUsesBoundedOverlappingMoves)
{
    char buffer[128] = {};
    for (uint32_t i = 0; i < ZUIContext::kUndoDepth + 4; ++i)
    {
        Begin("a");
        EXPECT_TRUE(ZUITextField(&ctx, Key, buffer, sizeof(buffer), 100.f));
    }
    EXPECT_EQ(secure_strlen(buffer), ZUIContext::kUndoDepth + 4);
    EXPECT_EQ(ctx.UndoTop, ZUIContext::kUndoDepth);
    Begin();
    ctx.CtrlZPressed = true;
    EXPECT_TRUE(ZUITextField(&ctx, Key, buffer, sizeof(buffer), 100.f));
    EXPECT_EQ(secure_strlen(buffer), ZUIContext::kUndoDepth + 3);
}

TEST_F(ZUIWidgetsTest, FileDropPreservesKindAndRejectsTruncation)
{
    Begin();
    ZUIBox source    = {};
    ZUIBox target    = {};
    source.Key       = 1;
    target.Key       = 2;
    ctx.ActiveKey    = source.Key;
    ctx.MouseDown[0] = true;
    ctx.MousePos[0]  = 20.f;
    ZUIBeginDragSource(&ctx, &source, "/Ship.GLB", 9, ZUIDragPayloadKind::VFSPath);
    EXPECT_EQ(ctx.DragPayloadKind, ZUIDragPayloadKind::VFSPath);
    EXPECT_EQ(ctx.DragPayloadLen, 9u);
    ctx.DragDropFired          = true;
    ctx.DragTargetKey          = target.Key;
    char               out[10] = {};
    ZUIDragPayloadKind kind    = ZUIDragPayloadKind::Opaque;
    EXPECT_FALSE(ZUIAcceptDrop(&ctx, &target, out, sizeof(out) - 1, &kind));
    EXPECT_EQ(kind, ZUIDragPayloadKind::Opaque);
    EXPECT_TRUE(ZUIAcceptDrop(&ctx, &target, out, sizeof(out), &kind));
    EXPECT_EQ(kind, ZUIDragPayloadKind::VFSPath);
    EXPECT_STREQ(out, "/Ship.GLB");
    ++ctx.DragPayloadLen; // Legacy source includes its terminator.
    EXPECT_TRUE(ZUIAcceptDrop(&ctx, &target, out, sizeof(out)));
    EXPECT_STREQ(out, "/Ship.GLB");
    ctx.DragPayloadLen = sizeof(ctx.DragPayload) + 1;
    EXPECT_FALSE(ZUIAcceptDrop(&ctx, &target, out, sizeof(out)));
}

TEST_F(ZUIWidgetsTest, BinaryPayloadIsCopiedExactlyIncludingTrailingZeroBytes)
{
    Begin();
    ZUIBox source       = {};
    ZUIBox target       = {};
    source.Key          = 1;
    target.Key          = 2;
    ctx.ActiveKey       = source.Key;
    ctx.MouseDown[0]    = true;
    ctx.MousePos[0]     = 20.f;
    const char bytes[8] = {1, 2, 0, 3, 0, 0, 0, 0};
    ZUIBeginDragSource(&ctx, &source, bytes, sizeof(bytes));
    ctx.DragDropFired       = true;
    ctx.DragTargetKey       = target.Key;
    char out[sizeof(bytes)] = {};
    EXPECT_TRUE(ZUIAcceptDrop(&ctx, &target, out, sizeof(out)));
    EXPECT_EQ(secure_memcmp(out, sizeof(out), bytes, sizeof(bytes), sizeof(bytes)), 0);
    EXPECT_FALSE(ZUIAcceptDrop(&ctx, &target, out, sizeof(out) - 1));
}

TEST_F(ZUIWidgetsTest, InvalidPathPayloadCannotStartADrag)
{
    Begin();
    ZUIBox source                               = {};
    source.Key                                  = 1;
    ctx.ActiveKey                               = source.Key;
    ctx.MouseDown[0]                            = true;
    ctx.MousePos[0]                             = 20.f;
    char oversized[sizeof(ctx.DragPayload) + 1] = {};
    secure_memset(oversized, 'a', sizeof(oversized), sizeof(oversized));
    ZUIBeginDragSource(&ctx, &source, oversized, sizeof(oversized), ZUIDragPayloadKind::NativePath);
    EXPECT_EQ(ctx.DragSourceKey, 0u);
    ZUIBeginDragSource(&ctx, &source, oversized, sizeof(ctx.DragPayload), ZUIDragPayloadKind::NativePath);
    EXPECT_EQ(ctx.DragSourceKey, 0u);
    const char embedded_nul[] = {'a', '\0', 'b'};
    ZUIBeginDragSource(&ctx, &source, embedded_nul, sizeof(embedded_nul), ZUIDragPayloadKind::NativePath);
    EXPECT_EQ(ctx.DragSourceKey, 0u);
    ZUIBeginDragSource(&ctx, &source, "", 0, ZUIDragPayloadKind::NativePath);
    EXPECT_EQ(ctx.DragSourceKey, 0u);
    ZUIBeginDragSource(&ctx, &source, "/Ship.GLB", sizeof("/Ship.GLB"), ZUIDragPayloadKind::NativePath);
    EXPECT_EQ(ctx.DragSourceKey, source.Key);
    EXPECT_EQ(ctx.DragPayloadLen, sizeof("/Ship.GLB") - 1);
}

TEST_F(ZUIWidgetsTest, DropPayloadSurvivesTheRealReleaseInteractionUntilNextBuild)
{
    Begin();
    ctx.ScreenW = 200;
    ctx.ScreenH = 100;
    ZUIBeginRow(&ctx, "##drop_root", ZPx(200), ZPx(100));
    auto* source     = ZUIPushBox(&ctx, "source", 6, ZUI_Clickable);
    source->Size[0]  = ZPx(100);
    source->Size[1]  = ZPx(100);
    ctx.ActiveKey    = source->Key;
    ctx.MouseDown[0] = true;
    ctx.MousePos[0]  = 20.f;
    ZUIBeginDragSource(&ctx, source, "/Ship.glb", 9, ZUIDragPayloadKind::VFSPath);
    ZUIPopBox(&ctx);
    auto* target    = ZUIPushBox(&ctx, "target", 6, ZUI_Clickable);
    target->Size[0] = ZPx(100);
    target->Size[1] = ZPx(100);
    ZUIPopBox(&ctx);
    ZUIEndRow(&ctx);
    ctx.MouseDown[0]     = false;
    ctx.MouseReleased[0] = true;
    ctx.MousePos[0]      = 150.f;
    ctx.MousePos[1]      = 50.f;
    ZUIEndFrame(&ctx);
    EXPECT_TRUE(ctx.DragDropFired);
    EXPECT_EQ(ctx.DragTargetKey, target->Key);
    ZUIBeginFrame(&ctx, 1.f / 60.f);
    // Frame arena contents may now be overwritten: use a stable key, not old boxes.
    ZUIBox next_target         = {};
    next_target.Key            = ZUIHashStr("target", 6);
    char               out[16] = {};
    ZUIDragPayloadKind kind    = {};
    EXPECT_TRUE(ZUIAcceptDrop(&ctx, &next_target, out, sizeof(out), &kind));
    EXPECT_STREQ(out, "/Ship.glb");
    EXPECT_EQ(kind, ZUIDragPayloadKind::VFSPath);
}

TEST_F(ZUIWidgetsTest, ForegroundScopesRestoreParentsAndResetWithTheFrameArena)
{
    ctx.ScreenW = 400;
    ctx.ScreenH = 300;
    Begin();
    auto* root  = ZUIBeginColumn(&ctx, "root", ZPx(400), ZPx(300));
    auto* saved = ZUIBeginForeground(&ctx);
    EXPECT_EQ(saved, root);
    auto* foreground = ctx.ForegroundRoot;
    ASSERT_NE(foreground, nullptr);
    auto* nested = ZUIBeginForeground(&ctx);
    EXPECT_EQ(nested, foreground);
    EXPECT_EQ(ctx.ForegroundRoot, foreground);
    ZUIEndForeground(&ctx, nested);
    EXPECT_EQ(ctx.Current, foreground);
    ZUIEndForeground(&ctx, saved);
    EXPECT_EQ(ctx.Current, root);
    ZUIEndColumn(&ctx);
    ZUIEndFrame(&ctx); // Already last: promotion must not create a sibling cycle.
    EXPECT_EQ(root->LastChild, foreground);
    EXPECT_EQ(foreground->NextSib, nullptr);
    Begin();
    EXPECT_EQ(ctx.ForegroundRoot, nullptr);
}
