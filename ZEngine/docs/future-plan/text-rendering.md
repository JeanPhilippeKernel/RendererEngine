# ZEngine — Text Rendering

**Priority:** P1 — Required for any UI, HUD, or localized text
**Status:** Design
**Depends on:** `vfs-design.md` (Ticket 1), `render-resource-manager.md`, `ui-system.md`
**Blocks:** `ui-system.md` (labels, buttons), localization
**Approach:** SDF (Signed Distance Field) font atlas via msdf-atlas-gen, rendered as textured quads

---

## 1. Why SDF Fonts

Traditional bitmap font atlases rasterize each glyph at a fixed point size and store the
resulting greyscale coverage map. They look crisp at exactly the baked size, but at larger
sizes the bilinear-interpolated texels produce a blurry, anti-aliased smear; at smaller
sizes the glyphs alias. A separate atlas must be baked for every size used at runtime.

Vector font rendering (FreeType path tessellation, NV_path_rendering) solves the
resolution problem but evaluates Bezier curves per glyph per frame. At 60 fps with
hundreds of visible glyphs, the per-pixel fillrate cost is prohibitive on mobile and
mid-range GPUs. It also requires driver extensions or compute-heavy algorithms (loop-blinn,
stencil-then-cover) that complicate the render graph.

**MSDF (Multi-channel Signed Distance Field)** stores a precomputed signed-distance-to-edge
value in each texel. The fragment shader evaluates a single texture sample, computes the
median of three channels, and applies an `fwidth`-scaled threshold to produce
anti-aliased coverage. The result is:

| Property | Bitmap atlas | Vector (FreeType) | MSDF atlas |
|---|---|---|---|
| Resolution independence | No — blurs at 2x | Yes | Yes |
| Sharp corners | Yes (at baked size) | Yes | Yes (multi-channel encodes corners) |
| Runtime cost | Texture sample | Bezier evaluation per glyph | Single texture sample + median |
| Atlas size | One per point size | No atlas needed | One per font family |
| Anti-aliasing | MSAA or blur | Analytical | `fwidth`-based, hardware-accelerated |
| Outline support | Separate bake | Free | Second threshold, free |

ZEngine uses MSDF for all text: one atlas per font family covers any point size from 8pt
to 96pt without visible degradation.

---

## 2. Offline Atlas Generation

### Tool: msdf-atlas-gen

`msdf-atlas-gen` (MIT license, https://github.com/Chlumsky/msdf-atlas-gen) is a
command-line tool that accepts a TTF or OTF file and produces:

1. A PNG atlas image containing the multichannel SDF glyphs packed into a power-of-two
   texture (typically 1024×1024 or 2048×2048 for Latin + common punctuation).
2. A JSON file containing per-glyph metrics: Unicode codepoint, advance width, plane
   bounds (in em units), atlas bounds (in texel UV coordinates), and font-level metrics
   (ascender, descender, line height).

**Recommended invocation** (part of the cook pipeline — see Section 11):

```
msdf-atlas-gen \
  -font assets/fonts/Inter-Regular.ttf \
  -type mtsdf \
  -format png \
  -imageout build/cook/Inter-Regular.png \
  -json build/cook/Inter-Regular.json \
  -size 48 \
  -pxrange 4 \
  -charset engine/tools/charset-latin-extended.txt
```

Flags:
- `-type mtsdf` — generates a 4-channel atlas (RGB = MSDF + A = greyscale SDF for
  small-size fallback). ZEngine's shader uses the RGB median only.
- `-size 48` — SDF is baked at 48px per em. The shader scales to any runtime size.
- `-pxrange 4` — 4-pixel distance range. Controls the maximum displayable stroke width.
  Increase to 6–8 if outlines wider than 2px are needed.
- `-charset` — a text file listing Unicode codepoints or ranges to include. The engine
  ships `charset-latin-extended.txt` (U+0020–U+02FF) and `charset-cjk-common.txt`
  (v2, separate atlas).

### .zatlas asset format

The cook pipeline (see Section 11) consumes the PNG + JSON pair and produces a single
`.zatlas` binary. The `.zatlas` format is engine-defined and has two sections:

```
[ zatlas_header_t  ]   fixed-size binary struct (glyph metrics, font metrics)
[ atlas_pixels     ]   BC4-compressed GPU texture data (if pre-compressed) or raw RGBA8
```

The JSON metrics are parsed offline and baked into `zatlas_header_t`. There is no JSON
parsing at runtime. The atlas pixel data is submitted to `RenderResourceManager::UploadTexture`
at load time.

---

## 3. Font Asset Format

```cpp
// ZEngine/Text/FontAsset.h
#pragma once
#include <Core/Containers/Array.h>
#include <Core/Containers/UnorderedHashMap.h>
#include <uuids/uuid.h>

namespace ZEngine::Text {

    /// One record per glyph stored in the atlas.
    struct GlyphMetric {
        uint32_t Codepoint;   // Unicode codepoint (UTF-32 scalar)

        // Horizontal layout metrics, in em units (divide by font_size to get pixels).
        float AdvanceX;       // pen advance after drawing this glyph
        float BearingX;       // horizontal offset from pen origin to glyph left edge
        float BearingY;       // vertical offset from baseline to glyph top edge

        // Glyph dimensions in em units.
        float Width;
        float Height;

        // Atlas UV coordinates [0,1] within the atlas texture.
        float AtlasU0, AtlasV0;  // top-left
        float AtlasU1, AtlasV1;  // bottom-right
    };

    /// Top-level font asset. One instance per loaded font family/style.
    struct FontAsset {
        uuids::uuid UUID;

        /// GPU texture handle (from RenderResourceManager). Owns no GPU memory directly;
        /// the RRM owns the VkImage and VmaAllocation.
        uint32_t AtlasTextureHandle;

        /// Atlas dimensions in texels, used for manual UV calculations.
        float AtlasWidth;
        float AtlasHeight;

        /// Font-level metrics in em units. Multiply by (font_size / em_size) to get pixels.
        float LineHeight;   // distance between consecutive baselines
        float Ascender;     // distance from baseline to top of tallest glyph
        float Descender;    // distance from baseline to bottom of deepest glyph (negative)

        /// All glyphs present in the atlas, stored contiguously. Not sorted.
        Core::Containers::Array<GlyphMetric> Glyphs;

        /// Codepoint → index into Glyphs array.
        /// Key:   Unicode codepoint (uint32_t)
        /// Value: index into Glyphs (uint32_t)
        Core::Containers::UnorderedHashMap<uint32_t, uint32_t> CodepointToGlyph;

        /// Kerning pairs.
        /// Key:   (left_codepoint << 32) | right_codepoint
        /// Value: kern adjustment in em units (positive = move right, negative = tighten)
        Core::Containers::UnorderedHashMap<uint64_t, float> KerningTable;
    };

}  // namespace ZEngine::Text
```

**Design notes:**

- `AtlasTextureHandle` is a raw `uint32_t` matching the `ImageHandle::Index` convention
  used by `RenderResourceManager`. The `FontManager` does not store a `RenderHandle<ImageTag>`
  directly to avoid a circular header dependency between `Text/` and `Rendering/`.
- All metrics are in **em units** (normalized to the font's coordinate space). The layout
  system scales by `font_size / em_size` at layout time. This means one `FontAsset` serves
  all runtime point sizes.
- The `KerningTable` key packs two `uint32_t` codepoints into a `uint64_t`. No struct is
  needed; the key is computed inline:
  ```cpp
  uint64_t key = (static_cast<uint64_t>(left_cp) << 32) | static_cast<uint64_t>(right_cp);
  ```
- `Glyphs`, `CodepointToGlyph`, and `KerningTable` are allocated from the same
  `ArenaAllocator` passed to `FontManager::Initialize`. They are never individually freed;
  the arena is released when the `FontManager` shuts down.

---

## 4. FontManager

```cpp
// ZEngine/Text/FontManager.h
#pragma once
#include <Core/Memory/ArenaAllocator.h>
#include <Core/Containers/Array.h>
#include <Text/FontAsset.h>
#include <VFS/IVFSContext.h>
#include <VFS/VFSPath.h>
#include <VFS/VFSResult.h>
#include <Rendering/RenderResourceManager.h>

namespace ZEngine::Text {

    /// Opaque integer handle. 0 = invalid.
    using FontHandle = uint32_t;
    constexpr FontHandle INVALID_FONT_HANDLE = 0;

    class FontManager {
    public:
        FontManager() = default;
        ~FontManager();

        // Not copyable or movable — owns arena allocations.
        FontManager(const FontManager&)            = delete;
        FontManager& operator=(const FontManager&) = delete;

        /// Must be called once before any other method.
        /// arena      — permanent arena; all FontAsset data lives here for the session.
        /// vfs        — used to open .zatlas files.
        /// rrm        — used to upload atlas textures.
        void Initialize(Core::Memory::ArenaAllocator* arena,
                        VFS::IVFSContext*              vfs,
                        Rendering::RenderResourceManager* rrm);

        /// Load a .zatlas file from the VFS.
        /// Returns a FontHandle on success; INVALID_FONT_HANDLE + error on failure.
        /// Caches by path: loading the same path twice returns the same handle.
        VFS::VFSResult<FontHandle> LoadFont(const VFS::VFSPath& atlas_path);

        /// Retrieve a loaded font. Returns nullptr if handle is invalid.
        const FontAsset* GetFont(FontHandle handle) const;

        /// Returns the number of loaded fonts.
        uint32_t GetFontCount() const;

        /// Release all GPU resources and invalidate all handles.
        /// Called during engine shutdown. After this call, all handles are invalid.
        void Shutdown();

    private:
        /// Parse a .zatlas binary from memory into a FontAsset stored in the arena.
        /// Returns false on malformed data; writes error message to ZEngine log.
        bool ParseZAtlas(const uint8_t* data,
                         uint64_t       size,
                         FontAsset*     out_asset);

        Core::Memory::ArenaAllocator*          m_arena  = nullptr;
        VFS::IVFSContext*                      m_vfs    = nullptr;
        Rendering::RenderResourceManager*      m_rrm    = nullptr;

        /// Font pool. Index 0 is unused (INVALID_FONT_HANDLE sentinel).
        /// FontHandle N → m_fonts[N - 1].
        Core::Containers::Array<FontAsset>     m_fonts;

        /// Path cache: avoids loading the same atlas twice.
        /// Key: VFSPath hash (uint64_t); Value: FontHandle.
        Core::Containers::UnorderedHashMap<uint64_t, FontHandle> m_path_to_handle;

        bool m_initialized = false;
    };

}  // namespace ZEngine::Text
```

**Design notes:**

- `FontHandle` is a 1-based index into `m_fonts`. Handle 0 is permanently invalid.
  `GetFont(h)` returns `&m_fonts[h - 1]` after a bounds check.
- `m_fonts` is grown once per `LoadFont` call via `Array::PushBack`. In practice a game
  loads 3–10 fonts at startup; the array never exceeds a few dozen entries.
- `ParseZAtlas` reads the fixed-size header section, fills `FontAsset` fields, then
  iterates the glyph table and inserts into `CodepointToGlyph`. All insertions use the
  arena allocator passed at initialize time — no `new`/`delete`.
- The path cache key is the FNV-1a 64-bit hash of the `VFSPath` string. Collision
  probability at <1024 distinct font paths is negligible (birthday bound << 2^-32).
- Thread safety: `LoadFont` is intended to be called from the main thread during
  scene load. No locking is provided. If async loading is needed, serialize calls via
  the engine's asset-loading job system.

---

## 5. Text Layout

```cpp
// ZEngine/Text/TextLayout.h
#pragma once
#include <Core/Containers/Array.h>
#include <Core/Memory/ArenaAllocator.h>
#include <Core/Maths/Vec2f.h>
#include <Core/Maths/Vec4f.h>
#include <Text/FontManager.h>

namespace ZEngine::Text {

    /// One renderable quad per visible glyph.
    struct TextGlyph {
        float X, Y;           // top-left screen position in pixels
        float Width, Height;  // quad dimensions in pixels
        float U0, V0;         // atlas UV top-left
        float U1, V1;         // atlas UV bottom-right
        Core::Maths::Vec4f Color; // RGBA, linear space
    };

    class TextLayout {
    public:
        /// Measure the bounding box of utf8_text rendered at font_size pixels.
        /// max_width == 0.0f means no wrapping.
        /// Returns Vec2f(total_width, total_height) in pixels.
        static Core::Maths::Vec2f Measure(
            const char*  utf8_text,
            FontHandle   font_handle,
            float        font_size,
            float        max_width,
            const FontManager& font_manager);

        /// Build a flat array of TextGlyph quads for utf8_text.
        /// out_glyphs is appended to (not cleared). Caller allocates the Array from
        /// a frame arena so it is automatically freed at frame end.
        ///
        /// origin     — top-left pixel position of the text block
        /// color      — uniform color applied to all glyphs
        /// max_width  — line wrap threshold in pixels; 0 = no wrap
        ///
        /// Returns the bounding box (same as Measure) so the caller can position
        /// subsequent elements without calling Measure separately.
        static Core::Maths::Vec2f Build(
            Core::Memory::ArenaAllocator*   frame_arena,
            const char*                     utf8_text,
            FontHandle                      font_handle,
            float                           font_size,
            float                           max_width,
            Core::Maths::Vec2f              origin,
            Core::Maths::Vec4f              color,
            Core::Containers::Array<TextGlyph>& out_glyphs,
            const FontManager&              font_manager);

    private:
        // Advances *p past one UTF-8 codepoint and returns the codepoint value.
        // `end` is one-past the last valid byte.
        //
        // On malformed input (invalid byte sequence): returns U+FFFD (replacement character).
        // GUARANTEE: *p ALWAYS advances by at least 1 byte, even on malformed input.
        // This prevents infinite loops in the layout engine when fed corrupt text.
        //
        // On *p == end: returns 0 (null terminator sentinel) without advancing.
        [[nodiscard]] static uint32_t NextCodepoint(const char** p, const char* end);

        /// Look up kerning between left_cp and right_cp from the font's KerningTable.
        /// Returns 0.0f if no kerning pair exists.
        static float GetKerning(const FontAsset* font,
                                uint32_t left_cp,
                                uint32_t right_cp);
    };

}  // namespace ZEngine::Text
```

### Layout algorithm (Build)

```cpp
// Pseudocode for TextLayout::Build — actual implementation in TextLayout.cpp

Vec2f TextLayout::Build(
    ArenaAllocator* frame_arena,
    const char*     utf8_text,
    FontHandle      font_handle,
    float           font_size,
    float           max_width,
    Vec2f           origin,
    Vec4f           color,
    Array<TextGlyph>& out_glyphs,
    const FontManager& font_manager)
{
    const FontAsset* font = font_manager.GetFont(font_handle);
    ZENGINE_VALIDATE_ASSERT(font != nullptr, "Invalid FontHandle passed to TextLayout::Build")

    // Scale factor: convert em units to pixels.
    // em_size is always 1.0 in msdf-atlas-gen output (metrics are in em units directly).
    const float scale = font_size;

    float cursor_x    = origin.X;
    float cursor_y    = origin.Y + font->Ascender * scale;
    float line_start_x = origin.X;
    float max_x_seen  = origin.X;
    float max_y_seen  = cursor_y;

    const char* p          = utf8_text;
    uint32_t    prev_cp    = 0;

    while (*p != '\0') {
        uint32_t cp = NextCodepoint(&p);

        // Newline
        if (cp == '\n') {
            if (cursor_x > max_x_seen) max_x_seen = cursor_x;
            cursor_x  = line_start_x;
            cursor_y += font->LineHeight * scale;
            prev_cp   = 0;
            continue;
        }

        // Skip control characters (except tab, handled below)
        if (cp < 0x20 && cp != '\t') { prev_cp = cp; continue; }

        // Tab: advance to next 4-glyph-width boundary
        if (cp == '\t') {
            const float space_adv = GetAdvanceX(font, ' ') * scale;
            cursor_x = line_start_x +
                       (std::floor((cursor_x - line_start_x) / (space_adv * 4.0f)) + 1.0f)
                       * (space_adv * 4.0f);
            prev_cp = cp;
            continue;
        }

        // Look up glyph
        auto it = font->CodepointToGlyph.Find(cp);
        if (!it) {
            // Fallback: try replacement character U+FFFD, then skip
            it = font->CodepointToGlyph.Find(0xFFFDu);
            if (!it) { prev_cp = cp; continue; }
        }
        const GlyphMetric& glyph = font->Glyphs[it->Value];

        // Apply kerning from previous glyph
        if (prev_cp != 0) {
            cursor_x += GetKerning(font, prev_cp, cp) * scale;
        }

        // Word-wrap: if this glyph would exceed max_width, break the line.
        // (Simple greedy wrap — no hyphenation in v1.)
        //
        // **Word overflow handling:** If a single word exceeds `max_width`, it cannot be placed
        // on a new line (it would still overflow). In this case, break the word mid-glyph: advance
        // glyphs until the line is full, then break and continue on the next line. This prevents
        // the layout from entering an infinite loop. Add a `kWordBreakMaxWidth` constant that
        // triggers character-level breaking when `word_width > max_width`.
        if (max_width > 0.0f) {
            float glyph_right = cursor_x + (glyph.BearingX + glyph.Width) * scale;
            if (glyph_right > origin.X + max_width && cursor_x > line_start_x) {
                if (cursor_x > max_x_seen) max_x_seen = cursor_x;
                cursor_x  = line_start_x;
                cursor_y += font->LineHeight * scale;
            }
        }

        // Emit TextGlyph quad
        if (glyph.Width > 0.0f && glyph.Height > 0.0f) {
            TextGlyph tg{};
            tg.X      = cursor_x + glyph.BearingX  * scale;
            tg.Y      = cursor_y - glyph.BearingY  * scale;
            tg.Width  = glyph.Width  * scale;
            tg.Height = glyph.Height * scale;
            tg.U0     = glyph.AtlasU0;
            tg.V0     = glyph.AtlasV0;
            tg.U1     = glyph.AtlasU1;
            tg.V1     = glyph.AtlasV1;
            tg.Color  = color;
            out_glyphs.PushBack(tg);  // frame_arena backs the Array storage
        }

        cursor_x += glyph.AdvanceX * scale;
        if (cursor_x > max_x_seen) max_x_seen = cursor_x;

        float bottom = cursor_y + font->Descender * scale;
        if (bottom > max_y_seen) max_y_seen = bottom;

        prev_cp = cp;
    }

    return Vec2f(max_x_seen - origin.X,
                 max_y_seen - origin.Y + (-font->Descender) * scale);
}
```

**Design notes:**

- `NextCodepoint` is a standalone static function that handles all four UTF-8 byte-length
  cases (1–4 bytes). It never allocates; it advances the pointer in-place.
  Every error/malformed path must end with:
  ```cpp
  if (*p == start_of_this_call) { ++(*p); }  // guarantee progress
  ```
  This ensures the layout loop always terminates, even on corrupt UTF-8 input.
- There is no dynamic allocation inside `Build`. `out_glyphs` is backed by a frame arena
  that the caller provides; `Array::PushBack` calls `ArenaAllocator::Alloc` under the hood
  when the backing block is exhausted.
- `Measure` reuses `Build` with a null output array (or simply runs the same cursor logic
  without emitting quads). It is not a separate code path.
- Word wrap is greedy (break before the first overflowing glyph). A full line-breaking
  algorithm (Unicode UAX #14) is v2.
- Right-to-left (Arabic, Hebrew) is v2. Bidirectional text (Unicode UBA) is v2.

---

## 6. SDF Shader

### Vertex shader

```glsl
// ZEngine/Shaders/text.vert
#version 450

layout(location = 0) in vec2  inPosition;  // screen-space pixel coords
layout(location = 1) in vec2  inUV;
layout(location = 2) in vec4  inColor;

layout(location = 0) out vec2  fragUV;
layout(location = 1) out vec4  fragColor;

// Push constant: orthographic projection matrix (column-major)
layout(push_constant) uniform PushConstants {
    mat4 uProjection;
};

void main() {
    gl_Position = uProjection * vec4(inPosition, 0.0, 1.0);
    fragUV      = inUV;
    fragColor   = inColor;
}
```

### Fragment shader

```glsl
// ZEngine/Shaders/text.frag
#version 450

layout(location = 0) in  vec2  fragUV;
layout(location = 1) in  vec4  fragColor;
layout(location = 0) out vec4  outColor;

layout(set = 0, binding = 0) uniform sampler2D uAtlas;

// Push constant (same block as vertex; Vulkan requires identical block layout)
layout(push_constant) uniform PushConstants {
    mat4  uProjection;
    vec4  uOutlineColor;   // RGBA; outline is drawn when uOutlineWidth > 0
    float uOutlineWidth;   // SDF edge offset for outline (0 = no outline)
};

// Median of three values — the key to MSDF robustness.
// The three SDF channels each encode a signed distance resolved from
// a different pair of the glyph's outline edges. Taking the median
// rather than a single channel eliminates the cross-channel artefacts
// that appear on curved corners when using a single-channel SDF.
float Median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

void main() {
    // Sample the multichannel SDF atlas.
    vec3 msd = texture(uAtlas, fragUV).rgb;

    // Compute the signed distance to the glyph edge from the median channel.
    // Values > 0.5 are inside the glyph; < 0.5 are outside.
    float sd = Median(msd.r, msd.g, msd.b);

    // fwidth gives the magnitude of the screen-space derivative.
    // Dividing by fwidth converts the signed distance into a number of
    // screen pixels, giving hardware-accurate anti-aliasing that scales
    // correctly at any display resolution.
    float d  = sd - 0.5;
    float w  = fwidth(sd);

    // Smooth step over one pixel: alpha goes from 0 to 1 across the edge.
    float fill_alpha = clamp(d / w + 0.5, 0.0, 1.0);

    // Outline: a second threshold offset inward from the fill edge.
    // When uOutlineWidth == 0, outline_alpha == 0 and the branch never fires.
    float outline_sd    = sd - (0.5 - uOutlineWidth);
    float outline_alpha = clamp(outline_sd / w + 0.5, 0.0, 1.0)
                        * (1.0 - fill_alpha);

    // Composite: fill over outline.
    vec4 fill_color    = fragColor * fill_alpha;
    vec4 outline_color = uOutlineColor * outline_alpha;
    outColor = fill_color + outline_color * (1.0 - fill_alpha);

    // Premultiplied alpha output: the render pass blends using ONE / ONE_MINUS_SRC_ALPHA.
    outColor.rgb *= outColor.a;
}
```

**Design notes:**

- `fwidth(sd)` is `abs(dFdx(sd)) + abs(dFdy(sd))`. It provides the exact pixel-to-texel
  ratio at the current screen position, so anti-aliasing is correct at any render
  resolution including supersampled or downsampled targets.
- The push constant block must be declared identically in both shaders. Only the vertex
  shader uses `uProjection`; only the fragment shader uses `uOutlineColor` and
  `uOutlineWidth`. Both see the same 80-byte block.
- The fragment shader does not branch on `uOutlineWidth`. The multiplication by
  `(1.0 - fill_alpha)` makes the outline mathematically zero when `fill_alpha == 1.0`
  or when `uOutlineWidth == 0.0`. GPUs optimize this with predication.
- For shadow effects: add a third threshold (e.g. `sd - 0.3`) with a shadow color and
  an offset to the UV before sampling. This is a v2 feature.
- The alpha-blending state must be `VK_BLEND_FACTOR_ONE` / `VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA`
  (premultiplied) to match the premultiplied output above.

---

## 7. Text Render Pass

```cpp
// ZEngine/Text/TextRenderPass.h
#pragma once
#include <Rendering/RenderGraph.h>
#include <Rendering/RenderResourceManager.h>
#include <Text/TextLayout.h>
#include <Core/Containers/Array.h>
#include <Core/Memory/ArenaAllocator.h>

namespace ZEngine::Text {

    /// Vertex layout for text quads. Must match text.vert attribute locations.
    struct TextVertex {
        float X, Y;     // screen-space pixel position
        float U, V;     // atlas UV
        float R, G, B, A; // color (linear RGBA)
    };

    class TextRenderPass : public Rendering::IRenderGraphCallbackPass {
    public:
        TextRenderPass() = default;

        /// One-time initialization. Called after Vulkan device is ready.
        void Initialize(Core::Memory::ArenaAllocator* arena,
                        Rendering::RenderResourceManager* rrm,
                        FontManager* font_manager,
                        uint32_t screen_width,
                        uint32_t screen_height);

        /// Called when swapchain is resized.
        void OnResize(uint32_t screen_width, uint32_t screen_height);

        /// Submit a batch of glyphs from a UIContext or any other source.
        /// All calls must arrive before Execute() is invoked this frame.
        void SubmitGlyphs(const Core::Containers::Array<TextGlyph>& glyphs,
                          uint32_t atlas_texture_handle);

        // --- IRenderGraphCallbackPass interface ---

        /// Called by RenderGraph during pass setup. Declares color attachment (no depth).
        void Setup(Rendering::RenderGraphBuilder& builder) override;

        /// Called by RenderGraph during execution.
        /// Builds vertex buffer, issues draw calls, resets submission list.
        void Execute(Rendering::RenderGraphContext& context,
                     VkCommandBuffer               cmd) override;

        void Shutdown();

    private:
        /// Build an orthographic projection matrix for [0, screen_w] x [0, screen_h].
        /// Depth range [0, 1]. Y points down (screen convention).
        void UpdateProjection();

        /// Upload TextVertex data to the per-frame vertex buffer via staging.
        void UploadVertices(VkCommandBuffer cmd,
                            const TextVertex* vertices,
                            uint32_t vertex_count);

        Core::Memory::ArenaAllocator*       m_arena        = nullptr;
        Rendering::RenderResourceManager*   m_rrm          = nullptr;
        FontManager*                        m_font_manager = nullptr;

        uint32_t m_screen_width  = 0;
        uint32_t m_screen_height = 0;

        /// Projection matrix (column-major, Vulkan NDC).
        float m_projection[16] = {};

        /// Per-atlas batch: one draw call per distinct font atlas.
        struct GlyphBatch {
            uint32_t                           AtlasTextureHandle;
            Core::Containers::Array<TextGlyph> Glyphs;
        };
        Core::Containers::Array<GlyphBatch> m_batches;

        /// Per-frame dynamic vertex buffer (CPU-writable, GPU-readable).
        /// Sized for MAX_TEXT_VERTICES vertices; resized if exceeded.
        static constexpr uint32_t MAX_TEXT_VERTICES = 65536 * 4;  // 64K glyphs
        VkBuffer      m_vertex_buffer     = VK_NULL_HANDLE;
        VmaAllocation m_vertex_allocation = VK_NULL_HANDLE;
        void*         m_vertex_mapped     = nullptr;  // persistently mapped

        /// Pipeline and descriptor state.
        VkPipeline            m_pipeline             = VK_NULL_HANDLE;
        VkPipelineLayout      m_pipeline_layout      = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_descriptor_set_layout = VK_NULL_HANDLE;
        VkDescriptorPool      m_descriptor_pool       = VK_NULL_HANDLE;
        VkSampler             m_sampler               = VK_NULL_HANDLE;
    };

}  // namespace ZEngine::Text
```

**Execution flow within Execute():**

1. Iterate `m_batches`. For each batch:
   a. Convert `TextGlyph` array to `TextVertex` array (4 vertices per glyph, triangle strip
      or indexed quads with a shared index buffer `{0,1,2, 2,3,0}`).
   b. Write into the persistently-mapped `m_vertex_buffer` at the current write offset.
2. Bind the graphics pipeline (`m_pipeline`).
3. Bind the vertex buffer.
4. Set push constant: `uProjection` (64 bytes) + `uOutlineColor` + `uOutlineWidth`.
5. For each batch, bind the atlas descriptor set and call `vkCmdDraw`.
6. Clear `m_batches` so the next frame starts clean.

**Alpha blending state:**

```
srcColorBlendFactor  = VK_BLEND_FACTOR_ONE               // premultiplied
dstColorBlendFactor  = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
colorBlendOp         = VK_BLEND_OP_ADD
srcAlphaBlendFactor  = VK_BLEND_FACTOR_ONE
dstAlphaBlendFactor  = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
depthWriteEnable     = VK_FALSE  // text never writes depth
depthTestEnable      = VK_FALSE  // text always appears on top
```

**Render order:** The `TextRenderPass` runs after the main opaque and transparent geometry
passes. In the `RenderGraph`, it is registered as the last pass that writes to the
swapchain color attachment.

---

## 8. Localization Integration

```cpp
// ZEngine/Localization/LocalizationManager.h
#pragma once
#include <Core/Containers/UnorderedHashMap.h>
#include <Core/Containers/String.h>
#include <Core/Memory/ArenaAllocator.h>
#include <VFS/IVFSContext.h>
#include <VFS/VFSPath.h>
#include <VFS/VFSResult.h>

namespace ZEngine::Localization {

    /// FNV-1a 64-bit hash of a null-terminated string key.
    using StringHash = uint64_t;

    StringHash HashKey(const char* key);

    class LocalizationManager {
    public:
        LocalizationManager() = default;

        void Initialize(Core::Memory::ArenaAllocator* arena,
                        VFS::IVFSContext*              vfs);

        /// Load a CSV string table for the given language code.
        /// CSV format (UTF-8, no BOM):
        ///   key,value
        ///   ui.button.ok,"OK"
        ///   ui.button.cancel,"Cancel"
        ///
        /// Lines beginning with '#' are comments and are skipped.
        /// Values may be double-quoted; embedded commas and newlines inside quotes
        /// are supported. Escape sequence: "" inside a quoted value = literal ".
        ///
        /// If a table for language_code is already loaded, it is replaced.
        /// Returns VFSResult<void> so the caller can surface file-not-found errors.
        VFS::VFSResult<void> LoadStringTable(const VFS::VFSPath& csv_path,
                                             const char*         language_code);

        /// Return the translated string for key in the active language.
        /// Falls back to the key itself if not found (never returns nullptr).
        const char* Get(const char* key) const;

        /// Hot-swap the active language.
        /// If language_code has not been loaded yet, this is a no-op and logs a warning.
        void SetLanguage(const char* language_code);

        /// Return the currently active language code ("en", "fr", "ja", etc.).
        const char* GetActiveLanguage() const;

        void Shutdown();

    private:
        /// One parsed string table.
        struct StringTable {
            char Language[8];  // null-terminated language code (e.g. "en", "zh-CN")
            Core::Containers::UnorderedHashMap<StringHash, Core::Containers::String> Entries;
        };

        Core::Memory::ArenaAllocator* m_arena = nullptr;
        VFS::IVFSContext*             m_vfs   = nullptr;

        /// All loaded language tables.
        Core::Containers::Array<StringTable> m_tables;

        /// Index into m_tables for the active language.
        uint32_t m_active_index = UINT32_MAX;
    };

}  // namespace ZEngine::Localization
```

**Design notes:**

- String values are stored as `Core::Containers::String` (engine string, no `std::string`)
  allocated from the same arena passed at `Initialize`. They persist for the session.
- `Get` is `O(1)`: `HashKey(key)` then `UnorderedHashMap::Find`. No string comparison
  in the hot path.
- `SetLanguage` is an O(N) linear scan over `m_tables` (N ≤ 10 in practice). A secondary
  `UnorderedHashMap<StringHash, uint32_t>` could accelerate this but is overkill.
- `LoadStringTable` re-parses the CSV using a hand-rolled parser that allocates from the
  arena. No `std::istringstream`. The parser is state-machine based: states are
  `IN_KEY`, `IN_VALUE`, `IN_QUOTED_VALUE`, `ESCAPE`.
- Hot language swap at runtime (e.g. from the settings menu) calls `SetLanguage` followed
  by a UI rebuild event. The localization manager does not own any UI handles; it is the
  UI system's responsibility to re-query `Get()` on the next frame.
- **Pluralization** (e.g. English "1 item" vs "2 items"): v2. A simple plural form lookup
  table per language is sufficient for Western languages; CLDR plural rules are v3.

---

## 9. Supported Languages and Unicode

**v1 scope:**

- UTF-8 only. All internal strings are UTF-8 null-terminated `const char*`.
- Latin, Latin Extended (U+0000–U+02FF): included in the default charset, fits in a
  1024×1024 atlas at 48px/em, pxrange 4.
- Cyrillic (U+0400–U+04FF): included in the extended charset, no atlas size increase.
- Greek (U+0370–U+03FF): included in the extended charset.
- Common punctuation and symbols (U+2000–U+206F, U+20A0–U+20CF): included.

**v2 scope (not in this design doc):**

- **CJK Unified Ideographs** (U+4E00–U+9FFF, 20,902 characters): requires an 8192×8192
  atlas at 48px/em. The engine will support a separate `cjk.zatlas` asset loaded on
  demand. The `TextRenderPass` must handle multi-atlas rendering without a full rewrite
  (batch by atlas handle already supports this).
- **Arabic / Hebrew (RTL):** Requires Unicode Bidirectional Algorithm (UBA, UAX #9),
  shaping (Harfbuzz or a lighter shaper), and per-line text direction detection. The
  layout cursor must advance right-to-left. This is non-trivial and is deferred.
- **Emoji** (U+1F600–U+1FFFF): requires a color atlas (RGBA8, no SDF). A separate
  `emoji.zatlas` with a color-atlas shader variant is the approach.
- **Dynamic atlas** for rare codepoints: generate SDF tiles at runtime with FreeType +
  msdfgen and blit them into a texture array. Deferred to v3.

---

## 10. File Layout

```
ZEngine/
  Text/
    FontAsset.h          — GlyphMetric, FontAsset structs
    FontManager.h
    FontManager.cpp
    TextLayout.h
    TextLayout.cpp       — UTF-8 decoder, layout algorithm, kerning lookup
    TextRenderPass.h
    TextRenderPass.cpp   — IRenderGraphCallbackPass implementation, Vulkan pipeline setup

  Localization/
    LocalizationManager.h
    LocalizationManager.cpp  — CSV parser, string table, Get/SetLanguage

  Shaders/
    text.vert
    text.frag

tests/
  Text/
    TextLayoutTest.cpp   — layout, wrapping, UTF-8 decoding, kerning
    LocalizationTest.cpp — CSV parsing, Get, SetLanguage, fallback key
```

---

## 11. Cook Pipeline Integration

The text asset cook step is a single entry in the cook pipeline (see `cook-pipeline.md`):

```
Input:  assets/fonts/<FamilyName>-<Style>.ttf   (tracked by VFS meta/UUID)
Output: build/cook/<FamilyName>-<Style>.zatlas
```

### Step 1: Atlas generation (msdf-atlas-gen)

Run `msdf-atlas-gen` as a subprocess from the cook tool:

```cpp
// ZEngine/Tools/Cook/FontCooker.cpp (pseudocode)
CookResult FontCooker::Cook(const CookTarget& target, const AssetRecord& record)
{
    // Invoke msdf-atlas-gen CLI
    String cmd;
    cmd.Format(
        "msdf-atlas-gen -font {} -type mtsdf -format png "
        "-imageout {}.tmp.png -json {}.tmp.json "
        "-size 48 -pxrange 4 -charset {}",
        record.SourcePath,
        target.OutputPath, target.OutputPath,
        GetCharsetPath(target.Platform));

    int exit_code = Platform::Shell::Execute(cmd.CStr());
    ZENGINE_VALIDATE_ASSERT(exit_code == 0, "msdf-atlas-gen failed")
    // ...
}
```

### Step 2: Texture compression

The atlas PNG is compressed to **BC4** (single-channel, but we use RGBA8 for the
multichannel SDF — so use **BC3** or **ASTC 4x4** on mobile):

| Platform | Format | Rationale |
|---|---|---|
| PC (Vulkan, desktop) | BC3 (DXT5) | 4:1 compression, hardware decompression |
| Mobile (Vulkan, Android) | ASTC 4×4 | Better quality/ratio, universal on Mali/Adreno/Apple |
| Fallback | RGBA8 uncompressed | Always available |

Compression is performed by `ispc_texcomp` or `basisu` (both available as cook-time tools).

### Step 3: .zatlas binary assembly

```
[8 bytes]  magic: "ZATLAS01"
// Version increment policy:
// - Increment version when GlyphMetric field layout changes (add/remove/reorder fields)
// - Increment version when KerningTable key encoding changes
// - Increment version when FontAsset header layout changes
// - Runtime parser must REJECT atlases with mismatched version:
//     ZENGINE_VALIDATE_ASSERT(header.version == ZATLAS_VERSION, "Atlas version mismatch — re-cook assets")
// - Never deserialize atlas data from an older version without a migration path
[4 bytes]  version: uint32_t = 1
[4 bytes]  glyph_count: uint32_t
[4 bytes]  kerning_count: uint32_t
[4 bytes]  atlas_width: uint32_t (texels)
[4 bytes]  atlas_height: uint32_t
[4 bytes]  atlas_format: uint32_t (VkFormat enum value)
[4 bytes]  atlas_data_size: uint32_t (bytes)
[4 bytes]  em_size: float
[4 bytes]  line_height: float (em units)
[4 bytes]  ascender: float (em units)
[4 bytes]  descender: float (em units)
[16 bytes] reserved (zero)
--- glyph table ---
[glyph_count * sizeof(GlyphMetric)] GlyphMetric records, packed, no padding
--- kerning table ---
[kerning_count * 12 bytes] {uint64_t key, float value} records
--- atlas pixel data ---
[atlas_data_size bytes] GPU-ready compressed texture data
```

The runtime parser (`FontManager::ParseZAtlas`) reads this sequentially with no seeking.
No heap allocation during parsing — all data is copied into the arena.

### SHA256 incremental gate

The cook pipeline gates the font cook step behind a SHA256 hash of the TTF source file
(same mechanism as all other cook steps). Re-cooking is triggered only when:
- The TTF file's SHA256 changes.
- The charset file changes.
- The msdf-atlas-gen version changes (stored in the cook manifest).

---

## 12. Deliverables Checklist

- [ ] `ZEngine/Text/FontAsset.h` — `GlyphMetric`, `FontAsset` structs; kerning key formula documented inline
- [ ] `ZEngine/Text/FontManager.h` + `.cpp` — `Initialize`, `LoadFont`, `GetFont`, `Shutdown`; arena-backed pool; path cache via `UnorderedHashMap<uint64_t, FontHandle>`; `ParseZAtlas` binary reader
- [ ] `ZEngine/Text/TextLayout.h` + `.cpp` — `TextGlyph` struct; `Measure`, `Build` (shared logic); `NextCodepoint` UTF-8 decoder; `GetKerning`; greedy word-wrap; tab stop support
- [ ] `ZEngine/Text/TextRenderPass.h` + `.cpp` — `IRenderGraphCallbackPass`; `SubmitGlyphs`; per-atlas batching; persistently-mapped dynamic vertex buffer; `vkCmdDraw` per atlas; alpha blending state; push constant projection
- [ ] `ZEngine/Shaders/text.vert` — screen-space orthographic transform via push constant
- [ ] `ZEngine/Shaders/text.frag` — MSDF median decode; `fwidth`-based anti-aliasing; outline via second threshold; premultiplied alpha output
- [ ] `ZEngine/Localization/LocalizationManager.h` + `.cpp` — `LoadStringTable` CSV parser; `Get`/`SetLanguage`; arena-backed `UnorderedHashMap<StringHash, String>`; fallback-to-key behavior
- [ ] `.zatlas` binary format spec (header above is authoritative)
- [ ] `ZEngine/Tools/Cook/FontCooker.cpp` — msdf-atlas-gen subprocess invocation; BC3/ASTC texture compression; `.zatlas` binary assembly; SHA256 incremental gate
- [ ] `engine/tools/charset-latin-extended.txt` — codepoint range list for default cook
- [ ] `tests/Text/TextLayoutTest.cpp` — ASCII layout, wrapping at max_width, kerning applied, UTF-8 multibyte decode, tab stops, empty string edge case
- [ ] `tests/Text/LocalizationTest.cpp` — CSV load, `Get` hit, `Get` miss returns key, `SetLanguage` hot-swap, quoted values with embedded commas
