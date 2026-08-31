#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <ZEngine/ZEngineDef.h>
#include <cstdint>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
}

namespace ZEngine::UI
{

    /// @brief Per-glyph metrics and atlas UV coordinates.
    ///
    /// All dimensional fields are in logical pixels before FontScale is applied.
    struct ZUIGlyph
    {
        float U0      = 0.f; ///< Atlas UV left edge
        float V0      = 0.f; ///< Atlas UV top edge
        float U1      = 0.f; ///< Atlas UV right edge
        float V1      = 0.f; ///< Atlas UV bottom edge
        float OffsetX = 0.f; ///< Pen X offset (logical px)
        float OffsetY = 0.f; ///< Pen Y offset from baseline (logical px; positive = below)
        float Width   = 0.f; ///< Glyph screen width (logical px)
        float Height  = 0.f; ///< Glyph screen height (logical px)
        float AdvanceX = 0.f; ///< Cursor advance after this glyph (logical px)
    };

    /// @brief Rasterized font data for one size variant (Small / Body / Header).
    ///
    /// Glyphs are stored in a flat array indexed by (codepoint - FirstCodepoint).
    /// All sizes are in logical pixels; multiply by FontScale to get screen coordinates.
    struct ZUIFont
    {
        ZUIGlyph* Glyphs         = nullptr;
        uint32_t  GlyphCount     = 0;
        uint32_t  FirstCodepoint = 32;
        float     FontSize       = 0.f;
        float     Ascent         = 0.f;
        float     Descent        = 0.f;
        float     LineGap        = 0.f;
        float     LineHeight     = 0.f;
        // Converts atlas-pixel metrics to logical screen coordinates.
        // Set to 1/UIScale (= 0.5 on Retina) after baking so glyph quads
        // render at the correct logical size even when baked at physical density.
        float     FontScale      = 1.f;
    };

    /// @brief Single shared GPU texture atlas containing all three font size variants.
    ///
    /// Mirrors ImGui's single-atlas approach. WhiteU/WhiteV address the 1×1 white
    /// texel at pixel (0,0): sampling it with a vertex color yields that color directly,
    /// so solid-color draws reuse the same texture and pipeline state as glyph draws.
    struct ZUIFontAtlas
    {
        ZUIFont*                           Small  = nullptr;
        ZUIFont*                           Body   = nullptr;
        ZUIFont*                           Header = nullptr;
        Rendering::Textures::TextureHandle Handle = {};
        uint32_t                           Width  = 0;
        uint32_t                           Height = 0;
        float                              WhiteU = 0.f;
        float                              WhiteV = 0.f;
    };

    /// @brief Pack Small + Body + Header fonts into one shared atlas texture.
    ///
    /// Uses FreeType with FT_LOAD_FORCE_AUTOHINT for cross-platform quality.
    /// Atlas layout handled by stb_rect_pack (1-px glyph padding to prevent UV bleed).
    /// Permanent allocations (ZUIFontAtlas, ZUIFont[3], ZUIGlyph arrays) go to @p persistent_arena.
    /// Temporary baking buffers (TTF bytes, FreeType bitmaps, pixel maps) go to @p temp_arena.
    /// @returns Pointer to the baked ZUIFontAtlas, or nullptr on failure.
    ZUIFontAtlas* ZUIFontAtlasBake(ZEngine::Core::Memory::ArenaAllocator* persistent_arena, ZEngine::Core::Memory::ArenaAllocator* temp_arena, Hardwares::VulkanDevice* device, const char* vfs_path, float size_small, float size_body, float size_header, uint32_t first_codepoint, uint32_t codepoint_count, const char* header_vfs_path = nullptr);

    /// @brief Measure the rendered extent of a string in logical pixels.
    /// @param font     Font to measure with; must not be nullptr.
    /// @param str      String to measure (need not be null-terminated beyond @p len).
    /// @param len      Number of bytes in @p str.
    /// @param out_size Receives [width, height] in logical pixels.
    void          ZUIMeasureText(const ZUIFont* font, const char* str, uint32_t len, float out_size[2]);

} // namespace ZEngine::UI
