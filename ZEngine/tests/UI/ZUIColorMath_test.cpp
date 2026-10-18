// Hexagon, pointer and chain regression cases adapted from redlamp/color-taylor
// b0a895aa80a36adf75e7c9555cd331587cbb34f8, under the MIT License.
// Copyright (c) 2026 Taylor Wright
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIColorMath.h>
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <numbers>

using namespace ZEngine::UI;
using namespace ZEngine::UI::ColorMath;

namespace
{
    constexpr double      Pi        = std::numbers::pi;
    constexpr HexGeometry Reference = {
        {260.0, 270.0},
        210.0
    };

    Point At(double angle_degrees, double fraction, const HexGeometry& geometry = Reference)
    {
        const double angle = angle_degrees * Pi / 180.0;
        const double edge  = HexEdgeDist(angle, geometry.Radius);
        return {geometry.Center.X + fraction * edge * std::cos(angle), geometry.Center.Y - fraction * edge * std::sin(angle)};
    }

    double Distance(Point point, const HexGeometry& geometry = Reference)
    {
        return std::hypot(point.X - geometry.Center.X, point.Y - geometry.Center.Y);
    }

    FieldInput BaseInput()
    {
        FieldInput input = {};
        input.Hue        = 200.0;
        return input;
    }
} // namespace

TEST(ZUIColorMathTest, ExactSrgbTransferMatchesEighteenPercentGrayAndEndpoints)
{
    EXPECT_EQ(ToByte(LinearToSrgb(0.18)), 118);
    EXPECT_EQ(ToByte(LinearToSrgb(0.0)), 0);
    EXPECT_EQ(ToByte(LinearToSrgb(1.0)), 255);
    EXPECT_NEAR(SrgbToLinear(118.0 / 255.0), 0.18, 1.0 / 255.0);
    EXPECT_DOUBLE_EQ(SrgbToLinear(0.0), 0.0);
    EXPECT_DOUBLE_EQ(SrgbToLinear(1.0), 1.0);
    EXPECT_NEAR(SrgbToLinear(0.04045), 0.04045 / 12.92, 1.0e-12);
    EXPECT_NEAR(LinearToSrgb(0.0031308), 0.0031308 * 12.92, 1.0e-12);
    for (int byte = 0; byte <= 255; ++byte)
        EXPECT_EQ(ToByte(LinearToSrgb(SrgbToLinear(byte / 255.0))), byte);
    for (int sample = 0; sample <= 1000; ++sample)
    {
        const double value = sample / 1000.0;
        EXPECT_NEAR(FromDisplay(ToDisplay(value, ZUIColorSpace::Linear), ZUIColorSpace::Linear), value, 1.0e-12);
        EXPECT_DOUBLE_EQ(ToDisplay(value, ZUIColorSpace::Srgb), value);
        EXPECT_DOUBLE_EQ(FromDisplay(value, ZUIColorSpace::Srgb), value);
    }
}

TEST(ZUIColorMathTest, FloatStorageKeepsExistingGridByteRounding)
{
    EXPECT_EQ(ToByte(0.6f), 153);
    EXPECT_EQ(ToByte(0.3f), 77);
    EXPECT_EQ(ToByte(0.9f), 230);
    EXPECT_EQ(ToByte(0.2f), 51);
    EXPECT_EQ(ToByte(0.4f), 102);
    EXPECT_EQ(ToByte(1.0f), 255);
    for (int byte = 0; byte <= 255; ++byte)
        EXPECT_EQ(ToByte(byte / 255.0f), byte);
    EXPECT_EQ(ToByte(std::numeric_limits<float>::quiet_NaN()), 0);
    EXPECT_EQ(ToByte(std::numeric_limits<float>::infinity()), 255);
}

TEST(ZUIColorMathTest, ConversionMatchesSixVerticesAndFigmaAccent)
{
    constexpr RGB colors[6] = {
        {255,   0,   0},
        {255, 255,   0},
        {  0, 255,   0},
        {  0, 255, 255},
        {  0,   0, 255},
        {255,   0, 255}
    };
    for (int k = 0; k < 6; ++k)
    {
        EXPECT_EQ(HsbToRgb(k * 60.0, 100.0, 100.0), colors[k]);
        EXPECT_EQ(HslToRgb(k * 60.0, 100.0, 50.0), colors[k]);
        EXPECT_EQ(RgbToHsb(colors[k]), (HSB{k * 60, 100, 100}));
        EXPECT_EQ(RgbToHsl(colors[k]), (HSL{k * 60, 100, 50}));
    }
    EXPECT_EQ(RgbToHsb({87, 157, 227}), (HSB{210, 62, 89}));
    EXPECT_EQ(RgbToHsl({87, 157, 227}), (HSL{210, 71, 62}));
    EXPECT_EQ(HsbToRgb(360.0, 100.0, 100.0), colors[0]);
    EXPECT_EQ(HsbToRgb(-60.0, 100.0, 100.0), colors[5]);
    EXPECT_EQ(HslToRgb(780.0, 100.0, 50.0), colors[1]);
    EXPECT_EQ(RgbToHsb({128, 128, 128}, 210), (HSB{210, 0, 50}));
    EXPECT_EQ(RgbToHsl({0, 0, 0}, 210), (HSL{210, 0, 0}));
    EXPECT_EQ(HslToRgb(200.0, 0.0, 30.0), (RGB{77, 77, 77}));
}

TEST(ZUIColorMathTest, HexEdgeHasTheReferenceVerticesMidpointsAndSixfoldSymmetry)
{
    const double radius = Reference.Radius;
    for (int k = 0; k < 6; ++k)
    {
        EXPECT_NEAR(HexEdgeDist(k * Pi / 3.0, radius), radius, 1.0e-9);
        EXPECT_NEAR(HexEdgeDist(Pi / 6.0 + k * Pi / 3.0, radius), radius * std::sqrt(3.0) / 2.0, 1.0e-9);
    }
    for (int i = 0; i < 60; ++i)
    {
        const double angle = i / 60.0 * 2.0 * Pi;
        EXPECT_NEAR(HexEdgeDist(angle, radius), HexEdgeDist(angle + Pi / 3.0, radius), 1.0e-9);
        EXPECT_NEAR(HexEdgeDist(angle, radius), HexEdgeDist(angle - 2.0 * Pi, radius), 1.0e-9);
    }
    EXPECT_NEAR(HexEdgeDist(0.0, radius) / HexEdgeDist(Pi / 6.0, radius), 2.0 / std::sqrt(3.0), 1.0e-9);
}

TEST(ZUIColorMathTest, CrossSectionBoundMatchesReference)
{
    EXPECT_DOUBLE_EQ(LimitScale(FieldMode::Brightness, 40, 0), 0.4);
    EXPECT_DOUBLE_EQ(LimitScale(FieldMode::Lightness, 0, 50), 1.0);
    EXPECT_DOUBLE_EQ(LimitScale(FieldMode::Lightness, 0, 25), 0.5);
    EXPECT_DOUBLE_EQ(LimitScale(FieldMode::Lightness, 0, 75), 0.5);
    EXPECT_DOUBLE_EQ(LimitScale(FieldMode::Lightness, 0, 100), 0.0);
}

TEST(ZUIColorMathTest, FieldColorsMatchReferenceCenterVerticesAndCrossSection)
{
    EXPECT_EQ(ColorAtPoint(Reference.Center, 100, 50, FieldMode::Brightness, Reference), (RGB{255, 255, 255}));
    EXPECT_EQ(ColorAtPoint(Reference.Center, 0, 50, FieldMode::Brightness, Reference), (RGB{}));
    constexpr RGB colors[6] = {
        {255,   0,   0},
        {255, 255,   0},
        {  0, 255,   0},
        {  0, 255, 255},
        {  0,   0, 255},
        {255,   0, 255}
    };
    for (int k = 0; k < 6; ++k)
        EXPECT_EQ(ColorAtPoint(At(k * 60.0, 0.999), 100, 50, FieldMode::Brightness, Reference), colors[k]);
    EXPECT_EQ(ColorAtPoint(At(0, 0.5 * 0.999), 50, 50, FieldMode::Brightness, Reference), (RGB{128, 0, 0}));
    EXPECT_EQ(ColorAtPoint(At(0, 0.999), 50, 50, FieldMode::Brightness, Reference), (RGB{255, 0, 0}));
    EXPECT_EQ(ColorAtPoint(At(0, 0.75), 100, 25, FieldMode::Lightness, Reference), (RGB{191, 0, 0}));
    EXPECT_EQ(ColorAtPoint(At(0, 0.75), 100, 75, FieldMode::Lightness, Reference), (RGB{255, 64, 64}));
    for (double lightness : {0.0, 100.0})
        EXPECT_EQ(ColorAtPoint(At(0, 1.4), 100, lightness, FieldMode::Lightness, Reference), (RGB{255, 0, 0}));
}

TEST(ZUIColorMathTest, BrightnessPointerMatchesReferenceAndPreservesAchromaticHue)
{
    auto input = BaseInput();
    EXPECT_EQ(HsbFromField(At(0, 0.999), input, Reference), (HSB{0, 100, 100}));
    EXPECT_EQ(HsbFromField(Reference.Center, input, Reference), (HSB{200, 0, 100}));
    EXPECT_EQ(HsbFromField(At(90, 0.0001), input, Reference), (HSB{200, 0, 100}));
    input.Brightness = 40.0;
    EXPECT_EQ(HsbFromField(At(0, 0.2), input, Reference), (HSB{0, 50, 40}));
    EXPECT_EQ(HsbFromField(At(120, 0.75), input, Reference), (HSB{120, 100, 75}));
    EXPECT_FALSE(HsbFromField(At(30, 1.2), input, Reference).has_value());
    input.ClampOnly = true;
    EXPECT_EQ(HsbFromField(At(30, 1.2), input, Reference), (HSB{30, 100, 100}));
}

TEST(ZUIColorMathTest, FrozenGestureOriginRestoresOriginalBrightnessInsteadOfRatcheting)
{
    auto input       = BaseInput();
    input.Origin     = FieldOrigin{40, 50, 0, 0};
    input.Brightness = 90;
    EXPECT_EQ(HsbFromField(At(0, 0.9), input, Reference), (HSB{0, 100, 90}));
    EXPECT_EQ(HsbFromField(At(0, 0.2), input, Reference), (HSB{0, 50, 40}));
    EXPECT_EQ(HsbFromField(Reference.Center, input, Reference), (HSB{0, 0, 40}));
}

TEST(ZUIColorMathTest, LightnessPointerMatchesReferenceAndClampsAtTheWidestCrossSection)
{
    auto input = BaseInput();
    input.Mode = FieldMode::Lightness;
    EXPECT_EQ(HsbFromField(At(240, 0.999), input, Reference), (HSB{240, 100, 100}));
    input.Lightness = 25;
    EXPECT_EQ(HsbFromField(At(0, 0.75), input, Reference), (HSB{0, 100, 75}));
    input.Lightness = 75;
    EXPECT_EQ(HsbFromField(At(0, 0.75), input, Reference), (HSB{0, 75, 100}));
    input.Lightness = 25;
    input.ClampOnly = true;
    EXPECT_EQ(HsbFromField(At(0, 1.4), input, Reference), (HSB{0, 100, 100}));
    input.Lightness = 75;
    EXPECT_EQ(HsbFromField(At(0, 1.4), input, Reference), (HSB{0, 100, 100}));
    input.Lightness = 30;
    EXPECT_EQ(HsbFromField(Reference.Center, input, Reference), (HSB{200, 0, 30}));
    input.Origin    = FieldOrigin{100, 25, 200, 50};
    input.Lightness = 90;
    EXPECT_EQ(HsbFromField(At(0, 0.75), input, Reference), (HSB{0, 100, 75}));
}

TEST(ZUIColorMathTest, FieldPointIsTheInverseOfTheFieldWithinReferenceIntegerRounding)
{
    constexpr RGB colors[] = {
        {199,  21, 133},
        {100, 149, 237},
        {  0,   0,   0},
        {255, 255, 255},
        {128, 128, 128}
    };
    for (const auto mode : {FieldMode::Brightness, FieldMode::Lightness})
        for (const auto rgb : colors)
        {
            const auto hsb  = RgbToHsb(rgb);
            const auto hsl  = RgbToHsl(rgb);
            const auto p    = PointForColor(rgb, mode, Reference);
            const auto back = ColorAtPoint(p, hsb.B, hsl.L, mode, Reference);
            if (mode == FieldMode::Brightness)
            {
                const auto converted = RgbToHsb(back);
                EXPECT_NEAR(converted.S, hsb.S, 0.5);
                if (hsb.S > 0)
                    EXPECT_NEAR(converted.H, hsb.H, 0.5);
            }
            else
            {
                const auto converted = RgbToHsl(back);
                EXPECT_NEAR(converted.S, hsl.S, 0.5);
                if (hsl.S > 0)
                    EXPECT_NEAR(converted.H, hsl.H, 0.5);
            }
        }
}

TEST(ZUIColorMathTest, ChainMatchesReferenceVerticesAndEqualChannelCancellation)
{
    const auto chain = BuildChain({10, 20, 30}, Reference);
    EXPECT_EQ(chain.size(), 4u);
    EXPECT_EQ(chain[0], Reference.Center);
    const auto red = BuildChain({255, 0, 0}, Reference);
    EXPECT_NEAR(red[3].X, Reference.Center.X + Reference.Radius, 1.0e-9);
    EXPECT_NEAR(red[3].Y, Reference.Center.Y, 1.0e-9);
    const auto yellow = BuildChain({255, 255, 0}, Reference);
    EXPECT_NEAR(Distance(yellow[3]), Reference.Radius, 1.0e-9);
    EXPECT_NEAR(std::atan2(-(yellow[3].Y - Reference.Center.Y), yellow[3].X - Reference.Center.X), Pi / 3.0, 1.0e-9);
    EXPECT_NEAR(Distance(BuildChain({128, 128, 128}, Reference)[3]), 0.0, 1.0e-9);
    // Reference's rounded HSB inputs, with the same one-unit/degree tolerances.
    for (const HSB hsb : {
         HSB{205,  88, 92},
         HSB{ 30,  40, 60},
         HSB{300, 100, 25}
    })
    {
        const auto   rgb      = HsbToRgb(hsb.H, hsb.S, hsb.B);
        const auto   back     = RgbToHsb(rgb);
        const auto   tip      = BuildChain(rgb, Reference)[3];
        const double expected = back.S / 100.0 * back.B / 100.0 * HexEdgeDist(back.H * Pi / 180.0, Reference.Radius);
        EXPECT_LT(std::abs(Distance(tip) - expected), 1.0);
        const double angle = std::atan2(-(tip.Y - Reference.Center.Y), tip.X - Reference.Center.X) * 180.0 / Pi;
        EXPECT_LT(std::abs(std::remainder(angle - back.H, 360.0)), 1.0);
    }
}

TEST(ZUIColorMathTest, FigmaChainUsesNinetySixPixelRadiusAndExactRgbBytes)
{
    const auto chain = BuildChain({87, 157, 227});
    EXPECT_EQ(chain[0], (Point{120.0, 116.0}));
    EXPECT_NEAR(chain[1].X, 152.75, 0.01);
    EXPECT_NEAR(chain[1].Y, 116.0, 0.01);
    EXPECT_NEAR(chain[2].X, 123.2, 0.01);
    EXPECT_NEAR(chain[2].Y, 64.81, 0.01);
    EXPECT_NEAR(chain[3].X, 80.47, 0.01);
    EXPECT_NEAR(chain[3].Y, 138.82, 0.01);
}

TEST(ZUIColorMathTest, HexAcceptsOnlyCompleteApprovedForms)
{
    for (const auto text : {"#579DE3", "579de3", "579DE3FF"})
        EXPECT_EQ(ParseHex(text), (RGBA{87, 157, 227, 255}));
    EXPECT_EQ(ParseHex("#abc"), (RGBA{170, 187, 204, 255}));
    EXPECT_EQ(ParseHex("AbC"), (RGBA{170, 187, 204, 255}));
    EXPECT_EQ(ParseHex("#579de380"), (RGBA{87, 157, 227, 128}));
    EXPECT_EQ(ParseHex("00000000"), (RGBA{0, 0, 0, 0}));
    for (const auto text : {"", "#", "#ab", "#abcd", "#12345", "#1234567", "#123456789", "#12345G", "123456junk", "##123456", " 123456", "123456 ", "+12345", "0x123456"})
        EXPECT_FALSE(ParseHex(text).has_value()) << text;
    EXPECT_FALSE(ParseHex(std::string_view("123\0FF", 6)).has_value());
}

TEST(ZUIColorMathTest, HexFormattingIsUppercaseBoundedAndConsistentWithBytes)
{
    char text[10] = {};
    ASSERT_TRUE(FormatHex({87, 157, 227, 255}, text, sizeof(text)));
    EXPECT_STREQ(text, "#579DE3");
    ASSERT_TRUE(FormatHex({87, 157, 227, 128}, text, sizeof(text)));
    EXPECT_STREQ(text, "#579DE380");
    EXPECT_EQ(ParseHex(text), (RGBA{87, 157, 227, 128}));
    ASSERT_TRUE(FormatHex({87, 157, 227, 255}, text, sizeof(text), true));
    EXPECT_STREQ(text, "#579DE3FF");
    ASSERT_TRUE(FormatHex({-1, 256, 0, 999}, text, sizeof(text)));
    EXPECT_STREQ(text, "#00FF00");
    EXPECT_FALSE(FormatHex({}, nullptr, 10));
    for (size_t capacity = 0; capacity < 10; ++capacity)
    {
        ZEngine::Helpers::secure_memset(text, '.', sizeof(text), sizeof(text));
        EXPECT_FALSE(FormatHex({87, 157, 227, 128}, text, capacity));
        EXPECT_EQ(text[capacity], '.');
        if (capacity)
            EXPECT_EQ(text[0], '\0');
    }
    for (int byte = 0; byte <= 255; ++byte)
    {
        const RGBA color = {byte, 255 - byte, byte, byte};
        ASSERT_TRUE(FormatHex(color, text, sizeof(text)));
        EXPECT_EQ(ParseHex(text), color);
    }
}

TEST(ZUIColorMathTest, MalformedValuesAreClampedWithoutUndefinedIntegerCasts)
{
    const double nan      = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (double value : {nan, -infinity, -1.0})
    {
        EXPECT_EQ(ToByte(value), 0);
        EXPECT_DOUBLE_EQ(LinearToSrgb(value), 0.0);
        EXPECT_DOUBLE_EQ(SrgbToLinear(value), 0.0);
    }
    EXPECT_EQ(ToByte(infinity), 255);
    EXPECT_NEAR(LinearToSrgb(infinity), 1.0, 1.0e-12);
    EXPECT_EQ(HsbToRgb(nan, infinity, infinity), (RGB{255, 0, 0}));
    EXPECT_EQ(RgbToHsb({-100, 900, 0}), (HSB{120, 100, 100}));
    EXPECT_DOUBLE_EQ(HexEdgeDist(nan, 96), 0.0);
    EXPECT_DOUBLE_EQ(HexEdgeDist(0, -1), 0.0);
    EXPECT_FALSE(HsbFromField({nan, 0}, BaseInput()).has_value());
    EXPECT_FALSE(HsbFromField({infinity, 0}, BaseInput()).has_value());
    EXPECT_EQ(ColorAtPoint({nan, 0}, 100), (RGB{}));
    const HexGeometry invalid = {
        {0, 0},
        0
    };
    EXPECT_FALSE(HsbFromField({}, BaseInput(), invalid).has_value());
    EXPECT_EQ(ColorAtPoint({}, 100, 50, FieldMode::Brightness, invalid), (RGB{}));
    EXPECT_EQ(BuildChain({255, 255, 255}, invalid), (std::array<Point, 4>{}));
}
