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

    struct ZUIGlyph
    {
        float U0, V0;   // atlas UV top-left
        float U1, V1;   // atlas UV bottom-right
        float OffsetX;  // pen X offset (logical px)
        float OffsetY;  // pen Y offset from baseline (logical px)
        float Width;    // screen width  (logical px)
        float Height;   // screen height (logical px)
        float AdvanceX; // cursor advance (logical px)
    };

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

    // Single shared texture atlas — all fonts packed together (ImGui approach).
    // WhiteU/WhiteV is the UV of the 1×1 white texel at pixel (0,0).
    // Use it for solid-color quads: sampling white × vertex color = vertex color.
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

    // Pack Small + Body + Header fonts into one atlas in a single stbtt_pack_context pass.
    // OversampleH=2, OversampleV=1 (ImGui default).
    // Permanent allocations (ZUIFontAtlas, ZUIFont[3], ZUIGlyph arrays) → persistent_arena.
    // Temporary baking buffers (TTF bytes, pixel maps) → temp_arena (caller releases it).
    ZUIFontAtlas* ZUIFontAtlasBake(ZEngine::Core::Memory::ArenaAllocator* persistent_arena, ZEngine::Core::Memory::ArenaAllocator* temp_arena, Hardwares::VulkanDevice* device, const char* vfs_path, float size_small, float size_body, float size_header, uint32_t first_codepoint, uint32_t codepoint_count, const char* header_vfs_path = nullptr);

    void          ZUIMeasureText(const ZUIFont* font, const char* str, uint32_t len, float out_size[2]);

} // namespace ZEngine::UI
