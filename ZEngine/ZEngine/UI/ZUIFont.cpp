// stb_rect_pack is kept for atlas layout; stb_truetype replaced by FreeType.
#define STB_RECT_PACK_IMPLEMENTATION
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/VFS/IVFSFile.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/UI/ZUIFont.h>
#include <ft2build.h>
#include <stb/stb_rect_pack.h>
#include FT_FREETYPE_H

using namespace ZEngine::Core::VFS;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Rendering;

namespace ZEngine::UI
{
    using namespace ZEngine::Core::Memory;

    // Load a TTF file from the VFS into temp_arena. Returns nullptr on failure.
    static uint8_t* LoadTTFFromVFS(const char* vfs_path, ArenaAllocator* temp_arena, uint64_t* out_size)
    {
        auto* vfs      = Engine::GetContext()->VFS;
        auto  path_res = VFSPath::Parse(vfs_path);
        if (!path_res.Succeeded())
            return nullptr;

        auto file_res = vfs->Open(path_res.Value(), VFSOpenFlags::Read);
        if (!file_res.Succeeded())
            return nullptr;

        auto* file     = file_res.Value();
        auto  size_res = file->Size();
        if (!size_res.Succeeded())
        {
            vfs->Close(file);
            return nullptr;
        }

        uint64_t           sz   = size_res.Value();
        uint8_t*           data = ZPushArray(temp_arena, uint8_t, (uint32_t) sz);
        ArrayView<uint8_t> view{data, sz};
        file->ReadAll(view);
        vfs->Close(file);

        if (out_size)
            *out_size = sz;
        return data;
    }

    ZUIFontAtlas* ZUIFontAtlasBake(ArenaAllocator* persistent_arena, ArenaAllocator* temp_arena, Hardwares::VulkanDevice* device, const char* vfs_path, float size_small, float size_body, float size_header, uint32_t first_codepoint, uint32_t codepoint_count, const char* header_vfs_path)
    {
        // 1. Load TTF data
        uint64_t ttf_size = 0;
        uint8_t* ttf_data = LoadTTFFromVFS(vfs_path, temp_arena, &ttf_size);
        if (!ttf_data)
            return nullptr;

        uint64_t hdr_size     = ttf_size;
        uint8_t* hdr_ttf_data = ttf_data;
        if (header_vfs_path)
        {
            uint8_t* hdr = LoadTTFFromVFS(header_vfs_path, temp_arena, &hdr_size);
            if (hdr)
                hdr_ttf_data = hdr;
        }

        // 2. Initialize FreeType
        FT_Library ft_lib = nullptr;
        if (FT_Init_FreeType(&ft_lib) != 0)
            return nullptr;

        FT_Face ft_body   = nullptr;
        FT_Face ft_header = nullptr;
        FT_New_Memory_Face(ft_lib, ttf_data, (FT_Long) ttf_size, 0, &ft_body);
        if (hdr_ttf_data != ttf_data)
            FT_New_Memory_Face(ft_lib, hdr_ttf_data, (FT_Long) hdr_size, 0, &ft_header);
        else
            ft_header = ft_body;

        const float    kSizes[3]    = {size_small, size_body, size_header};
        FT_Face        kFaces[3]    = {ft_body, ft_body, ft_header};
        const int      kGlyphPad    = 1; // 1-px border so bilinear sampling never bleeds

        // 3. Two-pass atlas packing
        //    Pass A: render each glyph to measure its bitmap dimensions.
        //    Pass B: render again into the atlas at stb_rect_pack positions.
        const uint32_t kTotalGlyphs = 3 * codepoint_count;
        stbrp_rect*    rects        = ZPushArray(temp_arena, stbrp_rect, kTotalGlyphs);

        // Pass A — measure
        for (int fi = 0; fi < 3; ++fi)
        {
            FT_Set_Pixel_Sizes(kFaces[fi], 0, (FT_UInt) kSizes[fi]);
            for (uint32_t gi = 0; gi < codepoint_count; ++gi)
            {
                uint32_t ri       = (uint32_t) fi * codepoint_count + gi;
                rects[ri].id      = (int) ri;
                FT_UInt glyph_idx = FT_Get_Char_Index(kFaces[fi], first_codepoint + gi);
                if (glyph_idx == 0 || FT_Load_Glyph(kFaces[fi], glyph_idx, FT_LOAD_RENDER | FT_LOAD_FORCE_AUTOHINT) != 0)
                {
                    rects[ri].w = rects[ri].h = 0;
                    continue;
                }
                rects[ri].w = (stbrp_coord) (kFaces[fi]->glyph->bitmap.width + 2 * kGlyphPad);
                rects[ri].h = (stbrp_coord) (kFaces[fi]->glyph->bitmap.rows + 2 * kGlyphPad);
            }
        }

        // Pack
        const uint32_t kAtlasW    = 1024;
        const uint32_t kAtlasH    = 2048;
        stbrp_context  pack_ctx   = {};
        stbrp_node*    pack_nodes = ZPushArray(temp_arena, stbrp_node, kAtlasW);
        stbrp_init_target(&pack_ctx, (int) kAtlasW, (int) kAtlasH, pack_nodes, (int) kAtlasW);
        stbrp_pack_rects(&pack_ctx, rects, (int) kTotalGlyphs);

        // Pass B — render into atlas
        uint8_t* atlas_px = ZPushArray(temp_arena, uint8_t, kAtlasW* kAtlasH);
        // White texel at (0,0) — used by solid-color draws
        atlas_px[0]       = 255u;

        for (int fi = 0; fi < 3; ++fi)
        {
            FT_Set_Pixel_Sizes(kFaces[fi], 0, (FT_UInt) kSizes[fi]);
            for (uint32_t gi = 0; gi < codepoint_count; ++gi)
            {
                uint32_t ri = (uint32_t) fi * codepoint_count + gi;
                if (!rects[ri].was_packed || rects[ri].w == 0)
                    continue;

                FT_UInt glyph_idx = FT_Get_Char_Index(kFaces[fi], first_codepoint + gi);
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

        // 4. Expand single-channel → RGBA8 (white text, alpha-masked)
        uint8_t* rgba = ZPushArray(temp_arena, uint8_t, kAtlasW * kAtlasH * 4);
        for (uint32_t i = 0; i < kAtlasW * kAtlasH; ++i)
        {
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = atlas_px[i];
        }

        // 5. Upload to GPU
        Rendering::Textures::TextureHandle gpu_handle = {};
        if (device->RRM)
        {
            auto* rrm  = static_cast<RenderResourceManager*>(device->RRM);
            gpu_handle = rrm->UploadFontAtlas(rgba, kAtlasW, kAtlasH);
        }
        device->TextureHandleToUpdates.Enqueue(gpu_handle);

        // 6. Build ZUIFontAtlas
        ZUIFontAtlas* atlas = ZPushStruct(persistent_arena, ZUIFontAtlas);
        atlas->Handle       = gpu_handle;
        atlas->Width        = kAtlasW;
        atlas->Height       = kAtlasH;
        atlas->WhiteU       = 0.5f / (float) kAtlasW;
        atlas->WhiteV       = 0.5f / (float) kAtlasH;

        float     inv_w     = 1.f / (float) kAtlasW;
        float     inv_h     = 1.f / (float) kAtlasH;

        ZUIFont** kDsts[3]  = {&atlas->Small, &atlas->Body, &atlas->Header};

        for (int fi = 0; fi < 3; ++fi)
        {
            FT_Set_Pixel_Sizes(kFaces[fi], 0, (FT_UInt) kSizes[fi]);

            ZUIFont* font        = ZPushStruct(persistent_arena, ZUIFont);
            font->Glyphs         = ZPushArray(persistent_arena, ZUIGlyph, codepoint_count);
            font->GlyphCount     = codepoint_count;
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

                FT_UInt   glyph_idx = FT_Get_Char_Index(kFaces[fi], first_codepoint + gi);
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

        // 7. Clean up FreeType (its own heap, not arena-tracked)
        if (ft_header != ft_body)
            FT_Done_Face(ft_header);
        FT_Done_Face(ft_body);
        FT_Done_FreeType(ft_lib);

        return atlas;
    }

    void ZUIMeasureText(const ZUIFont* font, const char* str, uint32_t len, float out_size[2])
    {
        if (!font || !str || len == 0)
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
