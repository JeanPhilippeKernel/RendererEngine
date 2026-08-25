// stb implementations — defined once here, linked into zEngineLib
#define STB_RECT_PACK_IMPLEMENTATION
#include <stb/stb_rect_pack.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

#include <ZEngine/UI/ZUIFont.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/VFS/IVFSFile.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/RenderResourceManager.h>

using namespace ZEngine::Core::VFS;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Rendering;

namespace ZEngine::UI
{
    ZUIFont* ZUIFontBake(ArenaAllocator*          persistent_arena,
                         ArenaAllocator*          temp_arena,
                         Hardwares::VulkanDevice* device,
                         const char*              vfs_path,
                         float                    font_size,
                         uint32_t                 atlas_width,
                         uint32_t                 atlas_height,
                         uint32_t                 first_codepoint,
                         uint32_t                 codepoint_count)
    {
        // --- 1. Load TTF file from VFS into temp_arena ---
        auto* vfs      = Engine::GetContext()->VFS;
        auto  path_res = VFSPath::Parse(vfs_path);
        if (!path_res.Succeeded()) { return nullptr; }

        auto file_res = vfs->Open(path_res.Value(), VFSOpenFlags::Read);
        if (!file_res.Succeeded()) { return nullptr; }

        auto* file     = file_res.Value();
        auto  size_res = file->Size();
        if (!size_res.Succeeded()) { vfs->Close(file); return nullptr; }

        uint64_t  ttf_size = size_res.Value();
        uint8_t*  ttf_data = ZPushArray(temp_arena, uint8_t, (uint32_t)ttf_size);
        ArrayView<uint8_t> view{ttf_data, ttf_size};
        file->ReadAll(view);
        vfs->Close(file);

        // --- 2. Extract font metrics ---
        stbtt_fontinfo font_info = {};
        if (!stbtt_InitFont(&font_info, ttf_data, 0)) { return nullptr; }

        int   raw_ascent   = 0;
        int   raw_descent  = 0;
        int   raw_line_gap = 0;
        stbtt_GetFontVMetrics(&font_info, &raw_ascent, &raw_descent, &raw_line_gap);
        float scale = stbtt_ScaleForPixelHeight(&font_info, font_size);

        // --- 3. Pack glyphs into a single-channel alpha atlas ---
        // atlas pixel buffer (zeroed by arena)
        uint8_t*          single_ch  = ZPushArray(temp_arena, uint8_t, atlas_width * atlas_height);
        stbtt_packedchar* packed     = ZPushArray(temp_arena, stbtt_packedchar, codepoint_count);

        stbtt_pack_context pack_ctx  = {};
        stbtt_PackBegin(&pack_ctx, single_ch, (int)atlas_width, (int)atlas_height, 0, 1, nullptr);
        stbtt_PackSetOversampling(&pack_ctx, 2, 2);
        stbtt_PackFontRange(&pack_ctx, ttf_data, 0, font_size,
                            (int)first_codepoint, (int)codepoint_count, packed);
        stbtt_PackEnd(&pack_ctx);

        // --- 4. Allocate permanent ZUIFont + ZUIGlyph[] ---
        ZUIFont* font        = ZPushStruct(persistent_arena, ZUIFont);
        font->Glyphs         = ZPushArray(persistent_arena, ZUIGlyph, codepoint_count);
        font->GlyphCount     = codepoint_count;
        font->FirstCodepoint = first_codepoint;
        font->FontSize       = font_size;
        font->Ascent         = raw_ascent   * scale;
        font->Descent        = raw_descent  * scale;
        font->LineGap        = raw_line_gap * scale;
        font->LineHeight     = font->Ascent - font->Descent + font->LineGap;
        font->AtlasWidth     = atlas_width;
        font->AtlasHeight    = atlas_height;

        float inv_w = 1.f / (float)atlas_width;
        float inv_h = 1.f / (float)atlas_height;

        for (uint32_t i = 0; i < codepoint_count; ++i)
        {
            const stbtt_packedchar& pc = packed[i];
            ZUIGlyph&               g  = font->Glyphs[i];
            g.U0       = (float)pc.x0 * inv_w;
            g.V0       = (float)pc.y0 * inv_h;
            g.U1       = (float)pc.x1 * inv_w;
            g.V1       = (float)pc.y1 * inv_h;
            g.OffsetX  = pc.xoff;
            g.OffsetY  = pc.yoff;
            g.Width    = pc.xoff2 - pc.xoff;
            g.Height   = pc.yoff2 - pc.yoff;
            g.AdvanceX = pc.xadvance;
        }

        // --- 5. Expand single-channel to RGBA8 (white text, alpha mask) ---
        uint8_t* rgba = ZPushArray(temp_arena, uint8_t, atlas_width * atlas_height * 4);
        for (uint32_t i = 0; i < atlas_width * atlas_height; ++i)
        {
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = single_ch[i];
        }

        // --- 6. Upload to GPU ---
        if (device->RRM)
        {
            auto* rrm     = static_cast<RenderResourceManager*>(device->RRM);
            font->AtlasHandle = rrm->UploadFontAtlas(rgba, atlas_width, atlas_height);
        }
        device->TextureHandleToUpdates.Enqueue(font->AtlasHandle);

        return font;
    }

    void ZUIMeasureText(const ZUIFont* font, const char* str, uint32_t len, float out_size[2])
    {
        if (!font || !str || len == 0)
        {
            out_size[0] = 0.f;
            out_size[1] = 0.f;
            return;
        }

        float width = 0.f;
        for (uint32_t i = 0; i < len; ++i)
        {
            uint32_t cp  = (uint8_t)str[i];
            uint32_t idx = cp - font->FirstCodepoint;
            if (cp < font->FirstCodepoint || idx >= font->GlyphCount) { continue; }
            width += font->Glyphs[idx].AdvanceX;
        }

        out_size[0] = width;
        out_size[1] = font->LineHeight;
    }

} // namespace ZEngine::UI
