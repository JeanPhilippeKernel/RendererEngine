#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <ZEngine/ZEngineDef.h>
#include <cstdint>

namespace ZEngine::Hardwares { struct VulkanDevice; }

namespace ZEngine::UI
{
    using namespace ZEngine::Core::Memory;

    struct ZUIGlyph
    {
        float    U0, V0;      // atlas UV top-left  (normalized)
        float    U1, V1;      // atlas UV bottom-right (normalized)
        float    OffsetX;     // screen X offset from cursor
        float    OffsetY;     // screen Y offset from baseline
        float    Width;       // glyph screen width in pixels
        float    Height;      // glyph screen height in pixels
        float    AdvanceX;    // cursor advance after this glyph
    };

    struct ZUIFont
    {
        ZUIGlyph*                          Glyphs         = nullptr; // persistent_arena, [GlyphCount]
        uint32_t                           GlyphCount     = 0;
        uint32_t                           FirstCodepoint = 32;      // inclusive
        float                              FontSize       = 0.f;
        float                              Ascent         = 0.f;
        float                              Descent        = 0.f;
        float                              LineGap        = 0.f;
        float                              LineHeight     = 0.f;     // Ascent - Descent + LineGap
        Rendering::Textures::TextureHandle AtlasHandle    = {};
        uint32_t                           AtlasWidth     = 0;
        uint32_t                           AtlasHeight    = 0;
    };

    // Bake a font atlas from a VFS path.
    // Permanent data (ZUIFont + ZUIGlyph[]) is allocated into persistent_arena.
    // Temporary baking data (TTF bytes, stb contexts, bitmaps) uses temp_arena,
    // which may be any arena the caller controls — including a scratch scope.
    // Returns nullptr on failure.
    ZUIFont* ZUIFontBake(ArenaAllocator*           persistent_arena,
                         ArenaAllocator*           temp_arena,
                         Hardwares::VulkanDevice*  device,
                         const char*               vfs_path,
                         float                     font_size,
                         uint32_t                  atlas_width,
                         uint32_t                  atlas_height,
                         uint32_t                  first_codepoint,
                         uint32_t                  codepoint_count);

    // Measure the pixel extent of a string using a baked font.
    // out_size[0] = width, out_size[1] = line height (single line only).
    void ZUIMeasureText(const ZUIFont* font, const char* str, uint32_t len, float out_size[2]);

} // namespace ZEngine::UI
