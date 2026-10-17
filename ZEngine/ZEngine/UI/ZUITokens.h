#pragma once
#include <cstdint>

// ZUI design tokens.
//
// Source of truth: Figma file r17DX8aJRKtHEpiV8iAyA8, variable collection "Editor tokens",
// mode "Dark". Names mirror the Figma variables one-to-one (`bg/window` -> BgWindow) so a
// token can be traced back to the design by name. Do not tune values here: change them in
// Figma, then update this file to match (see tests/UI/ZUITheme_test.cpp for the guard).
//
// Values are sRGB [0,1], the same space ZUITheme uses.

namespace ZEngine::UI::Tokens
{
    /// @brief RGBA color in sRGB [0,1].
    struct Color
    {
        float R = {};
        float G = {};
        float B = {};
        float A = {};
    };

    /// @brief Build a Color from a 0xRRGGBB literal (the form Figma shows) and an optional alpha.
    constexpr Color Hex(uint32_t rgb, float a = 1.f)
    {
        return {static_cast<float>((rgb >> 16) & 0xFFu) / 255.f, static_cast<float>((rgb >> 8) & 0xFFu) / 255.f, static_cast<float>(rgb & 0xFFu) / 255.f, a};
    }

    /// @brief Same color with a different alpha.
    constexpr Color WithAlpha(Color c, float a)
    {
        return {c.R, c.G, c.B, a};
    }

    // Surfaces
    inline constexpr Color BgWindow               = Hex(0x171717); // bg/window
    inline constexpr Color BgPanel                = Hex(0x202020); // bg/panel
    inline constexpr Color BgSurface              = Hex(0x292929); // bg/surface
    inline constexpr Color BgControl              = Hex(0x2e3133); // bg/control
    inline constexpr Color BgHover                = Hex(0x393939); // bg/hover
    inline constexpr Color BgViewport             = Hex(0x343b41); // bg/viewport
    inline constexpr Color BgGrid                 = Hex(0x3f474d); // bg/grid
    inline constexpr Color BgBackdrop             = Hex(0x151515); // bg/backdrop (editor frame fill behind the docks)

    // Borders
    inline constexpr Color BorderSubtle           = Hex(0x393939); // border/subtle
    inline constexpr Color BorderStrong           = Hex(0x565d61); // border/strong
    inline constexpr Color BorderField            = Hex(0x4a5359); // border/field
    inline constexpr Color BorderSelected         = Hex(0x2684d9); // border/selected (selected asset tile outline)

    // Text
    inline constexpr Color TextPrimary            = Hex(0xd8d8d8); // text/primary
    inline constexpr Color TextSecondary          = Hex(0x969696); // text/secondary
    inline constexpr Color TextDisabled           = Hex(0x687177); // text/disabled
    inline constexpr Color TextSelected           = Hex(0xaacce8); // text/selected (caption on a selected asset tile)

    // Accents
    inline constexpr Color AccentBlue             = Hex(0x579de3); // accent/blue
    inline constexpr Color AccentSelection        = Hex(0x174d78); // accent/selection
    inline constexpr Color AccentSelectionHover   = Hex(0x1e5d90); // accent/selection-hover    primary button hover
    inline constexpr Color AccentSelectionPressed = Hex(0x113a5a); // accent/selection-pressed  primary button pressed
    inline constexpr Color AccentGreen            = Hex(0x73bc78); // accent/green
    inline constexpr Color AccentRed              = Hex(0xdd6261); // accent/red
    inline constexpr Color AccentOrange           = Hex(0xe8a442); // accent/orange

    // Node graph pins
    inline constexpr Color PinFloat               = Hex(0x73bc78); // pin/float
    inline constexpr Color PinVector              = Hex(0xe8c442); // pin/vector
    inline constexpr Color PinTexture             = Hex(0xdd6261); // pin/texture
    inline constexpr Color PinExec                = Hex(0xd8d8d8); // pin/exec
    inline constexpr Color PinInt                 = Hex(0x5ec4c4); // pin/int

    // Node graph nodes
    inline constexpr Color NodeHeaderConstant     = Hex(0x2f5d34); // node/header-constant
    inline constexpr Color NodeHeaderMath         = Hex(0x4a5359); // node/header-math
    inline constexpr Color NodeHeaderTexture      = Hex(0x7a3636); // node/header-texture
    inline constexpr Color NodeHeaderResult       = Hex(0x174d78); // node/header-result
    inline constexpr Color NodeBody               = Hex(0x262a2d); // node/body
} // namespace ZEngine::UI::Tokens

/// Figma "Editor metrics" (Default). Logical pixels at 100%; UIScale handles device pixels.
namespace ZEngine::UI::Metrics
{
    inline constexpr float Space2           = 2.f;  // space/2
    inline constexpr float Space3           = 3.f;  // space/3
    inline constexpr float Space4           = 4.f;  // space/4
    inline constexpr float Space5           = 5.f;  // space/5
    inline constexpr float Space6           = 6.f;  // space/6
    inline constexpr float Space7           = 7.f;  // space/7
    inline constexpr float Space8           = 8.f;  // space/8
    inline constexpr float Space9           = 9.f;  // space/9
    inline constexpr float Space10          = 10.f; // space/10
    inline constexpr float Space12          = 12.f; // space/12
    inline constexpr float Space16          = 16.f; // space/16
    inline constexpr float Space20          = 20.f; // space/20
    inline constexpr float Radius2          = 2.f;  // radius/2
    inline constexpr float Radius3          = 3.f;  // radius/3
    inline constexpr float Radius4          = 4.f;  // radius/4
    inline constexpr float Radius6          = 6.f;  // radius/6
    inline constexpr float Radius8          = 8.f;  // radius/8
    inline constexpr float HeightControlSm  = 16.f; // height/control-sm
    inline constexpr float HeightRowCompact = 20.f; // height/row-compact
    inline constexpr float HeightField      = 22.f; // height/field
    inline constexpr float HeightRowCheck   = 24.f; // height/row-check
    inline constexpr float HeightSearch     = 25.f; // height/search
    inline constexpr float HeightControl    = 26.f; // height/control
    inline constexpr float HeightRow        = 28.f; // height/row
    inline constexpr float HeightTab        = 30.f; // height/tab
} // namespace ZEngine::UI::Metrics

/// Brace initializer for a `float[4]` member from a Tokens::Color.
#define ZUI_TOKEN(c) {(c).R, (c).G, (c).B, (c).A}
