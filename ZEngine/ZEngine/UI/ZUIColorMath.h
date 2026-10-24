#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ZEngine::UI
{
    /// Storage encoding, not a tone-mapping or exposure operation. Alpha is linear
    /// coverage in either case and must never pass through the RGB transfer curve.
    enum class ZUIColorSpace : uint8_t
    {
        Srgb,
        Linear
    };
} // namespace ZEngine::UI

namespace ZEngine::UI::ColorMath
{
    // Integer state deliberately follows color-taylor: RGB bytes, whole degrees
    // and whole percentages. Keep the RGB override when exact typed bytes matter;
    // converting through integer HSB/HSL is intentionally not lossless.
    struct RGB
    {
        int  R                            = 0;
        int  G                            = 0;
        int  B                            = 0;
        bool operator==(const RGB&) const = default;
    };

    struct RGBA
    {
        int  R                             = 0;
        int  G                             = 0;
        int  B                             = 0;
        int  A                             = 255;
        bool operator==(const RGBA&) const = default;
    };

    struct HSB
    {
        int  H                            = 0;
        int  S                            = 0;
        int  B                            = 0;
        bool operator==(const HSB&) const = default;
    };

    struct HSL
    {
        int  H                            = 0;
        int  S                            = 0;
        int  L                            = 0;
        bool operator==(const HSL&) const = default;
    };

    struct Point
    {
        double X                              = 0.0;
        double Y                              = 0.0;
        bool   operator==(const Point&) const = default;
    };

    /// Figma's 240 x 232 field, not the reference app's 540-unit stage.
    struct HexGeometry
    {
        Point  Center = {120.0, 116.0};
        double Radius = 96.0;
    };

    enum class FieldMode : uint8_t
    {
        Brightness,
        Lightness
    };

    /// Freeze at pointer-down; reusing the live color here would ratchet the
    /// brightness/lightness instead of restoring it on an inward drag.
    struct FieldOrigin
    {
        double Brightness    = 100.0;
        double Lightness     = 50.0;
        double Hue           = 0.0;
        double HslSaturation = 0.0;
    };

    struct FieldInput
    {
        double                     Brightness = 100.0;
        double                     Lightness  = 50.0;
        double                     Hue        = 0.0;
        FieldMode                  Mode       = FieldMode::Brightness;
        std::optional<FieldOrigin> Origin     = {};
        bool                       ClampOnly  = false;
    };

    /// Exact IEC sRGB transfer, normalized [0,1]. This is deliberately not the
    /// renderer's ACES + pow(1/2.2) presentation transform. Out-of-gamut values
    /// clamp for display; NaN is treated as zero. Neither function quantizes.
    double               LinearToSrgb(double linear) noexcept;
    double               SrgbToLinear(double srgb) noexcept;
    double               ToDisplay(double stored, ZUIColorSpace space) noexcept;
    double               FromDisplay(double srgb, ZUIColorSpace space) noexcept;
    int                  ToByte(double normalized) noexcept;
    /// Keep float-storage half-byte rounding in float precision (0.9f -> 230).
    int                  ToByte(float normalized) noexcept;

    RGB                  HsbToRgb(double hue, double saturation, double brightness) noexcept;
    HSB                  RgbToHsb(RGB rgb, int achromatic_hue = 0) noexcept;
    RGB                  HslToRgb(double hue, double saturation, double lightness) noexcept;
    HSL                  RgbToHsl(RGB rgb, int achromatic_hue = 0) noexcept;

    /// Angle is radians; screen Y points down. Invalid radius returns zero.
    double               HexEdgeDist(double angle, double radius) noexcept;
    double               LimitScale(FieldMode mode, double brightness, double lightness) noexcept;
    RGB                  ColorAtPoint(Point point, double brightness, double lightness = 50.0, FieldMode mode = FieldMode::Brightness, const HexGeometry& geometry = {}) noexcept;
    std::optional<HSB>   HsbFromField(Point point, const FieldInput& input, const HexGeometry& geometry = {}) noexcept;
    Point                PointForColor(RGB rgb, FieldMode mode = FieldMode::Brightness, const HexGeometry& geometry = {}) noexcept;
    /// Origin, R tip, G tip, B tip, in that fixed order. No heap allocation.
    std::array<Point, 4> BuildChain(RGB rgb, const HexGeometry& geometry = {}) noexcept;

    /// CP7: optional '#', exactly RGB, RRGGBB or RRGGBBAA; no partial parses.
    std::optional<RGBA>  ParseHex(std::string_view text) noexcept;
    /// Uppercase #RRGGBB, adding AA iff alpha < 255 (or explicitly requested).
    /// Failure clears a nonempty destination; capacity includes the terminator.
    bool                 FormatHex(RGBA color, char* destination, size_t capacity, bool force_alpha = false) noexcept;
} // namespace ZEngine::UI::ColorMath
