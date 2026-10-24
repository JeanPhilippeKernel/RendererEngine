// Portions ported from redlamp/color-taylor, revision
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

#include <ZEngine/UI/ZUIColorMath.h>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace ZEngine::UI::ColorMath
{
    namespace
    {
        constexpr double Pi     = std::numbers::pi;
        constexpr double Sqrt32 = 0.86602540378443864676;

        double           Clamp(double value, double maximum = 1.0) noexcept
        {
            return std::isnan(value) ? 0.0 : std::clamp(value, 0.0, maximum);
        }

        double Hue(double value) noexcept
        {
            if (!std::isfinite(value))
                return 0.0;
            const double wrapped = std::fmod(value, 360.0);
            return wrapped < 0.0 ? wrapped + 360.0 : wrapped;
        }

        int RoundedHue(double value) noexcept
        {
            return static_cast<int>(std::round(Hue(value))) % 360;
        }

        bool ValidGeometry(const HexGeometry& geometry) noexcept
        {
            return std::isfinite(geometry.Center.X) && std::isfinite(geometry.Center.Y) && std::isfinite(geometry.Radius) && geometry.Radius > 0.0;
        }

        RGB Channels(double hue, double chroma, double minimum) noexcept
        {
            const double x   = chroma * (1.0 - std::abs(std::fmod(hue / 60.0, 2.0) - 1.0));
            double       r   = 0.0;
            double       g   = 0.0;
            double       b   = 0.0;
            const int    arc = static_cast<int>(hue / 60.0);
            switch (arc)
            {
                case 0:
                    r = chroma;
                    g = x;
                    break;
                case 1:
                    r = x;
                    g = chroma;
                    break;
                case 2:
                    g = chroma;
                    b = x;
                    break;
                case 3:
                    g = x;
                    b = chroma;
                    break;
                case 4:
                    r = x;
                    b = chroma;
                    break;
                default:
                    r = chroma;
                    b = x;
                    break;
            }
            return {ToByte(r + minimum), ToByte(g + minimum), ToByte(b + minimum)};
        }

        struct Components
        {
            double Maximum = 0.0;
            double Minimum = 0.0;
            double Delta   = 0.0;
            int    H       = 0;
        };

        Components Decompose(RGB rgb, int achromatic_hue) noexcept
        {
            const double r      = std::clamp(rgb.R, 0, 255) / 255.0;
            const double g      = std::clamp(rgb.G, 0, 255) / 255.0;
            const double b      = std::clamp(rgb.B, 0, 255) / 255.0;
            Components   result = {};
            result.Maximum      = std::max({r, g, b});
            result.Minimum      = std::min({r, g, b});
            result.Delta        = result.Maximum - result.Minimum;
            if (result.Delta == 0.0)
                result.H = RoundedHue(achromatic_hue);
            else
            {
                const double h = result.Maximum == r ? std::fmod((g - b) / result.Delta, 6.0) : result.Maximum == g ? (b - r) / result.Delta + 2.0 : (r - g) / result.Delta + 4.0;
                result.H       = RoundedHue(h * 60.0);
            }
            return result;
        }

        int HexDigit(char c) noexcept
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        }
    } // namespace

    double LinearToSrgb(double linear) noexcept
    {
        const double x = Clamp(linear);
        return x <= 0.0031308 ? x * 12.92 : 1.055 * std::pow(x, 1.0 / 2.4) - 0.055;
    }

    double SrgbToLinear(double srgb) noexcept
    {
        const double x = Clamp(srgb);
        return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
    }

    double ToDisplay(double stored, ZUIColorSpace space) noexcept
    {
        return space == ZUIColorSpace::Linear ? LinearToSrgb(stored) : Clamp(stored);
    }

    double FromDisplay(double srgb, ZUIColorSpace space) noexcept
    {
        return space == ZUIColorSpace::Linear ? SrgbToLinear(srgb) : Clamp(srgb);
    }

    int ToByte(double normalized) noexcept
    {
        return static_cast<int>(std::round(Clamp(normalized) * 255.0));
    }

    int ToByte(float normalized) noexcept
    {
        return static_cast<int>(std::round(static_cast<float>(Clamp(normalized)) * 255.0f));
    }

    RGB HsbToRgb(double hue, double saturation, double brightness) noexcept
    {
        const double b = Clamp(brightness, 100.0) / 100.0;
        const double c = b * Clamp(saturation, 100.0) / 100.0;
        return Channels(Hue(hue), c, b - c);
    }

    HSB RgbToHsb(RGB rgb, int achromatic_hue) noexcept
    {
        const auto c = Decompose(rgb, achromatic_hue);
        return {c.H, static_cast<int>(std::round(c.Maximum == 0.0 ? 0.0 : c.Delta / c.Maximum * 100.0)), static_cast<int>(std::round(c.Maximum * 100.0))};
    }

    RGB HslToRgb(double hue, double saturation, double lightness) noexcept
    {
        const double l = Clamp(lightness, 100.0) / 100.0;
        const double c = (1.0 - std::abs(2.0 * l - 1.0)) * Clamp(saturation, 100.0) / 100.0;
        return Channels(Hue(hue), c, l - c / 2.0);
    }

    HSL RgbToHsl(RGB rgb, int achromatic_hue) noexcept
    {
        const auto   c = Decompose(rgb, achromatic_hue);
        const double l = (c.Maximum + c.Minimum) / 2.0;
        const double s = c.Delta == 0.0 ? 0.0 : c.Delta / (1.0 - std::abs(2.0 * l - 1.0));
        return {c.H, static_cast<int>(std::round(s * 100.0)), static_cast<int>(std::round(l * 100.0))};
    }

    double HexEdgeDist(double angle, double radius) noexcept
    {
        if (!std::isfinite(angle) || !std::isfinite(radius) || radius <= 0.0)
            return 0.0;
        const double wrapped = std::fmod(std::fmod(angle, 2.0 * Pi) + 2.0 * Pi, 2.0 * Pi);
        const double sector  = std::fmod(wrapped, Pi / 3.0);
        return radius * Sqrt32 / std::cos(sector - Pi / 6.0);
    }

    double LimitScale(FieldMode mode, double brightness, double lightness) noexcept
    {
        return mode == FieldMode::Brightness ? Clamp(brightness, 100.0) / 100.0 : 1.0 - std::abs(2.0 * Clamp(lightness, 100.0) / 100.0 - 1.0);
    }

    RGB ColorAtPoint(Point point, double brightness, double lightness, FieldMode mode, const HexGeometry& geometry) noexcept
    {
        if (!ValidGeometry(geometry))
            return {};
        const double dx    = point.X - geometry.Center.X;
        const double dy    = point.Y - geometry.Center.Y;
        const double dist  = std::hypot(dx, dy);
        const double angle = std::atan2(-dy, dx);
        const double edge  = HexEdgeDist(angle, geometry.Radius);
        if (!std::isfinite(dist) || edge <= 0.0)
            return {};
        const double h     = Hue(angle * 180.0 / Pi);
        const double limit = edge * LimitScale(mode, brightness, lightness);
        const double s     = limit > 0.0 ? Clamp(dist / limit) * 100.0 : 0.0;
        const double r     = Clamp(dist / edge);
        if (mode == FieldMode::Brightness)
            return dist <= limit ? HsbToRgb(h, s, brightness) : HsbToRgb(h, 100.0, r * 100.0);
        const double l_out = lightness <= 50.0 ? r * 50.0 : 100.0 - r * 50.0;
        return dist <= limit ? HslToRgb(h, s, lightness) : HslToRgb(h, 100.0, l_out);
    }

    std::optional<HSB> HsbFromField(Point point, const FieldInput& input, const HexGeometry& geometry) noexcept
    {
        if (!ValidGeometry(geometry))
            return {};
        const double dx    = point.X - geometry.Center.X;
        const double dy    = point.Y - geometry.Center.Y;
        const double dist  = std::hypot(dx, dy);
        const double angle = std::atan2(-dy, dx);
        const double edge  = HexEdgeDist(angle, geometry.Radius);
        if (!std::isfinite(dist) || edge <= 0.0 || (!input.ClampOnly && dist > edge))
            return {};
        const auto   origin    = input.Origin.value_or(FieldOrigin{input.Brightness, input.Lightness, input.Hue, 0.0});
        const double limit     = LimitScale(input.Mode, origin.Brightness, origin.Lightness);
        const double r         = dist / edge;
        const double s         = r <= limit ? (limit > 0.0 ? Clamp(r / limit) * 100.0 : 0.0) : 100.0;
        const int    rounded_s = static_cast<int>(std::round(s));
        const bool   gray      = input.Mode == FieldMode::Brightness ? rounded_s == 0 : s == 0.0;
        const int    h         = RoundedHue(gray ? origin.Hue : angle * 180.0 / Pi);
        if (input.Mode == FieldMode::Brightness)
            return HSB{h, rounded_s, static_cast<int>(std::round(r <= limit ? Clamp(origin.Brightness, 100.0) : Clamp(r) * 100.0))};
        const double pinned = Clamp(r);
        const double l      = r <= limit ? origin.Lightness : origin.Lightness <= 50.0 ? pinned * 50.0 : 100.0 - pinned * 50.0;
        auto         result = RgbToHsb(HslToRgb(h, s, l), h);
        result.H            = h;
        return result;
    }

    Point PointForColor(RGB rgb, FieldMode mode, const HexGeometry& geometry) noexcept
    {
        if (!ValidGeometry(geometry))
            return {};
        const auto   hsb   = RgbToHsb(rgb);
        const auto   hsl   = RgbToHsl(rgb);
        const double angle = hsb.H * Pi / 180.0;
        const double s     = (mode == FieldMode::Brightness ? hsb.S : hsl.S) / 100.0;
        const double dist  = s * HexEdgeDist(angle, geometry.Radius) * LimitScale(mode, hsb.B, hsl.L);
        return {geometry.Center.X + dist * std::cos(angle), geometry.Center.Y - dist * std::sin(angle)};
    }

    std::array<Point, 4> BuildChain(RGB rgb, const HexGeometry& geometry) noexcept
    {
        if (!ValidGeometry(geometry))
            return {};
        const double scale = geometry.Radius / 255.0;
        const double r     = std::clamp(rgb.R, 0, 255) * scale;
        const double g     = std::clamp(rgb.G, 0, 255) * scale;
        const double b     = std::clamp(rgb.B, 0, 255) * scale;
        const Point  p1    = {geometry.Center.X + r, geometry.Center.Y};
        const Point  p2    = {p1.X - g * 0.5, p1.Y - g * Sqrt32};
        return {
            geometry.Center, p1, p2, Point{p2.X - b * 0.5, p2.Y + b * Sqrt32}
        };
    }

    std::optional<RGBA> ParseHex(std::string_view text) noexcept
    {
        if (!text.empty() && text.front() == '#')
            text.remove_prefix(1);
        if (text.size() != 3 && text.size() != 6 && text.size() != 8)
            return {};
        int        channels[4] = {0, 0, 0, 255};
        const bool short_form  = text.size() == 3;
        const auto count       = short_form ? 3 : text.size() / 2;
        for (size_t i = 0; i < count; ++i)
        {
            const int high = HexDigit(text[short_form ? i : i * 2]);
            const int low  = short_form ? high : HexDigit(text[i * 2 + 1]);
            if (high < 0 || low < 0)
                return {};
            channels[i] = high * 16 + low;
        }
        return RGBA{channels[0], channels[1], channels[2], channels[3]};
    }

    bool FormatHex(RGBA color, char* destination, size_t capacity, bool force_alpha) noexcept
    {
        if (!destination || !capacity)
            return false;
        destination[0]  = '\0';
        const int count = force_alpha || std::clamp(color.A, 0, 255) < 255 ? 4 : 3;
        if (capacity < static_cast<size_t>(count * 2 + 2))
            return false;
        constexpr char digits[]    = "0123456789ABCDEF";
        const int      channels[4] = {color.R, color.G, color.B, color.A};
        destination[0]             = '#';
        for (int i = 0; i < count; ++i)
        {
            const int channel      = std::clamp(channels[i], 0, 255);
            destination[1 + i * 2] = digits[channel >> 4];
            destination[2 + i * 2] = digits[channel & 15];
        }
        destination[count * 2 + 1] = '\0';
        return true;
    }
} // namespace ZEngine::UI::ColorMath
