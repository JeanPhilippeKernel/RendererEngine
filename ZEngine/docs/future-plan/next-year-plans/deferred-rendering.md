# ZEngine — Deferred Rendering Path

**Priority:** Next-year plan — required for scenes with 50+ dynamic lights
**Status:** Design
**Depends on:** `render-graph-integration.md`, `shadows.md`, `light-culling.md`
**Note:** This is an alternative rendering path, not a replacement. Forward rendering remains the default for games with fewer than 20 lights. Deferred is opt-in.

---

## 1. Forward vs Deferred

The two rendering paths differ in when lighting is evaluated relative to visibility determination.

| Criterion | Forward | Deferred |
|---|---|---|
| Lighting evaluation | Per fragment, all lights, during geometry draw | Per pixel, all lights, after all geometry is drawn |
| Light count scaling | O(fragments × lights) | O(pixels × lights) |
| Practical light limit | 20–30 without culling; ~100 with tile culling | Hundreds to thousands (combined with tile culling) |
| Transparency support | Native | Requires a separate forward pass for transparent objects |
| MSAA support | Native | Requires deferred MSAA resolve or TAA (see §8) |
| Bandwidth | Low (one pass per draw call) | High (G-buffer read + write each frame) |
| Overdraw cost | High (shading culled by depth test after full shading) | None (G-buffer fill is cheap; lighting runs once per pixel) |
| Memory cost | Lower | Higher (~64MB additional VRAM at 4K; see §7) |
| Material variety | Unlimited | Must fit in G-buffer layout |
| Best for | Mobile, outdoor, few lights, high transparency | Indoor/architectural, 50+ lights, complex scenes |

**Default path:** Forward rendering with tile-based light culling (`light-culling.md`). Forward handles up to ~100 lights comfortably with culling. Deferred is activated only when a scene has 50+ lights or when the project explicitly enables it.

---

## 2. G-Buffer Layout

The G-buffer is four render targets. All targets share the same dimensions as the swapchain. All targets are created and owned by the RenderGraph as named resources.

| Slot | Format | Contents | RenderGraph Name |
|---|---|---|---|
| RT0 | `VK_FORMAT_R8G8B8A8_UNORM` | Albedo (RGB) + Ambient Occlusion (A) | `"gbuffer_albedo_ao"` |
| RT1 | `VK_FORMAT_R16G16B16A16_SFLOAT` | View-space normals (RGB) + Roughness (A) | `"gbuffer_normals_rough"` |
| RT2 | `VK_FORMAT_R8G8B8A8_UNORM` | Metallic (R) + Emissive Intensity (G) + Reserved (BA) | `"gbuffer_metallic_emissive"` |
| RT3 | `VK_FORMAT_D32_SFLOAT` | Depth | `"hdr_depth"` (shared with forward depth pre-pass) |

**RT0 packing:** Albedo is stored in linear space (sRGB conversion happens in the final tonemapping pass, same as the forward path). AO is a scalar [0,1] packed into the alpha channel.

**RT1 packing:** Normals are stored in view space, not world space. View-space normals have smaller magnitude variation (always point toward the camera hemisphere) and compress better. The W component stores roughness [0,1].

**RT2 packing:** Metallic is 0 or 1 for most materials (stored as a float for smooth gradients). Emissive intensity multiplies the emissive color at shading time. The reserved BA channels are available for lightmap UV packing (see `lightmap-baking.md` §9) or future material flags.

**Depth:** The depth buffer is shared with the forward depth pre-pass. `GBufferPass` writes to `"hdr_depth"`. `DeferredLightingPass` reads it for position reconstruction. Transparent forward pass reads it for depth testing after deferred lighting.

---

## 3. GBufferPass

`GBufferPass` replaces the forward `GeometryPass` in the deferred pipeline. It draws all opaque geometry and writes PBR material properties to the G-buffer. No lighting computation occurs in this pass.

```cpp
class GBufferPass final : public IRenderGraphCallbackPass {
public:
    void Setup(Hardwares::VulkanDevicePtr const device, cstring name,
               RenderGraphResourceBuilderPtr const res_builder,
               RenderGraphResourceInspectorPtr res_inspector) override;

    void Compile(Hardwares::VulkanDevicePtr const device,
                 Rendering::Scenes::SceneDataPtr const scene,
                 RenderPasses::RenderPassBuilder* pass_builder,
                 RenderGraphResourceInspectorPtr res_inspector,
                 RenderPasses::RenderPass** const output_pass) override;

    void Execute(Hardwares::VulkanDevicePtr const device,
                 RenderGraphResourceInspectorPtr res_inspector,
                 Rendering::Scenes::SceneDataPtr const scene,
                 RenderPasses::RenderPass* const pass,
                 Buffers::FramebufferVNext* const framebuffer,
                 Hardwares::CommandBufferPtr const command_buffer) override;

    void Deinitialize(Hardwares::VulkanDevicePtr const device) override;
};
```

**Setup:**

```cpp
void GBufferPass::Setup(Hardwares::VulkanDevicePtr const device, cstring name,
                        RenderGraphResourceBuilderPtr const res_builder,
                        RenderGraphResourceInspectorPtr res_inspector) {
    TextureSpecification albedo_spec = {};
    albedo_spec.Format               = VK_FORMAT_R8G8B8A8_UNORM;
    res_builder->WriteColorAttachment("gbuffer_albedo_ao", albedo_spec);

    TextureSpecification normals_spec = {};
    normals_spec.Format               = VK_FORMAT_R16G16B16A16_SFLOAT;
    res_builder->WriteColorAttachment("gbuffer_normals_rough", normals_spec);

    TextureSpecification metallic_spec = {};
    metallic_spec.Format               = VK_FORMAT_R8G8B8A8_UNORM;
    res_builder->WriteColorAttachment("gbuffer_metallic_emissive", metallic_spec);

    TextureSpecification depth_spec = {};
    depth_spec.Format               = VK_FORMAT_D32_SFLOAT;
    res_builder->WriteDepthAttachment("hdr_depth", depth_spec);
}
```

Before `Compile` is called, the RenderGraph pre-populates the `RenderPassBuilder` with one `UseRenderTarget` call per declared write. `Compile` receives this pre-populated builder and extends it with pipeline state:

```cpp
void GBufferPass::Compile(Hardwares::VulkanDevicePtr const device,
                          Rendering::Scenes::SceneDataPtr const scene,
                          RenderPasses::RenderPassBuilder* pass_builder,
                          RenderGraphResourceInspectorPtr res_inspector,
                          RenderPasses::RenderPass** const output_pass) {
    pass_builder->SetPipelineName("gbuffer")
                 .EnablePipelineDepthTest(true)
                 .UseShader("gbuffer.vert", "gbuffer.frag")
                 .Detach(output_pass);
}
```

`Execute` binds the per-mesh material descriptor sets and records the draw calls for all opaque meshes in the scene.

**Vertex shader:** standard MVP transform. No changes from the forward path.

**Fragment shader (`gbuffer.frag.glsl`):** samples albedo, normal map, roughness/metallic textures from the material. Transforms normals to view space. Packs outputs into the four G-buffer attachment locations:

```glsl
layout(location = 0) out vec4 o_albedo_ao;         // RT0
layout(location = 1) out vec4 o_normals_rough;      // RT1
layout(location = 2) out vec4 o_metallic_emissive;  // RT2

void main() {
    vec3 albedo    = texture(u_albedo, v_uv).rgb;
    float ao       = texture(u_ao, v_uv).r;
    vec3 normal    = compute_view_space_normal(v_normal, v_tangent, v_uv);
    float rough    = texture(u_rough_metal, v_uv).g;
    float metallic = texture(u_rough_metal, v_uv).b;
    float emissive = texture(u_emissive, v_uv).r;

    o_albedo_ao        = vec4(albedo, ao);
    o_normals_rough    = vec4(normal, rough);
    o_metallic_emissive = vec4(metallic, emissive, 0.0, 0.0);
}
```

---

## 4. DeferredLightingPass

`DeferredLightingPass` is a full-screen triangle pass that reads the G-buffer and evaluates all PBR lighting. It consumes the `light_grid` and `light_index_list` buffers from `LightCullPass` to restrict per-pixel light iteration to only the lights affecting each tile.

```cpp
class DeferredLightingPass final : public IRenderGraphCallbackPass {
public:
    void Setup(Hardwares::VulkanDevicePtr const device, cstring name,
               RenderGraphResourceBuilderPtr const res_builder,
               RenderGraphResourceInspectorPtr res_inspector) override;

    void Compile(Hardwares::VulkanDevicePtr const device,
                 Rendering::Scenes::SceneDataPtr const scene,
                 RenderPasses::RenderPassBuilder* pass_builder,
                 RenderGraphResourceInspectorPtr res_inspector,
                 RenderPasses::RenderPass** const output_pass) override;

    void Execute(Hardwares::VulkanDevicePtr const device,
                 RenderGraphResourceInspectorPtr res_inspector,
                 Rendering::Scenes::SceneDataPtr const scene,
                 RenderPasses::RenderPass* const pass,
                 Buffers::FramebufferVNext* const framebuffer,
                 Hardwares::CommandBufferPtr const command_buffer) override;

    void Deinitialize(Hardwares::VulkanDevicePtr const device) override;
};
```

**Setup:**

```cpp
void DeferredLightingPass::Setup(Hardwares::VulkanDevicePtr const device, cstring name,
                                  RenderGraphResourceBuilderPtr const res_builder,
                                  RenderGraphResourceInspectorPtr res_inspector) {
    res_builder->ReadTexture("gbuffer_albedo_ao");
    res_builder->ReadTexture("gbuffer_normals_rough");
    res_builder->ReadTexture("gbuffer_metallic_emissive");
    res_builder->ReadDepth("hdr_depth");

    // Shadow map read — depends on ShadowPass completing first.
    res_builder->ReadTexture("shadow_map_directional");

    // Light culling buffer reads are declared here when LightCullPass is implemented.
    // See light-culling.md for the light_grid and light_index_list resource names.

    TextureSpecification hdr_spec = {};
    hdr_spec.Format               = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    res_builder->WriteColorAttachment("hdr_lit", hdr_spec);
}
```

Depth is read in `VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL` (MoltenVK compatible). `depthWriteEnable` is set to false in the pipeline spec.

Before `Compile` is called, the RenderGraph pre-populates the `RenderPassBuilder` with one `AddInputAttachment` call per declared read and one `UseRenderTarget` call for the declared write. `Compile` extends the builder with pipeline state:

```cpp
void DeferredLightingPass::Compile(Hardwares::VulkanDevicePtr const device,
                                    Rendering::Scenes::SceneDataPtr const scene,
                                    RenderPasses::RenderPassBuilder* pass_builder,
                                    RenderGraphResourceInspectorPtr res_inspector,
                                    RenderPasses::RenderPass** const output_pass) {
    pass_builder->SetPipelineName("deferred_lighting")
                 .EnablePipelineDepthTest(false)
                 .UseShader("fullscreen_triangle.vert", "deferred_lighting.frag")
                 .Detach(output_pass);
}
```

`Execute` retrieves texture handles via `res_inspector->GetTextureHandle(handle)` and binds them to the lighting descriptor set before recording the full-screen triangle draw.

**Fragment shader (`deferred_lighting.frag.glsl`):** Reconstructs world position from depth + inverse VP matrix. Reads G-buffer. Evaluates directional light (always applied, not tile-culled). Iterates tile-assigned point and spot lights via `light_grid`/`light_index_list`. Applies shadow map lookups. Applies lightmap if `LightmapComponent` data is packed into RT2 reserved bits.

Normal reconstruction from G-buffer (view-space to world-space):

```glsl
// Sample view-space normal from G-buffer RT1
vec3 view_normal = texture(u_gbuffer_normals_rough, v_uv).rgb * 2.0 - 1.0;

// Transform to world space before lighting — lights are in world space.
// u_inv_view is the inverse of the view matrix (camera transform).
// The .xyz of the result is the world-space normal (w=0 means direction, not position).
vec3 N = normalize((u_inv_view * vec4(view_normal, 0.0)).xyz);

// Use N for all lighting calculations below.
```

`u_inv_view` must be added to the descriptor set specification for the `DeferredLightingPass` UBO.

Position reconstruction from depth:

```glsl
vec3 reconstruct_position(vec2 uv, float depth) {
    // Vulkan NDC: xy in [-1, 1] (from uv * 2.0 - 1.0), depth in [0, 1] (native).
    // The inverse view-projection matrix was built with Vulkan depth convention [0,1].
    // Do NOT convert depth to [-1,1] (that is OpenGL convention and will produce wrong positions).
    // If reversed-Z is used (near=1, far=0): negate depth before use.
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = u_inv_view_proj * ndc;
    return world.xyz / world.w;
}
```

Note: depth must be in Vulkan range [0,1]. If reversed-Z optimization is enabled, pass `(1.0 - depth)` instead.

**NDC convention:** Vulkan depth is [0,1] natively. The reconstruction shader uses depth directly without conversion. If the project uses reversed-Z (depth_near=1, depth_far=0), pass `(1.0 - depth)` instead.

---

## 5. Transparent Objects

The standard deferred pipeline cannot shade transparent objects: the G-buffer stores only a single surface per pixel (the closest opaque surface). Transparent surfaces require order-dependent blending that conflicts with the G-buffer's write model.

**Solution:** all transparent objects (alpha-blended meshes, glass, foliage alpha cutout with blending, particle systems) are rendered in a separate forward pass after `DeferredLightingPass`. This forward pass:

1. Reads the `hdr_depth` depth buffer from `GBufferPass` for depth testing (transparent objects cannot occlude opaque geometry).
2. Writes to the `hdr_lit` HDR target with standard alpha blending enabled.
3. Uses a simplified forward lighting shader — transparent objects evaluate only the N nearest lights (configured per-project; default 4).

This is the standard hybrid deferred + forward transparency approach used by virtually all deferred-capable engines.

Alpha-cutout materials (masked) that do not require blending can be rendered in `GBufferPass` with `discard` — they behave as opaque surfaces for G-buffer purposes.

---

## 6. RenderGraph Integration

`GraphicRenderer::Initialize` is the orchestrator for pass registration. Passes are registered via `AddCallbackPass` and toggled with `SetPassEnabled`. The deferred path is disabled by default; enabling it requires disabling the forward geometry pass and enabling the deferred passes.

```cpp
void GraphicRenderer::Initialize(/* ... */) {
    // Forward path (enabled by default)
    m_render_graph->AddCallbackPass("DepthPrePass",    &m_depth_pre_pass,    true);
    m_render_graph->AddCallbackPass("GeometryPass",    &m_geometry_pass,     true);
    m_render_graph->AddCallbackPass("LightingPass",    &m_lighting_pass,     true);

    // Deferred path (disabled by default)
    m_render_graph->AddCallbackPass("GBufferPass",          &m_gbuffer_pass,          false);
    m_render_graph->AddCallbackPass("DeferredLightingPass", &m_deferred_lighting_pass, false);
    m_render_graph->AddCallbackPass("TransparentForwardPass", &m_transparent_pass,     false);

    // Common passes (both modes)
    m_render_graph->AddCallbackPass("BloomPass",    &m_bloom_pass,    true);
    m_render_graph->AddCallbackPass("TonemapPass",  &m_tonemap_pass,  true);
    m_render_graph->AddCallbackPass("UIPass",       &m_ui_pass,       true);
}
```

To switch to the deferred path at runtime, disable the forward geometry group and enable the deferred group:

```cpp
m_render_graph->SetPassEnabled("DepthPrePass", false);
m_render_graph->SetPassEnabled("GeometryPass", false);
m_render_graph->SetPassEnabled("LightingPass", false);

m_render_graph->SetPassEnabled("GBufferPass",           true);
m_render_graph->SetPassEnabled("DeferredLightingPass",  true);
m_render_graph->SetPassEnabled("TransparentForwardPass", true);
```

A full RenderGraph recompile is triggered on pass enable/disable changes. This should not be done during gameplay.

**`ZENGINE_DEFERRED` CMake flag:** not yet wired. When implemented, it will control whether the deferred passes are registered at all. Until then, deferred pass registration is controlled entirely through `SetPassEnabled`.

### 6.3 Descriptor Set Layout (DeferredLightingPass)

```
Set 0 — per-frame UBO:
  binding 0: CameraData  { mat4 view; mat4 proj; mat4 inv_view; mat4 inv_view_proj; vec3 cam_pos; }
  binding 1: LightBuffer { uint light_count; Light lights[MAX_LIGHTS]; }

Set 1 — G-buffer textures (sampler2D):
  binding 0: gbuffer_albedo_ao
  binding 1: gbuffer_normals_rough
  binding 2: gbuffer_metallic_emissive
  binding 3: hdr_depth

Set 2 — shadow maps:
  binding 0: shadow_map_directional

Set 3 — light culling (storage buffers, when LightCullPass is implemented):
  binding 0: light_grid        (readonly SSBO)
  binding 1: light_index_list  (readonly SSBO)
```

---

## 7. Memory Cost

G-buffer memory consumption at various resolutions:

| Resolution | RT0 (R8G8B8A8) | RT1 (R16G16B16A16F) | RT2 (R8G8B8A8) | Total G-buffer | Forward HDR RT | Net increase |
|---|---|---|---|---|---|---|
| 1920×1080 | 8 MB | 16 MB | 8 MB | 32 MB | 8 MB | +24 MB |
| 2560×1440 | 14 MB | 28 MB | 14 MB | 56 MB | 14 MB | +42 MB |
| 3840×2160 | 32 MB | 64 MB | 32 MB | 128 MB | 32 MB | +96 MB |

The depth buffer is shared with the forward depth pre-pass and is not an additional cost.

At 4K, the G-buffer costs 96 MB of VRAM. This must be accounted for in the project's memory budget. Cross-reference: `memory-budget.md` should reserve a `RENDER_GBUFFER` budget line of 128 MB (worst-case 4K with some headroom).

If VRAM is constrained, the deferred path should be restricted to PC configurations with 8GB+ VRAM. Console targets with unified memory budgets require separate analysis.

---

## 8. MSAA

MSAA (multi-sample anti-aliasing) is not compatible with standard deferred rendering. MSAA requires storing multiple samples per pixel in the G-buffer, which would multiply the G-buffer memory cost by 4x (4xMSAA) or 8x (8xMSAA) and complicate the lighting resolve.

**Alternatives for the deferred path:**

| Method | Quality | Cost | Status |
|---|---|---|---|
| No AA | Lowest | Zero | Available now |
| FXAA (screen-space) | Low | Minimal | Can be added as a post-process pass |
| TAA (temporal AA) | High | Low-Medium (history buffer, motion vectors) | Deferred to v2 |
| Deferred MSAA (per-sample shading) | High | Very high (N× shading cost) | Not recommended |

**Recommendation for deferred path v1:** ship with FXAA as a post-process pass. TAA produces higher quality and handles moving edges better; add it in v2 alongside the deferred path.

Motion vectors for TAA require writing a `"motion_vectors"` render target in `GBufferPass` (or a separate pass), which is straightforward to add — `GBufferPass` already has the previous-frame matrix available.

---

## 9. Migration from Forward

The deferred path is additive. Existing forward shaders are not modified. New deferred-path shaders (`gbuffer.vert.glsl`, `gbuffer.frag.glsl`, `deferred_lighting.frag.glsl`) are added alongside them.

`GraphicRenderer::Initialize` registers both the forward and deferred pass groups. Switching between them is a matter of toggling pass enabled state via `SetPassEnabled`, which triggers a RenderGraph recompile. See §6 for the full switching pattern.

**`ZENGINE_DEFERRED` CMake flag:** not yet wired. See §6.

**Shader variants:** G-buffer fragment shaders are separate files, not `#ifdef` variants of the forward fragment shader. This avoids a combinatorial explosion of shader permutations and keeps both paths readable independently.

---

## 10. File Layout

The layout below is the target directory structure for the deferred rendering feature. It does not exist yet. Current rendering passes are located in `ZEngine/ZEngine/Rendering/Renderers/RendererPasses.h` and `ZEngine/ZEngine/Rendering/Renderers/RendererPasses.cpp`.

```
ZEngine/Rendering/Deferred/          -- target layout; not yet created
    GBufferPass.h
    GBufferPass.cpp
    DeferredLightingPass.h
    DeferredLightingPass.cpp
    TransparentForwardPass.h
    TransparentForwardPass.cpp
    Shaders/
        gbuffer.vert.glsl
        gbuffer.frag.glsl
        deferred_lighting.frag.glsl
        deferred_common.glsl          -- position reconstruction, G-buffer unpack helpers
        transparent_forward.frag.glsl
```

---

## 11. Deliverables Checklist

- [ ] `GBufferPass.h` / `GBufferPass.cpp` — Setup / Compile / Execute, four G-buffer outputs
- [ ] `gbuffer.vert.glsl` / `gbuffer.frag.glsl` — PBR material packing into G-buffer layout
- [ ] `DeferredLightingPass.h` / `DeferredLightingPass.cpp` — full-screen lighting with tile-culled light iteration
- [ ] `deferred_lighting.frag.glsl` — G-buffer unpack, position reconstruction, PBR evaluation
- [ ] `TransparentForwardPass.h` / `TransparentForwardPass.cpp` — forward pass for alpha-blended objects
- [ ] `RenderingMode` enum and `GraphicRenderer::Initialize` deferred registration
- [ ] `ZENGINE_DEFERRED` CMake flag wired to pass registration
- [ ] `memory-budget.md` updated with `RENDER_GBUFFER` budget line
- [ ] FXAA post-process pass (or stub placeholder)
- [ ] Integration test: scene with 50 point lights renders correctly in deferred mode
- [ ] Visual parity test: same scene in forward and deferred mode, compare screenshots
- [ ] Memory usage verified against §7 budget table at 1080p and 1440p
- [ ] Transparent object test: glass mesh renders correctly after deferred lighting pass
