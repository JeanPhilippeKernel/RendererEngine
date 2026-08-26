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
    ZUIFontAtlas* ZUIFontAtlasBake(ArenaAllocator*          persistent_arena,
                                   ArenaAllocator*          temp_arena,
                                   Hardwares::VulkanDevice* device,
                                   const char*              vfs_path,
                                   float                    size_small,
                                   float                    size_body,
                                   float                    size_header,
                                   uint32_t                 first_codepoint,
                                   uint32_t                 codepoint_count,
                                   const char*              header_vfs_path)
    {
        // --- 1. Load TTF from VFS ---
        auto* vfs      = Engine::GetContext()->VFS;
        auto  path_res = VFSPath::Parse(vfs_path);
        if (!path_res.Succeeded()) { return nullptr; }

        auto file_res = vfs->Open(path_res.Value(), VFSOpenFlags::Read);
        if (!file_res.Succeeded()) { return nullptr; }

        auto* file     = file_res.Value();
        auto  size_res = file->Size();
        if (!size_res.Succeeded()) { vfs->Close(file); return nullptr; }

        uint64_t ttf_size = size_res.Value();
        uint8_t* ttf_data = ZPushArray(temp_arena, uint8_t, (uint32_t)ttf_size);
        ArrayView<uint8_t> view{ttf_data, ttf_size};
        file->ReadAll(view);
        vfs->Close(file);

        // --- 2. Determine atlas size ---
        // With 3 fonts (OversampleH=2, OversampleV=1) each glyph occupies
        // roughly (size*2) × size pixels. At body=52px, 96 glyphs per font:
        //   Small(35): 70×32 = 2240  → 96 × 2240 =  215K
        //   Body (52): 104×47 = 4888 → 96 × 4888 =  470K
        //   Header(62): 124×56 = 6944→ 96 × 6944 =  667K
        //   Total ≈ 1.35M < 2048×1024=2M — fits in 2048×1024.
        // Use 1024×2048 (portrait) for comfortable packing.
        const uint32_t kAtlasW = 1024;
        const uint32_t kAtlasH = 2048;

        // --- 3. Allocate atlas pixel buffer (single channel, zero-init) ---
        uint8_t* single_ch = ZPushArray(temp_arena, uint8_t, kAtlasW * kAtlasH);

        // --- 4. Load optional header font (e.g. SemiBold for better hierarchy) ---
        uint8_t* hdr_ttf_data = ttf_data; // default: same font for all sizes
        if (header_vfs_path)
        {
            auto hdr_res = vfs->Open(VFSPath::Parse(header_vfs_path).Value(), VFSOpenFlags::Read);
            if (hdr_res.Succeeded())
            {
                auto* hdr_file = hdr_res.Value();
                auto  hdr_size = hdr_file->Size();
                if (hdr_size.Succeeded())
                {
                    hdr_ttf_data = ZPushArray(temp_arena, uint8_t, (uint32_t)hdr_size.Value());
                    ArrayView<uint8_t> hdr_view{hdr_ttf_data, hdr_size.Value()};
                    hdr_file->ReadAll(hdr_view);
                }
                vfs->Close(hdr_file);
            }
        }

        // --- 5. Pack all three fonts in one stbtt_pack_context pass ---
        const float  kSizes[3]    = { size_small, size_body, size_header };
        uint8_t*     kFontData[3] = { ttf_data, ttf_data, hdr_ttf_data };
        stbtt_packedchar* packed[3];
        for (int i = 0; i < 3; ++i)
            packed[i] = ZPushArray(temp_arena, stbtt_packedchar, codepoint_count);

        stbtt_pack_context pack_ctx = {};
        stbtt_PackBegin(&pack_ctx, single_ch, (int)kAtlasW, (int)kAtlasH, 0, 1, nullptr);
        stbtt_PackSetOversampling(&pack_ctx, 3, 1);
        for (int i = 0; i < 3; ++i)
        {
            stbtt_PackFontRange(&pack_ctx, kFontData[i], 0, kSizes[i],
                                (int)first_codepoint, (int)codepoint_count, packed[i]);
        }
        stbtt_PackEnd(&pack_ctx);

        // --- 5. Reserve pixel (0,0) as the white texel ---
        // stbtt_PackBegin zero-fills and uses 1-px padding, so (0,0) is unused.
        // Setting it to 255 gives a "solid white" UV that solid-color draws can use.
        single_ch[0] = 255u;

        // --- 6. Expand single-channel to RGBA8 (white text, alpha-masked) ---
        uint8_t* rgba = ZPushArray(temp_arena, uint8_t, kAtlasW * kAtlasH * 4);
        for (uint32_t i = 0; i < kAtlasW * kAtlasH; ++i)
        {
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = single_ch[i];
        }

        // --- 7. Upload to GPU (one texture for all fonts) ---
        Rendering::Textures::TextureHandle gpu_handle = {};
        if (device->RRM)
        {
            auto* rrm  = static_cast<RenderResourceManager*>(device->RRM);
            gpu_handle = rrm->UploadFontAtlas(rgba, kAtlasW, kAtlasH);
        }
        device->TextureHandleToUpdates.Enqueue(gpu_handle);

        // --- 8. Fill permanent ZUIFontAtlas ---
        ZUIFontAtlas* atlas = ZPushStruct(persistent_arena, ZUIFontAtlas);
        atlas->Handle       = gpu_handle;
        atlas->Width        = kAtlasW;
        atlas->Height       = kAtlasH;
        // White pixel sits at texel (0,0) — sample its centre
        atlas->WhiteU       = 0.5f / (float)kAtlasW;
        atlas->WhiteV       = 0.5f / (float)kAtlasH;

        // --- 9. Fill per-font metrics ---
        stbtt_fontinfo font_info = {};
        stbtt_InitFont(&font_info, ttf_data, 0);

        const char* kFontNames[3] = { "Small", "Body", "Header" };
        ZUIFont**   kDsts[3]      = { &atlas->Small, &atlas->Body, &atlas->Header };
        (void)kFontNames;

        float inv_w = 1.f / (float)kAtlasW;
        float inv_h = 1.f / (float)kAtlasH;

        for (int fi = 0; fi < 3; ++fi)
        {
            float font_size = kSizes[fi];
            float scale     = stbtt_ScaleForPixelHeight(&font_info, font_size);

            int raw_ascent = 0, raw_descent = 0, raw_line_gap = 0;
            stbtt_GetFontVMetrics(&font_info, &raw_ascent, &raw_descent, &raw_line_gap);

            ZUIFont* font        = ZPushStruct(persistent_arena, ZUIFont);
            font->Glyphs         = ZPushArray(persistent_arena, ZUIGlyph, codepoint_count);
            font->GlyphCount     = codepoint_count;
            font->FirstCodepoint = first_codepoint;
            font->FontSize       = font_size;
            font->Ascent         = (float)raw_ascent   * scale;
            font->Descent        = (float)raw_descent  * scale;
            font->LineGap        = (float)raw_line_gap * scale;
            font->LineHeight     = font->Ascent - font->Descent + font->LineGap;

            for (uint32_t gi = 0; gi < codepoint_count; ++gi)
            {
                const stbtt_packedchar& pc = packed[fi][gi];
                ZUIGlyph&               g  = font->Glyphs[gi];
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

            *kDsts[fi] = font;
        }

        return atlas;
    }

    void ZUIMeasureText(const ZUIFont* font, const char* str, uint32_t len, float out_size[2])
    {
        if (!font || !str || len == 0) { out_size[0] = out_size[1] = 0.f; return; }

        float width = 0.f;
        for (uint32_t i = 0; i < len; ++i)
        {
            uint32_t cp  = (uint8_t)str[i];
            uint32_t idx = cp - font->FirstCodepoint;
            if (cp < font->FirstCodepoint || idx >= font->GlyphCount) { continue; }
            width += font->Glyphs[idx].AdvanceX;
        }
        out_size[0] = width    * font->FontScale;
        out_size[1] = font->LineHeight * font->FontScale;
    }

} // namespace ZEngine::UI
