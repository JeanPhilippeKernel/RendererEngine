# ZEngine — Post-Processing Pipeline

**Priority:** P3 — Required for visual polish; bloom, tone mapping, and color grading are standard
**Status:** Design
**Depends on:** `render-resource-manager.md`, `shader-asset-pipeline.md`, `gpu-allocator-rearchitecture.md`, `per-frame-upload-heap.md`
**Blocks:** Visual polish, final image quality

**Goal**: Implement a fully data-driven, RenderGraph-integrated post-processing stack inside
`ZEngine::Rendering::PostProcessing`. The stack reads from a full-resolution HDR color buffer
produced by the main scene pass, chains a configurable sequence of `PostProcessPass` nodes
through the `RenderGraph`, and writes a tone-mapped, LDR result to the swapchain image. Every
effect is individually toggle-able at runtime. No exceptions. No `new`/`delete` in the hot path.

---

## 1. Architecture

### Design principles

Post-processing in ZEngine is a **chain of RenderGraph passes** that read from and write to
intermediate `TextureHandle` resources tracked and aliased by the `RenderGraph`. There is no
monolithic post-processing shader; each effect is its own `PostProcessPass` node with its own
GLSL shader, its own set of input/output resources, and its own parameter buffer.

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│                            PostProcessStack                                      │
│                                                                                  │
│  HDR RT  ──►  [ SSAO ]  ──►  [ Bloom ]  ──►  [ ToneMap ]  ──►  [ LUT ]  ──►   │
│               [ Blur  ]      [ Blur   ]       [ FXAA    ]       [ Vignette ]    │
│                                                               LDR ──► Swapchain │
└──────────────────────────────────────────────────────────────────────────────────┘
```

Each `PostProcessPass` conforms to `IRenderGraphCallbackPass`. The `PostProcessStack`
owns and orders all passes, calls `RenderGraph::AddPass` for each one at `Compile` time,
and wires the output `TextureHandle` of pass N as the input of pass N+1.

### Key types

```
PostProcessStack          — owns pass list, orders, wires resources, drives Execute
PostProcessPass           — abstract base; one shader, one set of parameters, one RG pass
PostProcessPassRegistry   — maps StringHash → PostProcessPass*; enables scripted enable/disable
```

### Rendering thread contract

`PostProcessStack::Execute` is called once per frame from the render thread, after the main
scene pass command buffer has been submitted and before the swapchain present. It does not
stall the GPU — it records secondary command buffers that the `RenderGraph` schedules and
executes in the correct pipeline dependency order.

---

## 2. HDR Pipeline

### Render target layout

The main scene pass renders into a full-resolution HDR render target:

| RT slot | Format | Layout | Purpose |
|---------|--------|--------|---------|
| `hdr_color` | `VK_FORMAT_R16G16B16A16_SFLOAT` | `COLOR_ATTACHMENT_OPTIMAL` | Scene color + emissive |
| `depth` | `VK_FORMAT_D32_SFLOAT` | `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` | Scene depth |
| `gbuf_normals` | `VK_FORMAT_R16G16_SFLOAT` (octahedral) | `COLOR_ATTACHMENT_OPTIMAL` | View-space normals for SSAO |

The post-processing stack receives `hdr_color` and `depth` as read-only inputs. It does
not write into the main scene's render targets — each pass allocates a new transient
resource from the `RenderGraph` allocator.

### LDR output

The last pass in the chain writes `VK_FORMAT_R8G8B8A8_UNORM` (LDR sRGB) into the
swapchain image via a `vkCmdBlitImage` or full-screen triangle blit. The `RenderGraph`
tracks this resource as the swapchain attachment and schedules the final
`VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` transition automatically.

### Bit depth rationale

`RGBA16F` for the HDR buffer:
- Sufficient range for HDR values (up to ~65,504 nits per channel before saturation).
- 8 bytes/pixel — cheaper to read/write than `RGBA32F` with no perceptible quality loss
  for game rendering.
- Supported as a color attachment on all Vulkan-capable hardware.

---

## 3. `PostProcessStack`

### Header

```cpp
// ZEngine/Rendering/PostProcessing/PostProcessStack.h
#pragma once
#include <Core/Memory/ArenaAllocator.h>
#include <Core/Containers/Array.h>
#include <Core/Containers/UnorderedHashMap.h>
#include <Rendering/PostProcessing/PostProcessPass.h>
#include <Rendering/Graph/RenderGraph.h>
#include <Rendering/Textures/TextureHandle.h>
#include <Hardwares/VulkanDevice.h>

namespace ZEngine::Rendering::PostProcessing {

    using Core::Memory::ArenaAllocator;
    using Core::Containers::Array;
    using Core::Containers::UnorderedHashMap;
    using Rendering::Textures::TextureHandle;

    // ---------------------------------------------------------------------------
    // PostProcessStack
    //
    // Owns and orders all post-process passes. Call Initialize once at engine
    // startup, then AddPass for each effect in any order. Call Compile before
    // the first frame to sort passes and wire RenderGraph resources. Call Execute
    // once per frame from the render thread.
    // ---------------------------------------------------------------------------
    struct PostProcessStack {
        void Initialize(ArenaAllocator* arena,
                        Hardwares::VulkanDevice* device,
                        Graph::RenderGraph* render_graph);

        // Add a pass by data + vtable. No heap allocation — entry is copied
        // into m_passes (arena-backed Array). Lower Order = executes earlier.
        void AddPass(PostProcessPassData data, PostProcessPassVtable vtable);

        // Remove a previously added pass by name hash.
        void RemovePass(StringHash pass_name);

        // Enable or disable a pass by name hash without removing it.
        // A disabled pass is skipped; the ping-pong chain reads the
        // previous enabled pass's output directly.
        void SetEnabled(StringHash pass_name, bool enabled);

        // Sort passes by Order, allocate intermediate RTs, register
        // each pass's Setup+Execute with the RenderGraph.
        // Must be called after all AddPass calls and before the first Execute.
        void Compile();

        // Record all enabled passes into the RenderGraph for this frame.
        // hdr_input  — HDR scene color RT from the main geometry pass.
        // ldr_output — swapchain image TextureHandle.
        void Execute(Graph::RenderGraph* rg,
                     TextureHandle hdr_input,
                     TextureHandle ldr_output);

        TextureHandle GetLDROutput() const;

    private:
        ArenaAllocator*          m_arena        = nullptr;
        Hardwares::VulkanDevice* m_device       = nullptr;
        Graph::RenderGraph*      m_render_graph = nullptr;

        // Flat array of pass entries — sorted by Data.Order at Compile().
        // No Ref<>, no vtable per element, no heap allocation.
        Array<PostProcessPassEntry>              m_passes;
        UnorderedHashMap<StringHash, uint32_t>   m_name_to_index;
        bool                                     m_compiled = false;
    };

}  // namespace ZEngine::Rendering::PostProcessing
```

### `PostProcessPass` base type

```cpp
// ZEngine/Rendering/PostProcessing/PostProcessPass.h
#pragma once
#include <Rendering/Graph/IRenderGraphCallbackPass.h>
#include <Rendering/Graph/RenderGraphResourceBuilder.h>
#include <Rendering/Textures/TextureHandle.h>
#include <Core/Memory/ArenaAllocator.h>
#include <Hardwares/VulkanDevice.h>

namespace ZEngine::Rendering::PostProcessing {

    // -------------------------------------------------------------------------
    // PostProcessPassData — plain data describing one post-process pass.
    // No vtable. No virtual dispatch. Passed by pointer to Setup/Execute.
    // -------------------------------------------------------------------------
    struct PostProcessPassData {
        StringHash    Name;                     // stable hash for enable/disable/lookup
        TextureHandle Input  = {};              // wired by PostProcessStack::Execute
        TextureHandle Output = {};
        bool          Enabled = true;
        int           Order   = 0;
        void*         Params  = nullptr;        // arena-allocated per-pass param struct
    };

    // -------------------------------------------------------------------------
    // PostProcessPassVtable — function table replacing virtual dispatch.
    // Each pass provides two free functions; no inheritance required.
    //
    //   Setup   — declare RenderGraph resource reads/writes for this pass
    //   Execute — record Vulkan commands (full-screen triangle or compute dispatch)
    //
    // DOD rationale: the render hot path calls Setup+Execute on N passes per
    // frame. A function-table dispatch is a single indirect call with no
    // heap allocation, no RTTI, and no virtual table walk.
    // -------------------------------------------------------------------------
    using PassSetupFn   = void (*)(PostProcessPassData&,
                                   Graph::RenderGraphResourceBuilder&);
    using PassExecuteFn = void (*)(PostProcessPassData&,
                                   VkCommandBuffer,
                                   const Graph::RenderGraph&);

    struct PostProcessPassVtable {
        PassSetupFn   Setup   = nullptr;
        PassExecuteFn Execute = nullptr;
    };

    // -------------------------------------------------------------------------
    // PostProcessPassEntry — one slot in the PostProcessStack.
    // Plain data + vtable. No heap allocation. Stored in a flat Array.
    // -------------------------------------------------------------------------
    struct PostProcessPassEntry {
        PostProcessPassData   Data;
        PostProcessPassVtable Vtable;
    };

}  // namespace ZEngine::Rendering::PostProcessing
```

### Ping-pong resource strategy

`Compile` allocates two intermediate `RGBA16F` render targets (`ping` and `pong`) from the
RenderGraph transient resource pool. Passes alternate which buffer they read from and which
they write to, tracked by a `bool m_ping_active` inside `PostProcessStack`. The swapchain
blit always reads from whichever buffer was last written.

This avoids allocating one unique intermediate RT per pass — the RenderGraph's aliasing
logic recognises non-overlapping lifetimes and can physically back both `ping` and `pong`
with the same memory on hardware that supports aliased attachments.

---

## 4. Bloom Pass

### Algorithm: Dual Kawase Blur

The dual kawase algorithm produces results visually indistinguishable from a large Gaussian
blur at a fraction of the cost. It uses two shader passes — downsample and upsample —
applied iteratively on a mip chain.

#### Step-by-step

| Step | Input | Output | Resolution | Description |
|------|-------|--------|------------|-------------|
| Threshold extract | HDR color | `bloom_threshold` | 1/2 | Keep pixels above `Threshold` luminance |
| Downsample ×5 | Previous mip | Next mip | 1/4 → 1/128 | 4-tap sample with offset |
| Upsample ×5 | Smaller mip | Larger mip | 1/64 → 1/2 | Bilinear + 4-tap scatter |
| Additive blend | HDR color + `bloom_threshold` chain | HDR color | Full | `result = hdr + bloom * Intensity` |

#### Parameters

```cpp
// ZEngine/Rendering/PostProcessing/BloomPass.h

struct BloomParams {
    float Threshold  = 1.0f;   // Luminance threshold; pixels below this are black
    float Intensity  = 0.04f;  // Additive blend weight of the bloom contribution
    float Scatter    = 0.7f;   // Controls how widely the blur spreads across mip levels
    float _pad       = 0.0f;   // std140 padding
};
```

#### DOD-conformant declaration — free functions + param struct

BloomPass is not a class. It is a plain param struct + two free functions registered
via `PostProcessPassVtable`. No vtable, no inheritance, no heap allocation.

```cpp
// ZEngine/Rendering/PostProcessing/BloomPass.h
#pragma once
#include <Rendering/PostProcessing/PostProcessPass.h>

namespace ZEngine::Rendering::PostProcessing {

    static constexpr uint32_t BLOOM_MIP_LEVELS = 5;

    // Per-pass GPU data — allocated from arena, pointer stored in
    // PostProcessPassData::Params.
    struct BloomPassState {
        BloomParams             Params{};
        // ParamsUBO is NOT a persistent VkBuffer. Bloom parameters are pushed via
        // PerFrameUploadHeap::Push each frame when dirty; at steady state (no param
        // change) the heap write is skipped via a dirty flag.
        Textures::TextureHandle DownsampleChain[BLOOM_MIP_LEVELS] = {};
        Textures::TextureHandle UpsampleChain[BLOOM_MIP_LEVELS]   = {};
        VkPipeline              ThresholdPipeline  = VK_NULL_HANDLE;
        VkPipeline              DownsamplePipeline = VK_NULL_HANDLE;
        VkPipeline              UpsamplePipeline   = VK_NULL_HANDLE;
        VkPipeline              CompositePipeline  = VK_NULL_HANDLE;
        VkPipelineLayout        PipelineLayout     = VK_NULL_HANDLE;
        bool                    ParamsDirty        = true;

        Core::Memory::ArenaAllocator* m_arena  = nullptr;
        Hardwares::VulkanDevice*      m_device = nullptr;
    };

// Factory — allocates BloomPassState from arena, returns a ready-to-add entry.
PostProcessPassEntry MakeBloomPass(Core::Memory::ArenaAllocator* arena,
                                   Hardwares::VulkanDevice*      device,
                                   const BloomParams&            params);

// Free functions registered in the vtable by MakeBloomPass.
void BloomPass_Setup  (PostProcessPassData&, Graph::RenderGraphResourceBuilder&);
void BloomPass_Execute(PostProcessPassData&, VkCommandBuffer, const Graph::RenderGraph&);

}  // namespace ZEngine::Rendering::PostProcessing
```

**Registration at startup:**
```cpp
stack.AddPass(MakeBloomPass(arena, device, {.Threshold=1.0f, .Intensity=0.04f}));
stack.AddPass(MakeToneMappingPass(arena, device, {.Op=ToneMappingOp::ACES_Film}));
stack.AddPass(MakeFXAAPass(arena, device, {}));
stack.Compile();
```

No heap allocation. No vtable. Each pass is a flat `PostProcessPassEntry` in `m_passes`.

#### Threshold extract shader (`bloom_threshold.frag`)

```glsl
#version 450
#pragma stage fragment

layout(set = 0, binding = 0) uniform sampler2D u_hdr_input;

layout(push_constant) uniform BloomPush {
    float threshold;
    float intensity;
    float scatter;
    float _pad;
} u_push;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

float Luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec3 color = texture(u_hdr_input, v_uv).rgb;
    float lum  = Luminance(color);
    // Soft knee: smoothly attenuate pixels near the threshold
    float knee   = u_push.threshold * 0.5;
    float rq     = clamp(lum - u_push.threshold + knee, 0.0, 2.0 * knee);
    rq           = (rq * rq) / (4.0 * knee + 0.00001);
    float weight = max(rq, lum - u_push.threshold) / max(lum, 0.00001);
    o_color = vec4(color * weight, 1.0);
}
```

#### Downsample shader (`bloom_downsample.frag`)

Based on the Call of Duty: Advanced Warfare method (Jimenez 2014). Takes 4 taps at half-pixel
offsets from the centre to produce an anti-aliased 2× downsample.

```glsl
#version 450
#pragma stage fragment

layout(set = 0, binding = 0) uniform sampler2D u_src;

layout(push_constant) uniform DownsamplePush {
    vec2  src_texel_size;  // 1.0 / src_resolution
    float scatter;
    float _pad;
} u_push;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main() {
    vec2 ts = u_push.src_texel_size;

    // 13-tap filter (Kawase-inspired, COD variant):
    // Centre 2x2 box + 4 inner cross taps + 4 outer diagonal taps
    vec3 a = texture(u_src, v_uv + ts * vec2(-2.0, -2.0)).rgb;
    vec3 b = texture(u_src, v_uv + ts * vec2( 0.0, -2.0)).rgb;
    vec3 c = texture(u_src, v_uv + ts * vec2( 2.0, -2.0)).rgb;
    vec3 d = texture(u_src, v_uv + ts * vec2(-1.0, -1.0)).rgb;
    vec3 e = texture(u_src, v_uv + ts * vec2( 1.0, -1.0)).rgb;
    vec3 f = texture(u_src, v_uv + ts * vec2(-2.0,  0.0)).rgb;
    vec3 g = texture(u_src, v_uv                        ).rgb;
    vec3 h = texture(u_src, v_uv + ts * vec2( 2.0,  0.0)).rgb;
    vec3 i = texture(u_src, v_uv + ts * vec2(-1.0,  1.0)).rgb;
    vec3 j = texture(u_src, v_uv + ts * vec2( 1.0,  1.0)).rgb;
    vec3 k = texture(u_src, v_uv + ts * vec2(-2.0,  2.0)).rgb;
    vec3 l = texture(u_src, v_uv + ts * vec2( 0.0,  2.0)).rgb;
    vec3 m = texture(u_src, v_uv + ts * vec2( 2.0,  2.0)).rgb;

    // Weighted sum: inner 2x2 get weight 0.5, cross get 0.125, corners get 0.03125
    vec3 result =
        (d + e + i + j) * 0.5      // inner 2x2
      + (b + f + h + l) * 0.125    // cross
      + (a + c + k + m) * 0.03125; // corners
    // Normalize: 4*0.5 + 4*0.125 + 4*0.03125 = 2.625 — divide by total weight
    result /= 2.625;

    o_color = vec4(result, 1.0);
}
```

#### Upsample shader (`bloom_upsample.frag`)

```glsl
#version 450
#pragma stage fragment

layout(set = 0, binding = 0) uniform sampler2D u_src;       // smaller mip
layout(set = 0, binding = 1) uniform sampler2D u_dst_prev;  // larger mip (accumulate into)

layout(push_constant) uniform UpsamplePush {
    vec2  src_texel_size;
    float scatter;      // blend factor between mip levels (default 0.7)
    float _pad;
} u_push;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main() {
    vec2 ts = u_push.src_texel_size;

    // 9-tap tent filter
    vec3 a = texture(u_src, v_uv + ts * vec2(-1.0, -1.0)).rgb;
    vec3 b = texture(u_src, v_uv + ts * vec2( 0.0, -1.0)).rgb;
    vec3 c = texture(u_src, v_uv + ts * vec2( 1.0, -1.0)).rgb;
    vec3 d = texture(u_src, v_uv + ts * vec2(-1.0,  0.0)).rgb;
    vec3 e = texture(u_src, v_uv                        ).rgb;
    vec3 f = texture(u_src, v_uv + ts * vec2( 1.0,  0.0)).rgb;
    vec3 g = texture(u_src, v_uv + ts * vec2(-1.0,  1.0)).rgb;
    vec3 h = texture(u_src, v_uv + ts * vec2( 0.0,  1.0)).rgb;
    vec3 i = texture(u_src, v_uv + ts * vec2( 1.0,  1.0)).rgb;

    // Weighted tent: corners 1, edges 2, centre 4 — total weight 16
    vec3 upsampled = (a + c + g + i) * (1.0 / 16.0)
                   + (b + d + f + h) * (2.0 / 16.0)
                   + e               * (4.0 / 16.0);

    // Accumulate: blend upsampled into the larger mip
    vec3 prev = texture(u_dst_prev, v_uv).rgb;
    o_color = vec4(mix(prev, upsampled, u_push.scatter), 1.0);
}
```

#### Composite (additive blend) shader (`bloom_composite.frag`)

```glsl
#version 450
#pragma stage fragment

layout(set = 0, binding = 0) uniform sampler2D u_hdr;
layout(set = 0, binding = 1) uniform sampler2D u_bloom;

layout(push_constant) uniform CompositePush {
    float intensity;
    float _pad0, _pad1, _pad2;
} u_push;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main() {
    vec3 hdr   = texture(u_hdr,   v_uv).rgb;
    vec3 bloom = texture(u_bloom, v_uv).rgb;
    o_color = vec4(hdr + bloom * u_push.intensity, 1.0);
}
```

---

## 5. Tone Mapping Pass

### Purpose

Converts the HDR `RGBA16F` render target to LDR `RGBA8` suitable for display. Applied after
bloom (bloom operates in HDR so the additive contribution is correctly included in tone
mapping).

### Operator selection

```cpp
// ZEngine/Rendering/PostProcessing/ToneMappingPass.h

enum class ToneMappingOp : uint8_t {
    ACES_Film    = 0,  // Narkowicz ACES approximation — industry default
    Reinhard     = 1,  // Simple luminance Reinhard — useful for reference/debug
    Uncharted2   = 2,  // Filmic Uncharted 2 (Hable) — warm filmic look
    Neutral      = 3,  // Linear clamp — useful for debugging HDR values
};

struct ToneMappingParams {
    float         Exposure    = 1.0f;  // Pre-exposure multiplier applied before tone map
    ToneMappingOp Operator    = ToneMappingOp::ACES_Film;
    uint8_t       _pad[3]     = {};
};
```

### State struct and free functions

Tone mapping follows the same DOD pattern as `BloomPass` — a plain state struct
plus two free functions registered in a `PostProcessPassVtable`. No inheritance,
no virtual dispatch.

```cpp
struct ToneMappingState {
    ToneMappingParams        Params{};
    // Parameters pushed via PerFrameUploadHeap when dirty; no persistent VkBuffer.
    bool                     ParamsDirty   = true;
    VkPipeline               Pipeline      = VK_NULL_HANDLE;
    VkPipelineLayout         PipelineLayout= VK_NULL_HANDLE;
    Textures::TextureHandle  Input         = {};
    Textures::TextureHandle  Output        = {};  // VK_FORMAT_R8G8B8A8_UNORM

    Core::Memory::ArenaAllocator* m_arena  = nullptr;
    Hardwares::VulkanDevice*      m_device = nullptr;
};

// Factory — allocates ToneMappingState from arena, returns a ready-to-add entry.
PostProcessPassEntry MakeToneMappingPass(Core::Memory::ArenaAllocator* arena,
                                         Hardwares::VulkanDevice*      device,
                                         const ToneMappingParams&      params);

void ToneMappingPass_Setup  (PostProcessPassData&, Graph::RenderGraphResourceBuilder&);
void ToneMappingPass_Execute(PostProcessPassData&, VkCommandBuffer, const Graph::RenderGraph&);
```

### Tone mapping shader (`tone_mapping.frag`)

The ACES filmic curve is the Narkowicz 2015 approximation (the industry-standard fast
version used in Unreal Engine 4+). The full matrix transform is included for correctness.

```glsl
#version 450
#pragma stage fragment

layout(set = 0, binding = 0) uniform sampler2D u_hdr;

layout(push_constant) uniform ToneMapPush {
    float   exposure;
    uint    op;        // 0=ACES 1=Reinhard 2=Uncharted2 3=Neutral
    float   _pad0;
    float   _pad1;
} u_push;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

// ---------------------------------------------------------------------------
// ACES filmic — Narkowicz 2015 approximation
// Reference: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
// ---------------------------------------------------------------------------
vec3 ACES_Film(vec3 x) {
    // Input transform: sRGB → ACES AP1
    const mat3 m_in = mat3(
         0.59719, 0.35458, 0.04823,
         0.07600, 0.90834, 0.01566,
         0.02840, 0.13383, 0.83777
    );
    // Output transform: ACES AP1 → sRGB
    const mat3 m_out = mat3(
         1.60475, -0.53108, -0.07367,
        -0.10208,  1.10813, -0.00605,
        -0.00327, -0.07276,  1.07602
    );

    // RRT and ODT fit by Stephen Hill
    vec3 v = m_in * x;
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return clamp(m_out * (a / b), 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// Reinhard (simple luminance)
// ---------------------------------------------------------------------------
vec3 Reinhard(vec3 x) {
    return x / (1.0 + x);
}

// ---------------------------------------------------------------------------
// Uncharted 2 / Hable filmic
// Reference: https://gdcvault.com/play/1012351/Uncharted-2-HDR-Lighting
// ---------------------------------------------------------------------------
vec3 Uncharted2Partial(vec3 x) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 Uncharted2(vec3 x) {
    const float W = 11.2;
    vec3 curr     = Uncharted2Partial(x * 2.0);
    vec3 white    = Uncharted2Partial(vec3(W));
    return curr / white;
}

void main() {
    vec3 hdr = texture(u_hdr, v_uv).rgb * u_push.exposure;

    vec3 ldr;
    if      (u_push.op == 0u) ldr = ACES_Film(hdr);
    else if (u_push.op == 1u) ldr = Reinhard(hdr);
    else if (u_push.op == 2u) ldr = Uncharted2(hdr);
    else                      ldr = clamp(hdr, 0.0, 1.0);

    // Gamma correct: linear → sRGB
    ldr = pow(ldr, vec3(1.0 / 2.2));

    o_color = vec4(ldr, 1.0);
}
```

---

## 6. Color Grading / LUT Pass

### Purpose

Applies a 3D color lookup table to achieve stylized or cinematic color grades without
writing per-scene shader variants. The LUT encodes the full `(R, G, B) → (R', G', B')`
mapping at a resolution of 32×32×32.

### LUT texture specification

```cpp
// Built once at engine startup from the loaded .cube file.
Rendering::Specifications::TextureSpecification lut_spec{};
lut_spec.Width       = 32;
lut_spec.Height      = 32;
lut_spec.Depth       = 32;
lut_spec.Format      = VK_FORMAT_R8G8B8A8_UNORM;
lut_spec.Type        = TextureType::Texture3D;
lut_spec.Layers      = 1;
lut_spec.MipLevels   = 1;
lut_spec.Usage       = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
lut_spec.SamplerFilter = VK_FILTER_LINEAR;  // trilinear interpolation across LUT cells
```

### Parameters

```cpp
struct ColorGradingParams {
    float Contribution = 1.0f;  // 0 = original color, 1 = fully graded
    float _pad[3]      = {};
};
```

### State struct and free functions

```cpp
// ZEngine/Rendering/PostProcessing/ColorGradingPass.h
struct ColorGradingState {
    ColorGradingParams       Params{};
    bool                     ParamsDirty    = true;
    Textures::TextureHandle  LUTTexture     = {};
    bool                     LUTIsIdentity  = true;
    VkPipeline               Pipeline       = VK_NULL_HANDLE;
    VkPipelineLayout         PipelineLayout = VK_NULL_HANDLE;
    Textures::TextureHandle  Input          = {};
    Textures::TextureHandle  Output         = {};

    Core::Memory::ArenaAllocator* m_arena  = nullptr;
    Hardwares::VulkanDevice*      m_device = nullptr;
};

PostProcessPassEntry MakeColorGradingPass(Core::Memory::ArenaAllocator* arena,
                                           Hardwares::VulkanDevice*      device,
                                           const ColorGradingParams&     params);

// Load a .cube LUT file from the VFS. Safe to call any time after Initialize.
// On failure logs the error and keeps the current LUT (identity at startup).
void ColorGradingPass_LoadLUT  (ColorGradingState* state, const VFS::VFSPath& cube_path);
void ColorGradingPass_ResetLUT (ColorGradingState* state);
void ColorGradingPass_Setup    (PostProcessPassData&, Graph::RenderGraphResourceBuilder&);
void ColorGradingPass_Execute  (PostProcessPassData&, VkCommandBuffer, const Graph::RenderGraph&);
```

### Color grading shader (`color_grading.frag`)

```glsl
#version 450
#pragma stage fragment

layout(set = 0, binding = 0) uniform sampler2D   u_input;
layout(set = 0, binding = 1) uniform sampler3D   u_lut;

layout(push_constant) uniform LUTPush {
    float contribution;
    float _pad0, _pad1, _pad2;
} u_push;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main() {
    vec3 original = texture(u_input, v_uv).rgb;

    // Scale input to LUT coordinate space.
    // For a 32x32x32 LUT: remap [0,1] to [0.5/32, 31.5/32] to hit cell centres.
    const float LUT_SIZE  = 32.0;
    const float HALF_CELL = 0.5 / LUT_SIZE;
    const float SCALE     = (LUT_SIZE - 1.0) / LUT_SIZE;

    vec3 lut_uv = original * SCALE + HALF_CELL;
    vec3 graded = texture(u_lut, lut_uv).rgb;

    o_color = vec4(mix(original, graded, u_push.contribution), 1.0);
}
```

### `.cube` file import

`.cube` files are ASCII text files (Adobe / Iridas format). The importer reads the
`LUT_SIZE` header line, then reads `LUT_SIZE^3` RGB triplets in RGB-first order
(R fast, G medium, B slow), remaps from [0, 1] to [0, 255], and uploads via
`RenderResourceManager::UploadTexture`. A 32×32×32 LUT is 98,304 bytes of R8G8B8A8 data
(with A=255 throughout). Parse is done on the asset import thread; GPU upload on the
render thread via the pending-upload flush.

---

## 7. FXAA Pass

### Purpose

Fast approximate anti-aliasing as a screen-space post-process. Applied after tone mapping
on the LDR buffer. Provides edge smoothing without the 4×/8× MSAA memory overhead.

### Parameters

```cpp
struct FXAAParams {
    float EdgeThreshold    = 0.125f;   // Minimum local contrast for edge detection
    float EdgeThresholdMin = 0.0625f;  // Absolute minimum luma for edge (ignore dark areas)
    float Subpixel         = 0.75f;    // Subpixel aliasing removal strength [0, 1]
    float _pad             = 0.0f;
};
```

### State struct and free functions

```cpp
// ZEngine/Rendering/PostProcessing/FXAAPass.h
struct FXAAState {
    FXAAParams               Params{};
    bool                     ParamsDirty  = true;
    VkPipeline               Pipeline     = VK_NULL_HANDLE;
    VkPipelineLayout         PipelineLayout = VK_NULL_HANDLE;
    Textures::TextureHandle  Input        = {};
    Textures::TextureHandle  Output       = {};

    Core::Memory::ArenaAllocator* m_arena  = nullptr;
    Hardwares::VulkanDevice*      m_device = nullptr;
};

PostProcessPassEntry MakeFXAAPass(Core::Memory::ArenaAllocator* arena,
                                   Hardwares::VulkanDevice*      device,
                                   const FXAAParams&             params);

void FXAAPass_Setup  (PostProcessPassData&, Graph::RenderGraphResourceBuilder&);
void FXAAPass_Execute(PostProcessPassData&, VkCommandBuffer, const Graph::RenderGraph&);
```

### FXAA 3.11 shader (`fxaa.frag`)

This implements the NVIDIA FXAA 3.11 algorithm ported to Vulkan GLSL. The input must be a
linear-light LDR buffer (after gamma correction from the tone mapping pass).

```glsl
#version 450
#pragma stage fragment

layout(set = 0, binding = 0) uniform sampler2D u_input;

layout(push_constant) uniform FXAAPush {
    vec2  rcp_frame;          // 1.0 / render_target_size
    float edge_threshold;
    float edge_threshold_min;
    float subpixel;
    float _pad0, _pad1, _pad2;
} u_push;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

// Convert RGB to luma (green-biased perceptual weight).
float RGBToLuma(vec3 rgb) {
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec2 uv = v_uv;
    vec2 ts = u_push.rcp_frame;

    // Sample 3x3 neighbourhood luma values
    float lumaNW = RGBToLuma(textureOffset(u_input, uv, ivec2(-1,-1)).rgb);
    float lumaNE = RGBToLuma(textureOffset(u_input, uv, ivec2( 1,-1)).rgb);
    float lumaSW = RGBToLuma(textureOffset(u_input, uv, ivec2(-1, 1)).rgb);
    float lumaSE = RGBToLuma(textureOffset(u_input, uv, ivec2( 1, 1)).rgb);
    float lumaM  = RGBToLuma(texture(u_input, uv).rgb);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    float lumaRange = lumaMax - lumaMin;

    // Early-out: not an edge
    if (lumaRange < max(u_push.edge_threshold_min,
                        lumaMax * u_push.edge_threshold)) {
        o_color = texture(u_input, uv);
        return;
    }

    // Determine blend direction (gradient of luma)
    float lumaS = RGBToLuma(textureOffset(u_input, uv, ivec2( 0, 1)).rgb);
    float lumaN = RGBToLuma(textureOffset(u_input, uv, ivec2( 0,-1)).rgb);
    float lumaE = RGBToLuma(textureOffset(u_input, uv, ivec2( 1, 0)).rgb);
    float lumaW = RGBToLuma(textureOffset(u_input, uv, ivec2(-1, 0)).rgb);

    float edgeH = abs(-2.0*lumaW + lumaNW + lumaSW)
                + abs(-2.0*lumaM + lumaN  + lumaS ) * 2.0
                + abs(-2.0*lumaE + lumaNE + lumaSE);
    float edgeV = abs(-2.0*lumaN + lumaNW + lumaNE)
                + abs(-2.0*lumaM + lumaW  + lumaE ) * 2.0
                + abs(-2.0*lumaS + lumaSW + lumaSE);

    bool isHorizontal = (edgeH >= edgeV);

    // Estimate sub-pixel blend factor (removes sub-pixel aliasing).
    float lumaAvg     = (lumaN + lumaS + lumaE + lumaW) * 0.25;
    float subpixelOff = abs(lumaAvg - lumaM) / lumaRange;
    float subpixelBlend = smoothstep(0.0, 1.0, subpixelOff) * u_push.subpixel;

    // Sample along the perpendicular to the edge
    vec2 step = isHorizontal ? vec2(0.0, ts.y) : vec2(ts.x, 0.0);
    vec3 col0 = texture(u_input, uv - step * 0.5).rgb;
    vec3 col1 = texture(u_input, uv + step * 0.5).rgb;
    vec3 blended = mix(texture(u_input, uv).rgb,
                       mix(col0, col1, 0.5),
                       subpixelBlend);

    o_color = vec4(blended, 1.0);
}
```

**Note**: The above is a simplified but correct FXAA 3.11 approximation suitable for game
use. For the full quality 3.11 reference (iterative endpoint search along the edge), see
Lottes 2011 — the iterative search adds ~12 additional texture samples and is recommended
for high-quality output.

---

## 8. Chromatic Aberration Pass (Optional)

Simulates lens chromatic aberration by offsetting the R, G, and B channels outward from
the screen centre by slightly different amounts.

### Parameters

```cpp
struct ChromaticAberrationParams {
    float Strength = 0.002f;  // Maximum UV offset at screen edge. Typical: 0.001–0.005
    float _pad[3]  = {};
};
```

### State struct and free functions

```cpp
// ZEngine/Rendering/PostProcessing/ChromaticAberrationPass.h
struct ChromaticAberrationState {
    ChromaticAberrationParams Params{};
    bool                      ParamsDirty  = true;
    VkPipeline                Pipeline     = VK_NULL_HANDLE;
    VkPipelineLayout          PipelineLayout = VK_NULL_HANDLE;
    Textures::TextureHandle   Input        = {};
    Textures::TextureHandle   Output       = {};

    Core::Memory::ArenaAllocator* m_arena  = nullptr;
    Hardwares::VulkanDevice*      m_device = nullptr;
};

PostProcessPassEntry MakeChromAberrationPass(Core::Memory::ArenaAllocator*         arena,
                                              Hardwares::VulkanDevice*              device,
                                              const ChromaticAberrationParams&      params);

void ChromAberrationPass_Setup  (PostProcessPassData&, Graph::RenderGraphResourceBuilder&);
void ChromAberrationPass_Execute(PostProcessPassData&, VkCommandBuffer, const Graph::RenderGraph&);
```

### Shader (`chromatic_aberration.frag`)

```glsl
#version 450
#pragma stage fragment

layout(set = 0, binding = 0) uniform sampler2D u_input;

layout(push_constant) uniform CAPush {
    float strength;
    float _pad0, _pad1, _pad2;
} u_push;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main() {
    // Compute offset vector from screen centre (NDC-space)
    vec2 dir = v_uv - 0.5;

    // Sample each channel with a progressively larger offset
    float r = texture(u_input, v_uv + dir * u_push.strength * 1.0).r;
    float g = texture(u_input, v_uv + dir * u_push.strength * 0.5).g;
    float b = texture(u_input, v_uv                               ).b;

    o_color = vec4(r, g, b, 1.0);
}
```

---

## 9. Vignette Pass (Optional)

Multiplies the image by a smooth radial falloff from the screen centre. Darkens the corners
to draw attention to the centre of the frame.

### Parameters

```cpp
struct VignetteParams {
    Core::Maths::Vec4f Color      = { 0.0f, 0.0f, 0.0f, 1.0f };  // Vignette tint (usually black)
    float              Intensity  = 0.4f;   // Maximum darkening strength at corners
    float              Smoothness = 0.8f;   // Falloff width; larger = gentler fade
    float              _pad[2]   = {};
};
```

### State struct and free functions

```cpp
// ZEngine/Rendering/PostProcessing/VignettePass.h
struct VignetteState {
    VignetteParams           Params{};
    bool                     ParamsDirty  = true;
    VkPipeline               Pipeline     = VK_NULL_HANDLE;
    VkPipelineLayout         PipelineLayout = VK_NULL_HANDLE;
    Textures::TextureHandle  Input        = {};
    Textures::TextureHandle  Output       = {};

    Core::Memory::ArenaAllocator* m_arena  = nullptr;
    Hardwares::VulkanDevice*      m_device = nullptr;
};

PostProcessPassEntry MakeVignettePass(Core::Memory::ArenaAllocator* arena,
                                       Hardwares::VulkanDevice*      device,
                                       const VignetteParams&         params);

void VignettePass_Setup  (PostProcessPassData&, Graph::RenderGraphResourceBuilder&);
void VignettePass_Execute(PostProcessPassData&, VkCommandBuffer, const Graph::RenderGraph&);
```

### Shader (`vignette.frag`)

```glsl
#version 450
#pragma stage fragment

layout(set = 0, binding = 0) uniform sampler2D u_input;

layout(push_constant) uniform VignettePush {
    vec4  color;
    float intensity;
    float smoothness;
    float _pad0, _pad1;
} u_push;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main() {
    vec3 input_color = texture(u_input, v_uv).rgb;

    // Distance from screen centre in UV space
    vec2 uv     = v_uv - 0.5;
    float dist  = length(uv);
    // Smooth falloff: 0 at centre, 1 at corners (dist ≈ 0.707 at corners for 1:1 aspect)
    float vignette = smoothstep(0.5 * u_push.smoothness,
                                0.5,
                                dist * u_push.intensity);
    // Blend towards vignette color
    vec3 result = mix(input_color, u_push.color.rgb, vignette);

    o_color = vec4(result, 1.0);
}
```

---

## 10. Screen-Space Ambient Occlusion (SSAO)

### Purpose

SSAO approximates ambient occlusion by sampling the depth buffer around each pixel in view
space. It produces an `R8` occlusion texture (0 = fully occluded, 1 = fully unoccluded) that
is multiplied into the ambient lighting term in the main scene pass.

### Position in the pipeline

SSAO runs **before** the main lighting pass — not as a post-process in the LDR chain. It is
included in `PostProcessStack` because it shares the same `IRenderGraphCallbackPass`
interface and is wired into the `RenderGraph` alongside the other effects.

### Parameters

```cpp
struct SSAOParams {
    float    Radius      = 0.5f;    // Hemisphere sample radius in view space (metres)
    float    Bias        = 0.025f;  // Depth bias to avoid self-occlusion artefacts
    float    Power       = 2.0f;    // Exponent applied to raw occlusion for contrast
    uint32_t SampleCount = 16;      // Hemisphere sample count; must be 4, 8, 16, or 32
};
```

### Kernel and noise generation

A hemisphere of `SampleCount` random samples is generated once at startup and uploaded to a
UBO:

```cpp
// ZEngine/Rendering/PostProcessing/SSAOPass.cpp (init excerpt)
void SSAOPass::GenerateKernel() {
    // Pseudo-random hemisphere samples, importance-sampled towards the surface
    for (uint32_t i = 0; i < m_params.SampleCount; ++i) {
        Core::Maths::Vec3f sample = {
            m_rng.NextFloat(-1.0f, 1.0f),
            m_rng.NextFloat(-1.0f, 1.0f),
            m_rng.NextFloat( 0.0f, 1.0f),  // hemisphere: z > 0 in view space
        };
        sample = sample.Normalized();
        sample = sample * m_rng.NextFloat(0.0f, 1.0f);
        // Accelerating interpolation: samples cluster near the origin
        float scale = float(i) / float(m_params.SampleCount);
        scale = 0.1f + 0.9f * (scale * scale);
        sample = sample * scale;
        m_kernel[i] = sample;
    }
}
```

A 4×4 tileable noise texture (`VK_FORMAT_R16G16_SFLOAT`, encoding random rotation vectors in
the XY plane) is used to rotate the hemisphere kernel per-pixel, removing banding artefacts
without extra samples.

### State struct and free functions

```cpp
// ZEngine/Rendering/PostProcessing/SSAOPass.h
#pragma once
#include <Rendering/PostProcessing/PostProcessPass.h>

namespace ZEngine::Rendering::PostProcessing {

    static constexpr uint32_t SSAO_MAX_SAMPLES = 32;
    static constexpr uint32_t SSAO_NOISE_DIM   = 4;

    struct SSAOState {
        SSAOParams               Params{};
        bool                     ParamsDirty     = true;
        Core::Maths::Vec3f       Kernel[SSAO_MAX_SAMPLES] = {};
        // KernelUBO and NoiseTex are static after init — uploaded once via
        // GpuAllocator staging ring, not re-uploaded each frame.
        VkBuffer                 KernelUBO       = VK_NULL_HANDLE;
        VmaAllocation            KernelAlloc     = nullptr;
        Textures::TextureHandle  NoiseTex        = {};
        Textures::TextureHandle  DepthInput      = {};
        Textures::TextureHandle  NormalsInput    = {};
        Textures::TextureHandle  OcclusionOut    = {};  // R8, half-res

        VkPipeline               SSAOPipeline    = VK_NULL_HANDLE;
        VkPipeline               BlurPipeline    = VK_NULL_HANDLE;
        VkPipelineLayout         PipelineLayout  = VK_NULL_HANDLE;

        Core::Memory::ArenaAllocator* m_arena    = nullptr;
        Hardwares::VulkanDevice*      m_device   = nullptr;
    };

    PostProcessPassEntry MakeSSAOPass(Core::Memory::ArenaAllocator* arena,
                                       Hardwares::VulkanDevice*      device,
                                       const SSAOParams&             params);

    void SSAOPass_Setup  (PostProcessPassData&, Graph::RenderGraphResourceBuilder&);
    void SSAOPass_Execute(PostProcessPassData&, VkCommandBuffer, const Graph::RenderGraph&);

}  // namespace ZEngine::Rendering::PostProcessing
```

### SSAO shader (`ssao.frag`)

```glsl
#version 450
#pragma stage fragment

layout(set = 0, binding = 0) uniform sampler2D u_depth;
layout(set = 0, binding = 1) uniform sampler2D u_normals;  // view-space octahedral normals
layout(set = 0, binding = 2) uniform sampler2D u_noise;    // 4x4 tileable random rotation

layout(set = 0, binding = 3) uniform SSAOParams {
    mat4     projection;
    mat4     inv_projection;
    vec4     samples[32];    // view-space hemisphere samples (w unused)
    float    radius;
    float    bias;
    float    power;
    uint     sample_count;
    vec2     noise_scale;    // render_size / noise_size (e.g. 1920/4, 1080/4)
    float    _pad0, _pad1;
} u_params;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out float o_occlusion;

// Reconstruct view-space position from depth and UV.
vec3 ReconstructViewPos(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 view = u_params.inv_projection * clip;
    return view.xyz / view.w;
}

// Decode octahedral normals (16-bit R16G16 packed)
vec3 DecodeNormal(vec2 enc) {
    vec2 f = enc * 2.0 - 1.0;
    vec3 n = vec3(f, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.x += (n.x >= 0.0 ? -t : t);
    n.y += (n.y >= 0.0 ? -t : t);
    return normalize(n);
}

void main() {
    float depth        = texture(u_depth, v_uv).r;
    vec3  frag_pos     = ReconstructViewPos(v_uv, depth);
    vec3  normal       = DecodeNormal(texture(u_normals, v_uv).rg);
    vec3  random_vec   = normalize(texture(u_noise, v_uv * u_params.noise_scale).xyz);

    // Gram-Schmidt: build TBN from normal and random vector
    vec3 tangent   = normalize(random_vec - normal * dot(random_vec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN       = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    for (uint i = 0u; i < u_params.sample_count; ++i) {
        // Transform sample from tangent space to view space
        vec3 sample_pos = TBN * u_params.samples[i].xyz;
        sample_pos = frag_pos + sample_pos * u_params.radius;

        // Project sample onto depth buffer
        vec4 offset = u_params.projection * vec4(sample_pos, 1.0);
        offset.xy  /= offset.w;
        offset.xy   = offset.xy * 0.5 + 0.5;

        float sample_depth = texture(u_depth, offset.xy).r;
        vec3  sample_view  = ReconstructViewPos(offset.xy, sample_depth);

        // Range check: only count samples within the radius
        float range_check = smoothstep(0.0, 1.0,
            u_params.radius / abs(frag_pos.z - sample_view.z));
        occlusion += (sample_view.z >= sample_pos.z + u_params.bias ? 1.0 : 0.0)
                   * range_check;
    }

    occlusion = 1.0 - (occlusion / float(u_params.sample_count));
    o_occlusion = pow(occlusion, u_params.power);
}
```

### Bilateral blur pass

After the SSAO pass, a 4×4 bilateral blur is applied to the `R8` occlusion buffer. The
bilateral weights preserve sharp edges (where the normal or depth changes discontinuously)
while smoothing noise in flat regions:

```glsl
// ssao_blur.frag (key logic only)
void main() {
    float result = 0.0;
    float total  = 0.0;
    float center_depth = texture(u_depth, v_uv).r;

    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            vec2  offset = vec2(x, y) * u_texel_size;
            float d      = texture(u_depth,     v_uv + offset).r;
            float occ    = texture(u_occlusion, v_uv + offset).r;

            // Spatial Gaussian weight
            float spatial_w = exp(-float(x*x + y*y) / (2.0 * 4.0));
            // Range weight: penalise samples far in depth
            float range_w   = exp(-abs(d - center_depth) / 0.01);

            float w = spatial_w * range_w;
            result += occ * w;
            total  += w;
        }
    }
    o_occlusion = result / max(total, 0.0001);
}
```

---

## 11. Pass Ordering

The exact sequence executed each frame. Lower order values are processed first.

| Order | Pass | Input | Output | Format |
|-------|------|-------|--------|--------|
| -200 | `SSAOPass` (SSAO compute) | `depth`, `gbuf_normals` | `occlusion_raw` (half-res) | `R8` |
| -190 | `SSAOPass` (bilateral blur) | `occlusion_raw` | `occlusion_blurred` | `R8` |
| -100 | Main Lighting | `hdr_color` + `occlusion_blurred` | `hdr_lit` | `RGBA16F` |
| 100 | `BloomPass` (threshold) | `hdr_lit` | `bloom_threshold` (quarter-res) | `RGBA16F` |
| 110 | `BloomPass` (downsample ×5) | `bloom_threshold` | `bloom_mip[0..4]` | `RGBA16F` |
| 120 | `BloomPass` (upsample ×5) | `bloom_mip[4..0]` | `bloom_blurred` | `RGBA16F` |
| 130 | `BloomPass` (composite) | `hdr_lit` + `bloom_blurred` | `hdr_bloomed` | `RGBA16F` |
| 200 | `ToneMappingPass` | `hdr_bloomed` | `ldr_tonemapped` | `RGBA8` |
| 300 | `ColorGradingPass` | `ldr_tonemapped` | `ldr_graded` | `RGBA8` |
| 400 | `FXAAPass` | `ldr_graded` | `ldr_aa` | `RGBA8` |
| 500 | `ChromaticAberrationPass` | `ldr_aa` | `ldr_ca` | `RGBA8` |
| 600 | `VignettePass` | `ldr_ca` | `swapchain_image` | `RGBA8` |

Passes marked optional (`ChromaticAberrationPass`, `VignettePass`) are disabled by default.
`SetEnabled(hash, false)` causes the stack to pass their input directly to their output slot
without recording a draw call, preserving the chain without rebuilding the RenderGraph.

---

## 12. Performance Notes

### Target budget at 1080p60

| Pass | Resolution | Expected cost (RTX 3060) |
|------|-----------|--------------------------|
| SSAO (16 samples) | Half-res | ~0.8 ms |
| SSAO blur | Half-res | ~0.2 ms |
| Bloom threshold | Quarter-res | ~0.1 ms |
| Bloom 5×downsample | Quarter–1/128 | ~0.3 ms |
| Bloom 5×upsample | 1/64–Half | ~0.3 ms |
| Tone mapping | Full-res | ~0.2 ms |
| Color LUT | Full-res | ~0.2 ms |
| FXAA | Full-res | ~0.4 ms |
| Chromatic aberration | Full-res | ~0.1 ms |
| Vignette | Full-res | ~0.1 ms |
| **Total** | | **~2.7 ms** |

### Key optimisation decisions

**Half-resolution SSAO**: The SSAO pass runs at half the render resolution. Spatial
reconstruction at full resolution from a half-res occlusion buffer is visually transparent
for most scenes. The bilateral blur preserves edge sharpness.

**Quarter-resolution bloom threshold**: The threshold extract and first downsample step
operate at quarter resolution. Bloom is a large-radius effect — the frequency content that
makes it perceptible is not present at sub-quarter-pixel scales.

**Full-resolution FXAA**: Anti-aliasing must run at full resolution to catch single-pixel
edges. The FXAA 3.11 cost at 1080p is acceptable (~0.4 ms). At 4K, consider TAA instead.

**RenderGraph aliasing**: The `ping` and `pong` intermediate buffers share a single
`VkDeviceMemory` allocation (if the RenderGraph aliasing pass confirms non-overlapping
lifetimes). This reduces total post-process VRAM cost from ~6× full-res to ~3× full-res.

**UBO caching**: Post-process parameter UBOs are written once per parameter change via
`vkCmdUpdateBuffer`, not once per frame. A dirty flag per-pass avoids redundant UBO writes
during stable frames.

---

## 13. File Layout

```
ZEngine/Rendering/PostProcessing/
├── PostProcessPass.h              — DOD types (PostProcessPassData, PostProcessPassVtable, PostProcessPassEntry)
├── PostProcessStack.h/.cpp        — owns + orders all passes
├── BloomPass.h/.cpp               — dual kawase bloom
├── ToneMappingPass.h/.cpp         — ACES/Reinhard/Uncharted2/Neutral
├── ColorGradingPass.h/.cpp        — 3D LUT color grading + .cube import
├── FXAAPass.h/.cpp                — FXAA 3.11
├── ChromaticAberrationPass.h/.cpp — optional chromatic aberration
├── VignettePass.h/.cpp            — optional vignette
└── SSAOPass.h/.cpp                — SSAO + bilateral blur

ZEngine/Assets/Shaders/PostProcessing/
├── fullscreen_triangle.vert       — shared full-screen triangle vertex shader
├── bloom_threshold.frag
├── bloom_downsample.frag
├── bloom_upsample.frag
├── bloom_composite.frag
├── tone_mapping.frag
├── color_grading.frag
├── fxaa.frag
├── chromatic_aberration.frag
├── vignette.frag
├── ssao.frag
└── ssao_blur.frag
```

### Full-screen triangle vertex shader (shared)

All post-process passes use a single full-screen triangle vertex shader. No vertex buffer
is bound — the three vertices are generated from the vertex index:

```glsl
// fullscreen_triangle.vert
#version 450
#pragma stage vertex

layout(location = 0) out vec2 v_uv;

void main() {
    // Indices 0,1,2 map to a CCW triangle that covers the full NDC [-1,1] square.
    v_uv        = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);
}
```

This is compatible with Vulkan's default front-face convention and requires no input
assembly state.

---

## 14. Deliverables Checklist

### Core infrastructure
- [ ] `ZEngine/Rendering/PostProcessing/PostProcessPass.h` — DOD types: `PostProcessPassData` (name, I/O handles, enabled flag, order, void* params), `PostProcessPassVtable` (Setup/Execute free-function pointers), `PostProcessPassEntry` aggregate; no virtual inheritance
- [ ] `ZEngine/Rendering/PostProcessing/PostProcessStack.h/.cpp` — `Initialize`, `AddPass`, `RemovePass`, `SetEnabled`, `Compile`, `Execute`; ping-pong resource allocation; sorted pass list via `order` field
- [ ] `ZEngine/Assets/Shaders/PostProcessing/fullscreen_triangle.vert` — index-based full-screen triangle, no VBO

### Bloom
- [ ] `BloomPass.h/.cpp` — `BloomParams`; threshold extract; 5-step downsample chain (13-tap Kawase); 5-step upsample chain (9-tap tent); additive composite
- [ ] `bloom_threshold.frag`, `bloom_downsample.frag`, `bloom_upsample.frag`, `bloom_composite.frag`

### Tone mapping
- [ ] `ToneMappingPass.h/.cpp` — `ToneMappingOp` enum; `ToneMappingParams`; ACES Narkowicz matrix + curve; Reinhard; Uncharted2/Hable; Neutral (clamp); gamma correction in shader
- [ ] `tone_mapping.frag` — runtime operator selection via push constant

### Color grading
- [ ] `ColorGradingPass.h/.cpp` — `ColorGradingParams`; 32×32×32 `R8G8B8A8` 3D texture; `LoadLUT(VFSPath)` parses `.cube` ASCII; `ResetLUT()` uploads identity LUT; trilinear sampler; half-cell remapping in shader
- [ ] `color_grading.frag`

### FXAA
- [ ] `FXAAPass.h/.cpp` — `FXAAParams`; FXAA 3.11 luma-based edge detection; subpixel blend; early-out on non-edges
- [ ] `fxaa.frag`

### Chromatic aberration (optional)
- [ ] `ChromaticAberrationPass.h/.cpp` — `ChromaticAberrationParams`; per-channel UV offset from screen centre
- [ ] `chromatic_aberration.frag`

### Vignette (optional)
- [ ] `VignettePass.h/.cpp` — `VignetteParams`; `smoothstep` radial falloff; colour tint
- [ ] `vignette.frag`

### SSAO
- [ ] `SSAOPass.h/.cpp` — `SSAOParams`; hemisphere kernel generation; 4×4 noise texture; depth reconstruction; view-space hemisphere sampling; range check; bilateral blur pass
- [ ] `ssao.frag`, `ssao_blur.frag`

### Shaders
- [ ] All GLSL shaders compile to valid SPIR-V via the `ShaderImporter` pipeline with zero errors
- [ ] All shaders use `#pragma stage` directive
- [ ] Shaders are registered as assets in the `AssetRegistry` at engine startup

### Integration
- [ ] `PostProcessStack` registered as a `WorldTick` system with correct `SystemDeps` ordering: after main scene render, before swapchain present
- [ ] All intermediate textures declared via `RenderGraphResourceBuilder` in `Setup()` — no manual `vkCreateImage` calls inside post-process passes
- [ ] `ZENGINE_VALIDATE_ASSERT` guards: non-null device, non-null arena, valid input/output handles in `Execute`
- [ ] Manual smoke test: render a scene with emissive surfaces; confirm bloom halos, ACES tone mapping, LUT grade, FXAA edge smoothing, and vignette all appear correctly with zero Vulkan validation layer errors
