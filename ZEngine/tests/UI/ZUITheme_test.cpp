#include <ZEngine/UI/ZUIContext.h>
#include <gtest/gtest.h>

using namespace ZEngine::UI;

namespace
{
    void ExpectColor(const float actual[4], uint32_t rgb, float alpha = 1.f)
    {
        const Tokens::Color expected = Tokens::Hex(rgb, alpha);
        EXPECT_FLOAT_EQ(actual[0], expected.R);
        EXPECT_FLOAT_EQ(actual[1], expected.G);
        EXPECT_FLOAT_EQ(actual[2], expected.B);
        EXPECT_FLOAT_EQ(actual[3], expected.A);
    }
} // namespace

// The hex literals below are copied from the Figma "Editor tokens" (Dark) collection. If one of
// these fails after a Tokens:: edit, the code has drifted from the design: fix the code, or update
// Figma first and then this table.
TEST(ZUIThemeTest, TokensMatchFigmaEditorTokens)
{
    using namespace Tokens;
    const float tokens[][4] = {
        ZUI_TOKEN(BgWindow),   ZUI_TOKEN(BgPanel),         ZUI_TOKEN(BgSurface),          ZUI_TOKEN(BgControl),      ZUI_TOKEN(BgHover),           ZUI_TOKEN(BgViewport),       ZUI_TOKEN(BgGrid),         ZUI_TOKEN(BorderSubtle),         ZUI_TOKEN(BorderStrong),           ZUI_TOKEN(BorderField),  ZUI_TOKEN(TextPrimary), ZUI_TOKEN(TextSecondary), ZUI_TOKEN(TextDisabled),
        ZUI_TOKEN(AccentBlue), ZUI_TOKEN(AccentSelection), ZUI_TOKEN(AccentGreen),        ZUI_TOKEN(AccentRed),      ZUI_TOKEN(AccentOrange),      ZUI_TOKEN(BgBackdrop),       ZUI_TOKEN(BorderSelected), ZUI_TOKEN(AccentSelectionHover), ZUI_TOKEN(AccentSelectionPressed), ZUI_TOKEN(TextSelected), ZUI_TOKEN(PinFloat),    ZUI_TOKEN(PinVector),     ZUI_TOKEN(PinTexture),
        ZUI_TOKEN(PinExec),    ZUI_TOKEN(PinInt),          ZUI_TOKEN(NodeHeaderConstant), ZUI_TOKEN(NodeHeaderMath), ZUI_TOKEN(NodeHeaderTexture), ZUI_TOKEN(NodeHeaderResult), ZUI_TOKEN(NodeBody),
    };
    const uint32_t figma[] = {
        0x171717, 0x202020, 0x292929, 0x2e3133, 0x393939, 0x343b41, 0x3f474d, 0x393939, 0x565d61, 0x4a5359, 0xd8d8d8, 0x969696, 0x687177, 0x579de3, 0x174d78, 0x73bc78, 0xdd6261, 0xe8a442, 0x151515, 0x2684d9, 0x1e5d90, 0x113a5a, 0xaacce8, 0x73bc78, 0xe8c442, 0xdd6261, 0xd8d8d8, 0x5ec4c4, 0x2f5d34, 0x4a5359, 0x7a3636, 0x174d78, 0x262a2d,
    };
    static_assert(sizeof(tokens) / sizeof(tokens[0]) == 33);
    static_assert(sizeof(tokens) / sizeof(tokens[0]) == sizeof(figma) / sizeof(figma[0]));
    for (size_t i = 0; i < sizeof(figma) / sizeof(figma[0]); ++i)
    {
        SCOPED_TRACE(i);
        ExpectColor(tokens[i], figma[i]);
    }
}

TEST(ZUIThemeTest, MetricsMatchFigmaEditorMetrics)
{
    using namespace Metrics;
    // Independent values copied from Figma's Editor metrics / Default collection.
    const float metrics[] = {
        Space2, Space3, Space4, Space5, Space6, Space7, Space8, Space9, Space10, Space12, Space16, Space20, Radius2, Radius3, Radius4, Radius6, Radius8, HeightControlSm, HeightRowCompact, HeightField, HeightRowCheck, HeightSearch, HeightControl, HeightRow, HeightTab,
    };
    const float figma[] = {
        2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f, 10.f, 12.f, 16.f, 20.f, 2.f, 3.f, 4.f, 6.f, 8.f, 16.f, 20.f, 22.f, 24.f, 25.f, 26.f, 28.f, 30.f,
    };
    static_assert(sizeof(metrics) / sizeof(metrics[0]) == 25);
    static_assert(sizeof(metrics) / sizeof(metrics[0]) == sizeof(figma) / sizeof(figma[0]));
    for (size_t i = 0; i < sizeof(figma) / sizeof(figma[0]); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_FLOAT_EQ(metrics[i], figma[i]);
    }
}

TEST(ZUIThemeTest, ScrollbarAndShadowDefaultsMatchFigmaRecipes)
{
    const ZUITheme theme = {};
    const ZUIStyle style = {};
    ExpectColor(theme.ScrollbarGrab, 0x393939);
    ExpectColor(theme.ScrollbarGrabHov, 0x565d61);
    ExpectColor(theme.ScrollbarGrabAct, 0x565d61);
    EXPECT_FLOAT_EQ(theme.ScrollbarBg[3], 0.f);
    EXPECT_FLOAT_EQ(style.ScrollbarSize, 10.f);
    EXPECT_FLOAT_EQ(style.ScrollbarRounding, 3.f);
    EXPECT_FLOAT_EQ(style.MenuShadowOffsetY, 10.f);
    EXPECT_FLOAT_EQ(style.MenuShadowBlur, 28.f);
    EXPECT_FLOAT_EQ(style.MenuShadowAlpha, 0.55f);
    EXPECT_FLOAT_EQ(style.ModalShadowOffsetY, 8.f);
    EXPECT_FLOAT_EQ(style.ModalShadowBlur, 24.f);
    EXPECT_FLOAT_EQ(style.ModalShadowAlpha, 0.50f);
}

TEST(ZUIThemeTest, DefaultThemeUsesTheFigmaBlueDarkPalette)
{
    const ZUITheme t = {};
    ExpectColor(t.WindowBg, 0x171717);
    ExpectColor(t.PanelBg, 0x202020);
    ExpectColor(t.PanelBgAlt, 0x292929);
    ExpectColor(t.InputBg, 0x171717);
    ExpectColor(t.ButtonBg, 0x292929);
    ExpectColor(t.WindowBackdrop, 0x151515);
    ExpectColor(t.ButtonHoveredBg, 0x393939);
    ExpectColor(t.ButtonActiveBg, 0x174d78);
    ExpectColor(t.RowSelectedBg, 0x174d78);
    ExpectColor(t.TabActiveBg, 0x202020);
    ExpectColor(t.TabActiveBorder, 0x579de3);
    ExpectColor(t.TextDefault, 0xd8d8d8);
    ExpectColor(t.TextDim, 0x969696);
    ExpectColor(t.TextAccent, 0x579de3);
    ExpectColor(t.TextWarn, 0xe8a442);
    ExpectColor(t.TextError, 0xdd6261);
    ExpectColor(t.PanelBorder, 0x393939);
    ExpectColor(t.InputBorder, 0x4a5359);
    ExpectColor(t.PopupBg, 0x202020);
    ExpectColor(t.PopupBorder, 0x565d61);
    ExpectColor(t.InputFocusBorder, 0x579de3);
}

TEST(ZUIThemeTest, InactiveTabsHaveNoFill)
{
    const ZUITheme t = {};
    EXPECT_FLOAT_EQ(t.TabInactiveBg[3], 0.f);
    EXPECT_FLOAT_EQ(t.TabDimmedBg[3], 0.f);
}

TEST(ZUIThemeTest, NoTealRemainsInTheDefaultTheme)
{
    const ZUITheme t       = {};
    const float    teal[3] = {0.306f, 0.788f, 0.690f}; // old #4EC9B0 accent
    const float*   all[]   = {t.TabAccent, t.TextAccent, t.CheckMark, t.SliderGrab, t.SliderGrabActive, t.PlotLines, t.InputFocusBorder, t.PanelFocusBorder, t.HeaderBg, t.HeaderHoveredBg, t.RowHoverBg, t.StatusBarBg, t.ButtonBg};
    for (const float* c : all)
    {
        const bool is_teal = (c[0] - teal[0]) * (c[0] - teal[0]) + (c[1] - teal[1]) * (c[1] - teal[1]) + (c[2] - teal[2]) * (c[2] - teal[2]) < 0.002f;
        EXPECT_FALSE(is_teal);
    }
}
