#include <Tetragrama/Gizmo/GizmoAppearance.h>
#include <Tetragrama/Gizmo/GizmoValuePill.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/UI/ZUIFont.h>
#include <ZEngine/UI/ZUIInteraction.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <gtest/gtest.h>
#include <limits>
#include <numbers>
#include <string_view>

using namespace Tetragrama::Gizmo;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::UI;

TEST(GizmoValuePillTest, LightFieldsReuseThemedPillLabelsAndApprovedUnitsWithoutAllocation)
{
    for (uint32_t index = uint32_t(Handle::LightRangePositiveX); index <= uint32_t(Handle::SpotOuterAngle); ++index)
    {
        ValuePillText text   = {};
        const auto    handle = Handle(index);
        ASSERT_TRUE(FormatLightPill(handle, handle == Handle::SpotOuterAngle ? 45.f : 6.f, text));
        EXPECT_STREQ(text.Label.data(), handle == Handle::SpotOuterAngle ? "Outer" : "Range");
        EXPECT_STREQ(text.Values.data(), handle == Handle::SpotOuterAngle ? "45.0" ZUI_GLYPH_DEGREE : "6.000");
        const auto color = ConstraintColor(handle);
        EXPECT_EQ(color.R, Tokens::PinVector.R);
        EXPECT_EQ(color.G, Tokens::PinVector.G);
        EXPECT_EQ(color.B, Tokens::PinVector.B);
    }
}

TEST(GizmoValuePillTest, InvalidLightFieldsPreserveOutputAndHugeRangesDoNotTruncate)
{
    ValuePillText text = {};
    ASSERT_TRUE(FormatLightPill(Handle::SpotRange, 6.f, text));
    for (float value : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        EXPECT_FALSE(FormatLightPill(Handle::SpotRange, value, text));
    }
    EXPECT_FALSE(FormatLightPill(Handle::SpotOuterAngle, 180.f, text));
    EXPECT_FALSE(FormatLightPill(Handle::TranslateX, 1.f, text));
    EXPECT_STREQ(text.Values.data(), "6.000");
    ASSERT_TRUE(FormatLightPill(Handle::SpotRange, std::numeric_limits<float>::max(), text));
    EXPECT_GT(std::string_view(text.Values.data()).size(), 40u);
    EXPECT_TRUE(std::string_view(text.Values.data()).ends_with(".000"));
}

TEST(GizmoValuePillTest, CameraFieldsUseBlueLabelsAndApprovedDistanceOrDegreeFormatting)
{
    for (const auto handle : {Handle::CameraFovUpperLeft, Handle::CameraFovUpperRight, Handle::CameraFarLowerLeft, Handle::CameraFarLowerRight, Handle::CameraNearLeft, Handle::CameraNearRight})
    {
        ValuePillText text  = {};
        const bool    angle = handle == Handle::CameraFovUpperLeft || handle == Handle::CameraFovUpperRight;
        const bool    near  = handle == Handle::CameraNearLeft || handle == Handle::CameraNearRight;
        ASSERT_TRUE(FormatCameraPill(handle, angle ? 60.f : 100.f, text));
        EXPECT_STREQ(text.Label.data(), angle ? "FOV" : near ? "Near" : "Far");
        EXPECT_STREQ(text.Values.data(), angle ? "60.0" ZUI_GLYPH_DEGREE : "100.000");
        const auto color = ConstraintColor(handle);
        EXPECT_EQ(color.R, Tokens::AccentBlue.R);
        EXPECT_EQ(color.G, Tokens::AccentBlue.G);
        EXPECT_EQ(color.B, Tokens::AccentBlue.B);
    }
}

TEST(GizmoValuePillTest, InvalidCameraTextPreservesThePreviousPillAndFarDoesNotTruncate)
{
    ValuePillText text = {};
    ASSERT_TRUE(FormatCameraPill(Handle::CameraFovUpperLeft, 60.f, text));
    for (float value : {0.f, -1.f, 180.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        EXPECT_FALSE(FormatCameraPill(Handle::CameraFovUpperLeft, value, text));
    }
    EXPECT_FALSE(FormatCameraPill(Handle::SpotRange, 1.f, text));
    EXPECT_STREQ(text.Values.data(), "60.0" ZUI_GLYPH_DEGREE);
    ASSERT_TRUE(FormatCameraPill(Handle::CameraFarLowerRight, std::numeric_limits<float>::max(), text));
    EXPECT_TRUE(std::string_view(text.Values.data()).ends_with(".000"));
}

TEST(GizmoValuePillTest, FormatsAllTranslationConstraintsInTheirLabeledOrderWithoutUnits)
{
    const Vec3f values(.25f, -.1f, 1.5f);
    struct Example
    {
        Handle      Constraint = Handle::None;
        const char* Label      = "";
        const char* Values     = "";
    };
    const Example examples[] = {
        {   Handle::TranslateX,   "X",                                 "+0.250"},
        {   Handle::TranslateY,   "Y",                  ZUI_GLYPH_MINUS "0.100"},
        {   Handle::TranslateZ,   "Z",                                 "+1.500"},
        {  Handle::TranslateXY,  "XY",        "+0.250 " ZUI_GLYPH_MINUS "0.100"},
        {  Handle::TranslateYZ,  "YZ",           ZUI_GLYPH_MINUS "0.100 +1.500"},
        {  Handle::TranslateZX,  "ZX",                          "+1.500 +0.250"},
        {Handle::TranslateView, "XYZ", "+0.250 " ZUI_GLYPH_MINUS "0.100 +1.500"}
    };
    for (const auto& example : examples)
    {
        ValuePillText output = {};
        ASSERT_TRUE(FormatTranslationPill(example.Constraint, values, output));
        EXPECT_STREQ(output.Label.data(), example.Label);
        EXPECT_STREQ(output.Values.data(), example.Values);
    }
}

TEST(GizmoValuePillTest, NegativeZeroRoundsToPositiveZeroAndFiniteFloatExtremaNeverTruncate)
{
    ValuePillText output = {};
    ASSERT_TRUE(FormatTranslationPill(Handle::TranslateView, {-0.f, -.0001f, .0001f}, output));
    EXPECT_STREQ(output.Values.data(), "+0.000 +0.000 +0.000");
    const float huge = std::numeric_limits<float>::max();
    ASSERT_TRUE(FormatTranslationPill(Handle::TranslateView, {-huge, huge, huge}, output));
    const std::string_view text(output.Values.data());
    EXPECT_LT(text.size(), output.Values.size());
    EXPECT_GT(text.size(), 120u);
    EXPECT_EQ(text.find('e'), std::string_view::npos);
    EXPECT_TRUE(text.ends_with(".000"));
}

TEST(GizmoValuePillTest, GridPillsUseUNVWithoutReorderingTheCapturedQuaternionCoordinates)
{
    const Handle translation[] = {Handle::TranslateX, Handle::TranslateY, Handle::TranslateZ, Handle::TranslateXY, Handle::TranslateYZ, Handle::TranslateZX, Handle::TranslateView};
    const char*  labels[]      = {"U", "N", "V", "UN", "NV", "VU", "UNV"};
    for (uint32_t i = 0; i < 7; ++i)
    {
        ValuePillText entity = {}, grid = {};
        ASSERT_TRUE(FormatTranslationPill(translation[i], {.25f, -.1f, 1.5f}, entity));
        ASSERT_TRUE(FormatTranslationPill(translation[i], {.25f, -.1f, 1.5f}, grid, AxisLabels::UNV));
        EXPECT_STREQ(grid.Label.data(), labels[i]);
        EXPECT_STREQ(grid.Values.data(), entity.Values.data());
    }
    const char* rings[] = {"U", "N", "V", "View"};
    for (uint32_t i = 0; i < 4; ++i)
    {
        ValuePillText text = {};
        ASSERT_TRUE(FormatRotationPill(Handle(uint32_t(Handle::RotateX) + i), std::numbers::pi / 4., text, AxisLabels::UNV));
        EXPECT_STREQ(text.Label.data(), rings[i]);
        EXPECT_STREQ(text.Values.data(), "45.0" ZUI_GLYPH_DEGREE);
    }
    ValuePillText preserved = {};
    ASSERT_TRUE(FormatTranslationPill(Handle::TranslateX, {}, preserved, AxisLabels::UNV));
    EXPECT_FALSE(FormatTranslationPill(Handle::TranslateX, {}, preserved, AxisLabels(99)));
    EXPECT_FALSE(FormatRotationPill(Handle::RotateX, 0., preserved, AxisLabels(99)));
    EXPECT_STREQ(preserved.Label.data(), "U");
}

TEST(GizmoValuePillTest, InvalidInputPreservesCallerText)
{
    ValuePillText output = {};
    ASSERT_TRUE(FormatTranslationPill(Handle::TranslateX, {.25f, 0.f, 0.f}, output));
    EXPECT_FALSE(FormatTranslationPill(Handle::None, {}, output));
    EXPECT_FALSE(FormatTranslationPill(Handle(999), {}, output));
    EXPECT_FALSE(FormatTranslationPill(Handle::TranslateX, {std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f}, output));
    EXPECT_STREQ(output.Label.data(), "X");
    EXPECT_STREQ(output.Values.data(), "+0.250");
}

TEST(GizmoValuePillTest, RotationUsesOneDecimalDegreesForEveryRingIncludingMultiTurnAndNegativeZero)
{
    for (const auto constraint : {Handle::RotateX, Handle::RotateY, Handle::RotateZ, Handle::RotateView})
    {
        ValuePillText text = {};
        ASSERT_TRUE(FormatRotationPill(constraint, std::numbers::pi / 4.0, text));
        EXPECT_STREQ(text.Label.data(), constraint == Handle::RotateView ? "View" : constraint == Handle::RotateX ? "X" : constraint == Handle::RotateY ? "Y" : "Z");
        EXPECT_STREQ(text.Values.data(), "45.0" ZUI_GLYPH_DEGREE);
        ASSERT_TRUE(FormatRotationPill(constraint, -2.5 * std::numbers::pi, text));
        EXPECT_STREQ(text.Values.data(), ZUI_GLYPH_MINUS "450.0" ZUI_GLYPH_DEGREE);
        ASSERT_TRUE(FormatRotationPill(constraint, -.00001, text));
        EXPECT_STREQ(text.Values.data(), "0.0" ZUI_GLYPH_DEGREE);
    }
}

TEST(GizmoValuePillTest, InvalidOrUnrepresentableRotationTextPreservesThePreviousValue)
{
    ValuePillText text = {};
    ASSERT_TRUE(FormatRotationPill(Handle::RotateZ, std::numbers::pi / 4.0, text));
    EXPECT_FALSE(FormatRotationPill(Handle::TranslateZ, 0.0, text));
    EXPECT_FALSE(FormatRotationPill(Handle::RotateZ, std::numeric_limits<double>::infinity(), text));
    EXPECT_FALSE(FormatRotationPill(Handle::RotateZ, std::numeric_limits<double>::max(), text));
    EXPECT_FALSE(FormatRotationPill(Handle::RotateZ, 1e200, text));
    EXPECT_STREQ(text.Label.data(), "Z");
    EXPECT_STREQ(text.Values.data(), "45.0" ZUI_GLYPH_DEGREE);
}

TEST(GizmoValuePillTest, ScaleRatiosUseTheAtlasMultiplicationGlyphAndTwoDecimalsForAllAxes)
{
    for (const auto handle : {Handle::ScaleX, Handle::ScaleY, Handle::ScaleZ, Handle::ScaleUniform})
    {
        ValuePillText text = {};
        ASSERT_TRUE(FormatScalePill(handle, 1.5f, text));
        EXPECT_STREQ(text.Label.data(), handle == Handle::ScaleUniform ? "XYZ" : handle == Handle::ScaleX ? "X" : handle == Handle::ScaleY ? "Y" : "Z");
        EXPECT_STREQ(text.Values.data(), ZUI_GLYPH_MULTIPLY "1.50");
        ASSERT_TRUE(FormatScalePill(handle, std::numeric_limits<float>::max(), text));
        EXPECT_TRUE(std::string_view(text.Values.data()).ends_with(".00"));
        EXPECT_LT(std::string_view(text.Values.data()).size(), text.Values.size());
    }
}

TEST(GizmoValuePillTest, InvalidScaleRatiosAndHandlesPreservePreviousText)
{
    ValuePillText text = {};
    ASSERT_TRUE(FormatScalePill(Handle::ScaleUniform, 1.25f, text));
    for (float invalid : {0.f, -1.f, .0001f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        EXPECT_FALSE(FormatScalePill(Handle::ScaleUniform, invalid, text));
    }
    EXPECT_FALSE(FormatScalePill(Handle::TranslateX, 1.f, text));
    EXPECT_FALSE(FormatScalePill(Handle(255), 1.f, text));
    EXPECT_STREQ(text.Label.data(), "XYZ");
    EXPECT_STREQ(text.Values.data(), ZUI_GLYPH_MULTIPLY "1.25");
}

class GizmoValuePillUITest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager Memory      = {};
    ZUIContext                           Context     = {};
    ZUIGlyph                             Glyphs[256] = {};
    ZUIFont                              Body = {}, Heading = {};
    ZUIFontAtlas                         Atlas    = {};
    Geometry::Viewport                   Viewport = {
        { 37.f,  61.f},
        {700.f, 500.f}
    };

    void SetUp() override
    {
        Memory.Initialize(ZMega(16), {});
        ZUIContextInit(&Context, &Memory.MainArena, ZMega(8), ZMega(1), 128, 128);
        for (auto& glyph : Glyphs)
        {
            glyph.AdvanceX = 6.f;
        }
        Body.Glyphs         = Glyphs;
        Body.GlyphCount     = 256;
        Body.FirstCodepoint = 0;
        Body.LineHeight     = 14.f;
        Body.FontSize       = 11.f;
        Heading             = Body;
        Heading.LineHeight  = 12.f;
        Atlas.Body          = &Body;
        Atlas.Heading       = &Heading;
        Context.Atlas       = &Atlas;
        Context.ScreenW     = 800;
        Context.ScreenH     = 600;
        BeginFrame();
    }
    void TearDown() override
    {
        ZUIContextDestroy(&Context);
    }
    void BeginFrame()
    {
        ZUIBeginFrame(&Context, 1.f / 60.f);
        auto* root    = ZUIPushBox(&Context, "root", 4, ZUI_None);
        root->Size[0] = ZPx(800);
        root->Size[1] = ZPx(600);
    }
    const ZUIBox* Find(const char* key)
    {
        const auto  hash = ZUIHashStr(key, uint32_t(ZEngine::Helpers::secure_strlen(key)));
        const auto* box  = Context.Root;
        while (box)
        {
            if (box->Key == hash)
            {
                return box;
            }
            if (box->FirstChild)
            {
                box = box->FirstChild;
            }
            else
            {
                while (box != Context.Root && !box->NextSib)
                {
                    box = box->Parent;
                }
                box = box == Context.Root ? nullptr : box->NextSib;
            }
        }
        return nullptr;
    }
};

TEST_F(GizmoValuePillUITest, UsesFigmaTokensCenteredTextAndDoesNotInterceptTheViewport)
{
    auto* base          = ZUIPushBox(&Context, "viewport", 8, ZUI_Clickable | ZUI_NoKeyboardFocus);
    base->Size[0]       = ZPx(800);
    base->Size[1]       = ZPx(600);
    const auto base_key = base->Key;
    ZUIPopBox(&Context);
    auto* saved = Context.Current;
    ASSERT_TRUE(DrawTranslationPill(&Context, Viewport, {100.f, 200.f}, Handle::TranslateX, {.25f, 0.f, 0.f}));
    EXPECT_EQ(Context.Current, saved);
    ZUIEndFrame(&Context);
    const auto* pill   = Find("##gizmo_value_pill");
    const auto* axis   = Find("##gizmo_value_axis");
    const auto* number = Find("##gizmo_value_number");
    ASSERT_NE(pill, nullptr);
    ASSERT_NE(axis, nullptr);
    ASSERT_NE(number, nullptr);
    EXPECT_EQ(pill->Parent, Context.ForegroundRoot);
    EXPECT_EQ(pill->ComputedSize[1], Metrics::HeightField);
    EXPECT_EQ(pill->ScreenMin[0], 116.f);
    EXPECT_EQ(pill->ScreenMin[1], 216.f);
    EXPECT_EQ(pill->CornerRadii[0], Metrics::Radius3);
    EXPECT_EQ(pill->BorderThickness, Constants::PillBorderWidthPx);
    EXPECT_EQ(pill->Colors[0][0], Tokens::BgWindow.R);
    EXPECT_EQ(pill->BorderColor[0], Tokens::BorderField.R);
    EXPECT_EQ(axis->TextColor[0], Tokens::AccentRed.R);
    EXPECT_EQ(number->TextColor[0], Tokens::TextPrimary.R);
    EXPECT_EQ(axis->FontSize, ZUIFontSize::Heading);
    EXPECT_EQ(number->FontSize, ZUIFontSize::Body);
    EXPECT_FLOAT_EQ((axis->ScreenMin[1] + axis->ScreenMax[1]) * .5f, (number->ScreenMin[1] + number->ScreenMax[1]) * .5f);
    const float point[2] = {pill->ScreenMin[0] + 1.f, pill->ScreenMin[1] + 1.f};
    const auto  query    = ZUIQueryPointer(&Context, point);
    ASSERT_NE(query.HotBox, nullptr);
    EXPECT_EQ(query.HotBox->Key, base_key);
    EXPECT_FALSE(pill->Flags & ZUI_Clickable);
}

TEST_F(GizmoValuePillUITest, RotationReusesTheNoninteractiveFieldAndApprovedAxisColors)
{
    for (const auto constraint : {Handle::RotateX, Handle::RotateY, Handle::RotateZ, Handle::RotateView})
    {
        BeginFrame();
        auto* saved = Context.Current;
        ASSERT_TRUE(DrawRotationPill(&Context, Viewport, {2000.f, 3000.f}, constraint, .25 * std::numbers::pi));
        EXPECT_EQ(Context.Current, saved);
        ZUIEndFrame(&Context);
        const auto* pill  = Find("##gizmo_value_pill");
        const auto* label = Find("##gizmo_value_axis");
        const auto* value = Find("##gizmo_value_number");
        ASSERT_NE(pill, nullptr);
        ASSERT_NE(label, nullptr);
        ASSERT_NE(value, nullptr);
        EXPECT_FALSE(pill->Flags & ZUI_Clickable);
        EXPECT_LE(pill->ScreenMax[0], Viewport.Origin.x + Viewport.Size.x);
        EXPECT_LE(pill->ScreenMax[1], Viewport.Origin.y + Viewport.Size.y);
        EXPECT_EQ(label->TextColor[0], ConstraintColor(constraint).R);
        EXPECT_EQ(label->TextColor[1], ConstraintColor(constraint).G);
        EXPECT_EQ(value->TextColor[2], Tokens::TextPrimary.B);
        EXPECT_FLOAT_EQ((label->ScreenMin[1] + label->ScreenMax[1]) * .5f, (value->ScreenMin[1] + value->ScreenMax[1]) * .5f);
    }
}

TEST_F(GizmoValuePillUITest, ScaleUsesTheSameClippedNoninteractiveFieldAndAxisColors)
{
    for (const auto handle : {Handle::ScaleX, Handle::ScaleY, Handle::ScaleZ, Handle::ScaleUniform})
    {
        BeginFrame();
        ASSERT_TRUE(DrawScalePill(&Context, Viewport, {2000.f, 3000.f}, handle, 1.25f));
        ZUIEndFrame(&Context);
        const auto* pill  = Find("##gizmo_value_pill");
        const auto* label = Find("##gizmo_value_axis");
        const auto* value = Find("##gizmo_value_number");
        ASSERT_NE(pill, nullptr);
        ASSERT_NE(label, nullptr);
        ASSERT_NE(value, nullptr);
        EXPECT_FALSE(pill->Flags & ZUI_Clickable);
        EXPECT_LE(pill->ScreenMax[0], Viewport.Origin.x + Viewport.Size.x);
        EXPECT_LE(pill->ScreenMax[1], Viewport.Origin.y + Viewport.Size.y);
        EXPECT_EQ(label->TextColor[0], ConstraintColor(handle).R);
        EXPECT_EQ(label->TextColor[1], ConstraintColor(handle).G);
        EXPECT_EQ(value->TextColor[2], Tokens::TextPrimary.B);
        EXPECT_FLOAT_EQ((label->ScreenMin[1] + label->ScreenMax[1]) * .5f, (value->ScreenMin[1] + value->ScreenMax[1]) * .5f);
        EXPECT_EQ(uint8_t(value->Label.Ptr[0]), uint8_t(ZUI_GLYPH_MULTIPLY[0]));
    }
}

TEST_F(GizmoValuePillUITest, PivotHintUsesApprovedTextThemeAndCenteredNoninteractiveField)
{
    auto* saved = Context.Current;
    ASSERT_TRUE(DrawPivotPill(&Context, Viewport, {100.f, 200.f}));
    EXPECT_EQ(Context.Current, saved);
    ZUIEndFrame(&Context);
    const auto* pill  = Find("##gizmo_value_pill");
    const auto* label = Find("##gizmo_value_axis");
    const auto* value = Find("##gizmo_value_number");
    ASSERT_NE(pill, nullptr);
    ASSERT_NE(label, nullptr);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(std::string_view(label->Label.Ptr, label->Label.Len), "Pivot");
    EXPECT_EQ(std::string_view(value->Label.Ptr, value->Label.Len), "Alt + drag");
    EXPECT_EQ(label->TextColor[0], Tokens::AccentOrange.R);
    EXPECT_EQ(label->TextColor[1], Tokens::AccentOrange.G);
    EXPECT_EQ(value->TextColor[2], Tokens::TextPrimary.B);
    EXPECT_EQ(label->FontSize, ZUIFontSize::Heading);
    EXPECT_EQ(value->FontSize, ZUIFontSize::Body);
    EXPECT_FLOAT_EQ((label->ScreenMin[1] + label->ScreenMax[1]) * .5f, (value->ScreenMin[1] + value->ScreenMax[1]) * .5f);
    EXPECT_FALSE(pill->Flags & ZUI_Clickable);
    EXPECT_EQ(pill->ComputedSize[1], Metrics::HeightField);
    EXPECT_EQ(pill->BorderColor[0], Tokens::BorderField.R);
}

TEST_F(GizmoValuePillUITest, ClampsEveryEdgeAndEllipsizesWithinTheViewport)
{
    for (const Vec2f pointer : {Vec2f(-100.f, -200.f), Vec2f(2000.f, 3000.f)})
    {
        BeginFrame();
        Viewport.Size.x  = 130.f;
        const float huge = std::numeric_limits<float>::max();
        ASSERT_TRUE(DrawTranslationPill(&Context, Viewport, pointer, Handle::TranslateView, {huge, huge, huge}));
        ZUIEndFrame(&Context);
        const auto* pill   = Find("##gizmo_value_pill");
        const auto* number = Find("##gizmo_value_number");
        ASSERT_NE(pill, nullptr);
        ASSERT_NE(number, nullptr);
        EXPECT_GE(pill->ScreenMin[0], Viewport.Origin.x);
        EXPECT_GE(pill->ScreenMin[1], Viewport.Origin.y);
        EXPECT_LE(pill->ScreenMax[0], Viewport.Origin.x + Viewport.Size.x);
        EXPECT_LE(pill->ScreenMax[1], Viewport.Origin.y + Viewport.Size.y);
        EXPECT_LT(number->Label.Len, 30u);
        EXPECT_LE(number->ScreenMax[0], pill->ScreenMax[0]);
    }
}

TEST_F(GizmoValuePillUITest, MalformedOrTinyViewportEmitsNothingAndReusesStorageAcrossFrames)
{
    EXPECT_FALSE(DrawTranslationPill(
        &Context,
        {
        {},
        {10.f, 10.f}
    },
        {},
        Handle::TranslateX,
        {}));
    EXPECT_FALSE(DrawTranslationPill(&Context, Viewport, {std::numeric_limits<float>::infinity(), 0.f}, Handle::TranslateX, {}));
    EXPECT_EQ(Find("##gizmo_value_pill"), nullptr);
    const auto persistent  = Memory.MainArena.m_current_offset;
    size_t     frame_bytes = 0;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        BeginFrame();
        ASSERT_TRUE(DrawTranslationPill(&Context, Viewport, {100.f, 200.f}, Handle::TranslateX, {.25f, 0.f, 0.f}));
        ZUIEndFrame(&Context);
        if (i == 0)
        {
            frame_bytes = Context.FrameArena.m_current_offset;
        }
        EXPECT_EQ(Context.FrameArena.m_current_offset, frame_bytes);
    }
    EXPECT_EQ(Memory.MainArena.m_current_offset, persistent);
    const auto folded = ZUIPushStr(&Context.FrameArena, "\xE2\x88\x92", 3);
    ASSERT_EQ(folded.Len, 1u);
    EXPECT_EQ(uint8_t(folded.Ptr[0]), uint8_t(ZUI_GLYPH_MINUS[0]));
}
