// stb_rect_pack is kept for atlas layout; stb_truetype replaced by FreeType.
#define STB_RECT_PACK_IMPLEMENTATION
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/VFS/IVFSFile.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/UI/ZUIAssetIcons.h>
#include <ZEngine/UI/ZUIFont.h>
#include <ft2build.h>
#include <stb/stb_image.h>
#include <stb/stb_rect_pack.h>
#include <cmath>
#include <limits>
#include <memory>
#include <type_traits>
#include FT_FREETYPE_H

using namespace ZEngine::Core::VFS;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Rendering;
using namespace ZEngine::Core::Memory;

namespace ZEngine::UI
{
    // Latin-1 C1 control slots hold extra typographic glyphs so byte-based text can reach them
    // (see ZUI_GLYPH_* in ZUIFont.h).
    static uint32_t ZUIBakedCodepoint(uint32_t byte_value)
    {
        switch (byte_value)
        {
            case 0x90:
                return 0x2212; // typographic minus
            case 0x85:
                return 0x2026; // ellipsis
            case 0x93:
                return 0x201c; // left double quotation mark
            case 0x94:
                return 0x201d; // right double quotation mark
            case 0x95:
                return 0x2022; // bullet
            case 0x96:
                return 0x2013; // en dash
            case 0x97:
                return 0x2014; // em dash
            case 0x8F:
                return 0x2191; // up arrow
            case 0x8E:
                return 0x2193; // down arrow
            default:
                return byte_value;
        }
    }

    // Load a bounded file for FreeType/stb's signed-length APIs. No partial reads.
    static uint8_t* LoadBytesFromVFS(const char* vfs_path, ArenaAllocator* temp_arena, uint64_t* out_size)
    {
        if (out_size)
            *out_size = 0;
        auto* engine = Engine::GetContext();
        auto* vfs    = engine ? engine->VFS : nullptr;
        if (!vfs || !vfs_path || !temp_arena)
            return nullptr;
        auto path_res = VFSPath::Parse(vfs_path);
        if (!path_res.Succeeded())
            return nullptr;

        auto file_res = vfs->Open(path_res.Value(), VFSOpenFlags::Read);
        if (!file_res.Succeeded())
            return nullptr;

        auto* file     = file_res.Value();
        auto  size_res = file->Size();
        if (!size_res.Succeeded() || !size_res.Value() || size_res.Value() > static_cast<uint64_t>(std::numeric_limits<int>::max()))
        {
            vfs->Close(file);
            return nullptr;
        }

        uint64_t   sz   = size_res.Value();
        uint8_t*   data = static_cast<uint8_t*>(temp_arena->AllocateNoZero(static_cast<size_t>(sz)));
        const auto read = data ? file->ReadAll({data, sz}) : VFSResult<size_t>::Fail(VFSError::OutOfMemory);
        vfs->Close(file);
        if (read.Failed() || read.Value() != sz)
            return nullptr;

        if (out_size)
            *out_size = sz;
        return data;
    }

    static ZUIFont* AllocateFont(ArenaAllocator* arena, uint32_t glyph_count)
    {
        auto* font = ZPushStruct<ZUIFont>(arena);
        if (!font)
            return nullptr;
        new (font) ZUIFont{}; // In particular, FontScale must default to 1, not zero.
        font->Glyphs = ZPushArray<ZUIGlyph>(arena, glyph_count);
        if (!font->Glyphs)
            return nullptr;
        font->GlyphCount = glyph_count;
        return font;
    }

    ZUIFontAtlas* ZUIFontAtlasBake(ArenaAllocator* persistent_arena, ArenaAllocator* temp_arena, Hardwares::VulkanDevice* device, const char* vfs_path, float size_small, float size_body, float size_header, uint32_t first_codepoint, uint32_t codepoint_count, const char* header_vfs_path, const char* icon_vfs_path, const uint32_t* icon_codepoints, uint32_t icon_count, float icon_size, float size_tiny, float size_large, float size_heading, const char* asset_icons_vfs_path)
    {
        constexpr uint32_t kFontVariants = 6;
        constexpr uint32_t kAtlasW       = 1024;
        constexpr uint32_t kAtlasH       = 2048;
        constexpr int      kGlyphPad     = 1; // 1-px border so bilinear sampling never bleeds.
        const uint64_t     max_glyphs    = static_cast<uint64_t>(kFontVariants) * codepoint_count + icon_count + kZUIAssetIconCount;
        if (!persistent_arena || !temp_arena || !device || !codepoint_count || first_codepoint > std::numeric_limits<uint32_t>::max() - (codepoint_count - 1) || max_glyphs > static_cast<uint64_t>(std::numeric_limits<int>::max()))
            return nullptr;
        for (float size : {size_small, size_body, size_header, size_tiny, size_large, size_heading, icon_size})
            if (!std::isfinite(size) || size < 0.f || size > static_cast<float>(kAtlasH - 2 * kGlyphPad))
                return nullptr;

        // 1. Load TTF data
        uint64_t ttf_size = 0;
        uint8_t* ttf_data = LoadBytesFromVFS(vfs_path, temp_arena, &ttf_size);
        if (!ttf_data)
            return nullptr;

        uint64_t hdr_size     = ttf_size;
        uint8_t* hdr_ttf_data = ttf_data;
        if (header_vfs_path)
        {
            uint64_t loaded_hdr_size = 0;
            uint8_t* hdr             = LoadBytesFromVFS(header_vfs_path, temp_arena, &loaded_hdr_size);
            if (hdr)
            {
                hdr_ttf_data = hdr;
                hdr_size     = loaded_hdr_size;
            }
        }

        // 2. Initialize FreeType
        FT_Library ft_lib = nullptr;
        if (FT_Init_FreeType(&ft_lib) != 0)
            return nullptr;
        const std::unique_ptr<std::remove_pointer_t<FT_Library>, decltype(&FT_Done_FreeType)> library(ft_lib, FT_Done_FreeType);

        FT_Face                                                                               ft_body   = nullptr;
        FT_Face                                                                               ft_header = nullptr;
        if (FT_New_Memory_Face(ft_lib, ttf_data, (FT_Long) ttf_size, 0, &ft_body) != 0)
            return nullptr;
        const std::unique_ptr<std::remove_pointer_t<FT_Face>, decltype(&FT_Done_Face)> body_face(ft_body, FT_Done_Face);
        if (hdr_ttf_data != ttf_data)
            FT_New_Memory_Face(ft_lib, hdr_ttf_data, (FT_Long) hdr_size, 0, &ft_header);
        const std::unique_ptr<std::remove_pointer_t<FT_Face>, decltype(&FT_Done_Face)> header_face(ft_header, FT_Done_Face);
        if (!ft_header)
            ft_header = ft_body;

        FT_Face ft_icon = nullptr;
        if (icon_vfs_path && icon_codepoints && icon_count > 0 && icon_size > 0.f)
        {
            uint64_t icon_ttf_size = 0;
            uint8_t* icon_ttf      = LoadBytesFromVFS(icon_vfs_path, temp_arena, &icon_ttf_size);
            if (icon_ttf)
                FT_New_Memory_Face(ft_lib, icon_ttf, (FT_Long) icon_ttf_size, 0, &ft_icon);
        }
        const std::unique_ptr<std::remove_pointer_t<FT_Face>, decltype(&FT_Done_Face)> icon_face(ft_icon, FT_Done_Face);
        const uint32_t                                                                 kIconGlyphs = ft_icon ? icon_count : 0;

        // Decode the authored thumbnail sheet once during atlas creation.
        // Validate dimensions before decompression, and release stb's temporary
        // heap storage on every exit. Only its alpha masks enter the shared atlas.
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>                           asset_pixels(nullptr, stbi_image_free);
        if (asset_icons_vfs_path)
        {
            uint64_t    png_size = 0;
            const auto* png      = LoadBytesFromVFS(asset_icons_vfs_path, temp_arena, &png_size);
            int         width = 0, height = 0, channels = 0;
            if (png && stbi_info_from_memory(png, static_cast<int>(png_size), &width, &height, &channels) && width == static_cast<int>(kZUIAssetIconPixels * kZUIAssetIconCount) && height == static_cast<int>(kZUIAssetIconPixels) && channels == STBI_grey)
                asset_pixels.reset(stbi_load_from_memory(png, static_cast<int>(png_size), &width, &height, &channels, STBI_grey));
            if (!asset_pixels)
                ZENGINE_CORE_ERROR("[ZUI] Could not load thumbnail artwork: {}", asset_icons_vfs_path);
        }
        const uint32_t kAssetGlyphs          = asset_pixels ? kZUIAssetIconCount : 0;

        // Header and Heading use the semibold face; both share the single GPU atlas.
        // A size of 0 skips an optional variant.
        const float    kSizes[kFontVariants] = {size_small, size_body, size_header, size_tiny, size_large, size_heading};
        FT_Face        kFaces[kFontVariants] = {ft_body, ft_body, ft_header, ft_body, ft_body, ft_header};

        // 3. Two-pass atlas packing
        //    Pass A: render each glyph to measure its bitmap dimensions.
        //    Pass B: render again into the atlas at stb_rect_pack positions.
        const uint32_t kAssetStart           = kFontVariants * codepoint_count + kIconGlyphs;
        const uint32_t kTotalGlyphs          = kAssetStart + kAssetGlyphs;
        stbrp_rect*    rects                 = ZPushArray<stbrp_rect>(temp_arena, kTotalGlyphs);
        if (!rects)
            return nullptr;

        // Pass A — measure
        for (int fi = 0; fi < kFontVariants; ++fi)
        {
            if (kSizes[fi] <= 0.f)
                continue;
            if (FT_Set_Pixel_Sizes(kFaces[fi], 0, (FT_UInt) kSizes[fi]) != 0)
                return nullptr;
            for (uint32_t gi = 0; gi < codepoint_count; ++gi)
            {
                uint32_t ri       = (uint32_t) fi * codepoint_count + gi;
                rects[ri].id      = (int) ri;
                FT_UInt glyph_idx = FT_Get_Char_Index(kFaces[fi], ZUIBakedCodepoint(first_codepoint + gi));
                if (glyph_idx == 0 || FT_Load_Glyph(kFaces[fi], glyph_idx, FT_LOAD_RENDER | FT_LOAD_FORCE_AUTOHINT) != 0)
                {
                    rects[ri].w = rects[ri].h = 0;
                    continue;
                }
                rects[ri].w = (stbrp_coord) (kFaces[fi]->glyph->bitmap.width + 2 * kGlyphPad);
                rects[ri].h = (stbrp_coord) (kFaces[fi]->glyph->bitmap.rows + 2 * kGlyphPad);
            }
        }

        if (ft_icon)
        {
            if (FT_Set_Pixel_Sizes(ft_icon, 0, (FT_UInt) icon_size) != 0)
                return nullptr;
            for (uint32_t ii = 0; ii < kIconGlyphs; ++ii)
            {
                uint32_t ri       = kFontVariants * codepoint_count + ii;
                rects[ri].id      = (int) ri;
                FT_UInt glyph_idx = FT_Get_Char_Index(ft_icon, icon_codepoints[ii]);
                if (glyph_idx == 0 || FT_Load_Glyph(ft_icon, glyph_idx, FT_LOAD_RENDER | FT_LOAD_NO_HINTING) != 0)
                {
                    rects[ri].w = rects[ri].h = 0;
                    continue;
                }
                rects[ri].w = (stbrp_coord) (ft_icon->glyph->bitmap.width + 2 * kGlyphPad);
                rects[ri].h = (stbrp_coord) (ft_icon->glyph->bitmap.rows + 2 * kGlyphPad);
            }
        }

        for (uint32_t i = 0; i < kAssetGlyphs; ++i)
        {
            auto& rect = rects[kAssetStart + i];
            rect.id    = static_cast<int>(kAssetStart + i);
            rect.w = rect.h = static_cast<stbrp_coord>(kZUIAssetIconPixels + 2 * kGlyphPad);
        }

        // Pack
        stbrp_context pack_ctx   = {};
        stbrp_node*   pack_nodes = ZPushArray<stbrp_node>(temp_arena, kAtlasW);
        if (!pack_nodes)
            return nullptr;
        stbrp_init_target(&pack_ctx, (int) kAtlasW, (int) kAtlasH, pack_nodes, (int) kAtlasW);
        // Optional artwork must not displace text or toolbar glyphs.
        if (!stbrp_pack_rects(&pack_ctx, rects, (int) kAssetStart))
        {
            ZENGINE_CORE_ERROR("[ZUI] Font glyphs exceed the shared atlas capacity");
            return nullptr;
        }
        if (kAssetGlyphs && !stbrp_pack_rects(&pack_ctx, rects + kAssetStart, (int) kAssetGlyphs))
            ZENGINE_CORE_ERROR("[ZUI] Thumbnail artwork exceeds the shared atlas capacity; using toolbar glyphs where needed");

        // Pass B — render into atlas
        uint8_t* atlas_px = ZPushArray<uint8_t>(temp_arena, kAtlasW * kAtlasH);
        if (!atlas_px)
            return nullptr;
        // White texel at (0,0) — used by solid-color draws
        atlas_px[0] = 255u;

        for (int fi = 0; fi < kFontVariants; ++fi)
        {
            if (kSizes[fi] <= 0.f)
                continue;
            if (FT_Set_Pixel_Sizes(kFaces[fi], 0, (FT_UInt) kSizes[fi]) != 0)
                return nullptr;
            for (uint32_t gi = 0; gi < codepoint_count; ++gi)
            {
                uint32_t ri = (uint32_t) fi * codepoint_count + gi;
                if (!rects[ri].was_packed || rects[ri].w == 0)
                    continue;

                FT_UInt glyph_idx = FT_Get_Char_Index(kFaces[fi], ZUIBakedCodepoint(first_codepoint + gi));
                if (glyph_idx == 0 || FT_Load_Glyph(kFaces[fi], glyph_idx, FT_LOAD_RENDER | FT_LOAD_FORCE_AUTOHINT) != 0)
                    continue;

                FT_Bitmap& bmp   = kFaces[fi]->glyph->bitmap;
                int        dst_x = (int) rects[ri].x + kGlyphPad;
                int        dst_y = (int) rects[ri].y + kGlyphPad;

                for (int row = 0; row < (int) bmp.rows; ++row)
                {
                    for (int col = 0; col < (int) bmp.width; ++col)
                    {
                        int dst       = (dst_y + row) * (int) kAtlasW + (dst_x + col);
                        atlas_px[dst] = bmp.buffer[row * abs(bmp.pitch) + col];
                    }
                }
            }
        }

        if (ft_icon)
        {
            if (FT_Set_Pixel_Sizes(ft_icon, 0, (FT_UInt) icon_size) != 0)
                return nullptr;
            for (uint32_t ii = 0; ii < kIconGlyphs; ++ii)
            {
                uint32_t ri = kFontVariants * codepoint_count + ii;
                if (!rects[ri].was_packed || rects[ri].w == 0)
                    continue;
                FT_UInt glyph_idx = FT_Get_Char_Index(ft_icon, icon_codepoints[ii]);
                if (glyph_idx == 0 || FT_Load_Glyph(ft_icon, glyph_idx, FT_LOAD_RENDER | FT_LOAD_NO_HINTING) != 0)
                    continue;
                FT_Bitmap& bmp   = ft_icon->glyph->bitmap;
                int        dst_x = (int) rects[ri].x + kGlyphPad;
                int        dst_y = (int) rects[ri].y + kGlyphPad;
                for (int row = 0; row < (int) bmp.rows; ++row)
                    for (int col = 0; col < (int) bmp.width; ++col)
                        atlas_px[(dst_y + row) * (int) kAtlasW + (dst_x + col)] = bmp.buffer[row * abs(bmp.pitch) + col];
            }
        }

        for (uint32_t i = 0; i < kAssetGlyphs; ++i)
        {
            const auto& rect = rects[kAssetStart + i];
            if (!rect.was_packed)
                continue;
            for (uint32_t y = 0; y < kZUIAssetIconPixels; ++y)
            {
                const size_t source      = static_cast<size_t>(y) * kZUIAssetIconCount * kZUIAssetIconPixels + i * kZUIAssetIconPixels;
                const size_t destination = (rect.y + kGlyphPad + y) * kAtlasW + rect.x + kGlyphPad;
                Helpers::secure_memcpy(atlas_px + destination, kAtlasW * kAtlasH - destination, asset_pixels.get() + source, kZUIAssetIconPixels);
            }
        }
        asset_pixels.reset(); // Coverage is now in atlas_px; do not retain the decoder buffer through upload.

        // 4. Expand single-channel → RGBA8 (white text, alpha-masked)
        uint8_t* rgba = ZPushArray<uint8_t>(temp_arena, kAtlasW * kAtlasH * 4);
        if (!rgba)
            return nullptr;
        for (uint32_t i = 0; i < kAtlasW * kAtlasH; ++i)
        {
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = atlas_px[i];
        }

        // Build all CPU metadata before upload, so allocation failure cannot leak a GPU texture.
        ZUIFontAtlas* atlas = ZPushStruct<ZUIFontAtlas>(persistent_arena);
        if (!atlas)
            return nullptr;
        new (atlas) ZUIFontAtlas{};
        atlas->Width                   = kAtlasW;
        atlas->Height                  = kAtlasH;
        atlas->WhiteU                  = 0.5f / (float) kAtlasW;
        atlas->WhiteV                  = 0.5f / (float) kAtlasH;

        float     inv_w                = 1.f / (float) kAtlasW;
        float     inv_h                = 1.f / (float) kAtlasH;

        ZUIFont** kDsts[kFontVariants] = {&atlas->Small, &atlas->Body, &atlas->Header, &atlas->Tiny, &atlas->Large, &atlas->Heading};

        for (int fi = 0; fi < kFontVariants; ++fi)
        {
            if (kSizes[fi] <= 0.f)
                continue;
            if (FT_Set_Pixel_Sizes(kFaces[fi], 0, (FT_UInt) kSizes[fi]) != 0)
                return nullptr;

            ZUIFont* font = AllocateFont(persistent_arena, codepoint_count);
            if (!font)
                return nullptr;
            font->FirstCodepoint = first_codepoint;
            font->FontSize       = kSizes[fi];

            // FreeType metrics are in 26.6 fixed-point — shift right 6 to get pixels
            font->Ascent         = (float) (kFaces[fi]->size->metrics.ascender >> 6);
            font->Descent        = (float) (kFaces[fi]->size->metrics.descender >> 6); // negative
            font->LineGap        = 0.f;
            font->LineHeight     = (float) (kFaces[fi]->size->metrics.height >> 6);

            for (uint32_t gi = 0; gi < codepoint_count; ++gi)
            {
                ZUIGlyph& g         = font->Glyphs[gi];
                uint32_t  ri        = (uint32_t) fi * codepoint_count + gi;

                FT_UInt   glyph_idx = FT_Get_Char_Index(kFaces[fi], ZUIBakedCodepoint(first_codepoint + gi));
                if (glyph_idx == 0 || FT_Load_Glyph(kFaces[fi], glyph_idx, FT_LOAD_RENDER | FT_LOAD_FORCE_AUTOHINT) != 0)
                {
                    // Missing glyph — advance only, no visible quad
                    g          = {};
                    g.AdvanceX = kSizes[fi] * 0.5f; // fallback width
                    continue;
                }

                FT_GlyphSlot slot = kFaces[fi]->glyph;

                g.OffsetX         = (float) slot->bitmap_left;
                g.OffsetY         = -(float) slot->bitmap_top; // FT: positive = above baseline; ZUI: positive = below
                g.Width           = (float) slot->bitmap.width;
                g.Height          = (float) slot->bitmap.rows;
                g.AdvanceX        = (float) (slot->advance.x >> 6);

                if (rects[ri].was_packed && rects[ri].w > 0)
                {
                    int px0 = (int) rects[ri].x + kGlyphPad;
                    int py0 = (int) rects[ri].y + kGlyphPad;
                    g.U0    = (float) px0 * inv_w;
                    g.V0    = (float) py0 * inv_h;
                    g.U1    = (float) (px0 + (int) slot->bitmap.width) * inv_w;
                    g.V1    = (float) (py0 + (int) slot->bitmap.rows) * inv_h;
                }
            }

            *kDsts[fi] = font;
        }

        if (ft_icon)
        {
            if (FT_Set_Pixel_Sizes(ft_icon, 0, (FT_UInt) icon_size) != 0)
                return nullptr;
            ZUIFont* font = AllocateFont(persistent_arena, kIconGlyphs);
            if (!font)
                return nullptr;
            font->FirstCodepoint = 0;
            font->FontSize       = icon_size;
            font->Ascent         = (float) (ft_icon->size->metrics.ascender >> 6);
            font->Descent        = (float) (ft_icon->size->metrics.descender >> 6);
            font->LineHeight     = (float) (ft_icon->size->metrics.height >> 6);
            for (uint32_t ii = 0; ii < kIconGlyphs; ++ii)
            {
                ZUIGlyph& g         = font->Glyphs[ii];
                uint32_t  ri        = kFontVariants * codepoint_count + ii;
                FT_UInt   glyph_idx = FT_Get_Char_Index(ft_icon, icon_codepoints[ii]);
                if (glyph_idx == 0 || FT_Load_Glyph(ft_icon, glyph_idx, FT_LOAD_RENDER | FT_LOAD_NO_HINTING) != 0)
                {
                    g          = {};
                    g.AdvanceX = icon_size;
                    continue;
                }
                FT_GlyphSlot slot = ft_icon->glyph;
                g.OffsetX         = (float) slot->bitmap_left;
                g.OffsetY         = -(float) slot->bitmap_top;
                g.Width           = (float) slot->bitmap.width;
                g.Height          = (float) slot->bitmap.rows;
                g.AdvanceX        = (float) (slot->advance.x >> 6);
                if (rects[ri].was_packed && rects[ri].w > 0)
                {
                    int px0 = (int) rects[ri].x + kGlyphPad;
                    int py0 = (int) rects[ri].y + kGlyphPad;
                    g.U0    = (float) px0 * inv_w;
                    g.V0    = (float) py0 * inv_h;
                    g.U1    = (float) (px0 + (int) slot->bitmap.width) * inv_w;
                    g.V1    = (float) (py0 + (int) slot->bitmap.rows) * inv_h;
                }
            }
            atlas->Icon = font;
        }

        if (kAssetGlyphs)
        {
            auto* font = AllocateFont(persistent_arena, static_cast<uint32_t>(ZUIIcon::Count));
            if (!font)
                return nullptr;
            font->FirstCodepoint = 0;
            font->FontSize = font->Ascent = font->LineHeight = static_cast<float>(kZUIAssetIconPixels);
            for (uint32_t i = 0; i < kAssetGlyphs; ++i)
            {
                const auto& rect = rects[kAssetStart + i];
                if (!rect.was_packed)
                    continue;
                auto& glyph = font->Glyphs[static_cast<uint16_t>(kZUIAssetIcons[i])];
                glyph.Width = glyph.Height = glyph.AdvanceX = static_cast<float>(kZUIAssetIconPixels);
                glyph.OffsetY                               = -static_cast<float>(kZUIAssetIconPixels);
                glyph.U0                                    = (rect.x + kGlyphPad) * inv_w;
                glyph.V0                                    = (rect.y + kGlyphPad) * inv_h;
                glyph.U1                                    = (rect.x + kGlyphPad + kZUIAssetIconPixels) * inv_w;
                glyph.V1                                    = (rect.y + kGlyphPad + kZUIAssetIconPixels) * inv_h;
            }
            atlas->AssetIcon = font;
        }

        if (device->RRM)
        {
            auto* rrm     = static_cast<RenderResourceManager*>(device->RRM);
            atlas->Handle = rrm->UploadFontAtlas(rgba, kAtlasW, kAtlasH);
            if (!atlas->Handle.Valid())
                return nullptr;
            device->RequestDescriptorUpdate(atlas->Handle);
        }

        return atlas;
    }

    void ZUIMeasureText(const ZUIFont* font, const char* str, uint32_t len, float out_size[2])
    {
        if (!font || !font->Glyphs || !str || len == 0)
        {
            out_size[0] = out_size[1] = 0.f;
            return;
        }

        float width = 0.f;
        for (uint32_t i = 0; i < len; ++i)
        {
            uint32_t cp  = (uint8_t) str[i];
            uint32_t idx = cp - font->FirstCodepoint;
            if (cp < font->FirstCodepoint || idx >= font->GlyphCount)
                continue;
            width += font->Glyphs[idx].AdvanceX;
        }
        out_size[0] = width * font->FontScale;
        out_size[1] = font->LineHeight * font->FontScale;
    }

} // namespace ZEngine::UI
