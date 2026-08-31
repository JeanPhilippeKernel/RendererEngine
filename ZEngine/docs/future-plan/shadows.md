# ZEngine — Shadow Mapping

**Priority:** P2 — Required for visual quality in any 3D game
**Status:** Design
**Depends on:** `gpu-allocator-rearchitecture.md`, `per-frame-upload-heap.md`, `actor-ecs-architecture.md` (LightComponent), `vfs-design.md` (Ticket 1)
**Blocks:** Visual quality, night/indoor scenes

**Goal**: Implement a multi-technique shadow system for ZEngine that covers the three
primary light types (directional, spot, point) using classical depth-map shadow techniques
in v1, with an architecture that accommodates PCSS, VSM, and ray-traced shadows in v2.
The implementation follows ZEngine conventions: no `new`/`delete`, no exceptions, no RTTI,
all GPU resources managed through `RenderGraph` resource declarations and the RRM, and all
CPU-side data owned by `ArenaAllocator`-backed containers.

---

## 1. Shadow Techniques Overview

### v1 — Implemented

| Technique | Light Type | Notes |
|---|---|---|
| Cascaded Shadow Maps (CSM) | Directional | 4 cascades, PCF 3×3 |
| Single 2D Shadow Map | Spot | 1 map per caster, capped at 4 |
| Cube Shadow Map | Point | 6 faces per caster, capped at 2 |

All v1 shadow maps use `VK_FORMAT_D32_SFLOAT`. PCF (Percentage Closer Filtering) with a
3×3 Poisson disk kernel is applied in the main lighting pass shader. Depth bias (constant
+ slope-scaled) is configured per-cascade and per-light-type to eliminate self-shadowing
artifacts without peter-panning.

### v2 — Deferred

| Technique | Description |
|---|---|
| PCSS (Percentage Closer Soft Shadows) | Variable-radius PCF based on blocker distance; requires a blocker search pass before the PCF gather |
| VSM (Variance Shadow Maps) | Stores `depth` and `depth²`; allows Gaussian blur on the shadow map; eliminates hard-coded kernel size |
| Ray-traced shadows | Via `VK_KHR_ray_tracing_pipeline`; returns binary or soft shadows per-pixel from TLAS traversal; no depth maps needed |
| Exponential Shadow Maps (ESM) | Alternative to VSM; single channel, very fast on mobile |

v2 techniques require either additional render passes (PCSS blocker pass, VSM blur pass)
or hardware ray-tracing support. They are out of scope for v1 but the architecture must
not preclude them: shadow map textures are accessed by handle, and the lighting pass
selects the sampling method via a specialization constant, so switching from PCF to PCSS
is a shader-level change that does not alter the C++ pass structure.

---

## 2. Cascaded Shadow Maps (CSM) for Directional Lights

### 2.1 Theory

A single shadow map for a directional light covering the entire view frustum wastes
texel resolution on distant geometry and under-samples objects near the camera. CSM
solves this by splitting the camera frustum into N sub-frusta (cascades), each covered by
its own depth map rendered from the light's perspective. The nearest cascade gets the
highest texel density; the farthest gets the lowest.

ZEngine v1 uses **N = 4** cascades by default (configurable via `ShadowQualityPreset`).

### 2.2 Cascade Split Scheme

ZEngine uses the **practical split scheme**, a weighted blend of the uniform (linear) and
logarithmic schemes:

```
split_i = lambda * near * (far / near)^(i / N)
        + (1 - lambda) * (near + (far - near) * (i / N))
```

where `lambda = 0.7` is the blend factor. A value of 1.0 gives pure logarithmic splits
(optimal for scenes with large depth range), while 0.0 gives uniform splits (better for
tight depth ranges). The blend at 0.7 is a good default for outdoor environments.

The split depths are stored in view space (positive Z, camera looks along +Z) and
compared against the fragment's depth during shadow lookup.

### 2.3 CSMData GPU Struct

```cpp
// ZEngine/Rendering/Shadows/ShadowData.h
#pragma once
#include <cstdint>
#include "Core/Maths/Mat4f.h"

namespace ZEngine::Rendering::Shadows {

    struct CascadeData {
        Core::Maths::Mat4f LightSpaceMatrix; // world → light-clip for this cascade
        float              SplitDepth;       // view-space far plane of this cascade
        float              Padding[3];       // std140 alignment to 16 bytes
    };

    // sizeof(CascadeData) == 80 bytes (64 + 4 + 12)

    struct CSMData {
        CascadeData Cascades[4];     // indexed [0] = nearest, [3] = farthest
        uint32_t    CascadeCount;    // 1–4; matches ShadowQualityPreset
        float       Padding[3];      // pad to 16-byte boundary
    };

    // sizeof(CSMData) == 80*4 + 16 == 336 bytes

} // namespace ZEngine::Rendering::Shadows
```

`LightSpaceMatrix` is the product `LightProj * LightView`. It transforms a world-space
position into the NDC of the cascade's depth map. In GLSL, the texture coordinate is
derived as:

```glsl
vec4 shadowCoord = cascade.LightSpaceMatrix * vec4(worldPos, 1.0);
shadowCoord.xyz /= shadowCoord.w;          // perspective divide (orthographic → no-op)
shadowCoord.xy  = shadowCoord.xy * 0.5 + 0.5; // NDC to UV [0,1]
```

### 2.4 Shadow Map Resolution

| Cascade | Default Resolution | Purpose |
|---|---|---|
| 0 | 2048 × 2048 | Near objects, highest quality |
| 1 | 2048 × 2048 | Mid-range |
| 2 | 2048 × 2048 | Far range |
| 3 | 2048 × 2048 | Horizon |

All cascades use the same resolution by default for simplicity in the descriptor layout.
Resolution is controlled by `ShadowQualityPreset` (see section 10).

### 2.5 Depth Bias

Self-shadowing (shadow acne) is eliminated by applying a depth bias during the shadow
map comparison. ZEngine applies both a constant bias and a slope-scaled bias:

```cpp
struct CascadeBiasParams {
    float ConstantBias; // added directly to stored depth
    float SlopeBias;    // multiplied by the rate-of-depth-change (dz/dx, dz/dy)
};

// Default values (tuned for 2048×2048 cascades):
constexpr CascadeBiasParams kDefaultCascadeBias[4] = {
    { 0.005f, 1.5f },  // cascade 0 — near, small bias needed
    { 0.005f, 1.5f },  // cascade 1
    { 0.008f, 2.0f },  // cascade 2 — larger texels, slightly more bias
    { 0.010f, 2.5f },  // cascade 3 — largest texels
};
```

In Vulkan the bias is applied via `VkPipelineRasterizationStateCreateInfo`:
```cpp
rasterInfo.depthBiasEnable         = VK_TRUE;
rasterInfo.depthBiasConstantFactor = bias.ConstantBias;
rasterInfo.depthBiasSlopeFactor    = bias.SlopeBias;
rasterInfo.depthBiasClamp          = 0.0f;
```

### 2.6 PCF — Percentage Closer Filtering

The main lighting pass samples the shadow map using a **3×3 Poisson disk** kernel. This
produces smooth shadow edges without the hard aliasing of a single sample.

```glsl
// 9-sample Poisson disk (radius scaled to shadow map texel size)
const vec2 kPoissonDisk[9] = vec2[](
    vec2( 0.000,  0.000),
    vec2( 0.527,  0.153),
    vec2(-0.527,  0.153),
    vec2( 0.000, -0.620),
    vec2( 0.834, -0.480),
    vec2(-0.834, -0.480),
    vec2( 0.361,  0.860),
    vec2(-0.361,  0.860),
    vec2( 0.000,  1.000)
);

float SampleShadowPCF(sampler2DShadow shadowMap, vec3 shadowCoord, float texelSize) {
    float shadow = 0.0;
    for (int i = 0; i < 9; ++i) {
        shadow += texture(shadowMap, vec3(
            shadowCoord.xy + kPoissonDisk[i] * texelSize,
            shadowCoord.z
        ));
    }
    return shadow / 9.0; // 1.0 = fully lit, 0.0 = fully shadowed
}
```

`sampler2DShadow` triggers hardware depth comparison so each `texture()` call returns
0.0 or 1.0 (with bilinear filtering producing fractional values on supporting hardware).

---

## 3. Shadow Render Passes

### 3.1 ShadowPassSpec

```cpp
// ZEngine/Rendering/Shadows/ShadowPassSpec.h
#pragma once
#include <cstdint>
#include <vulkan/vulkan.h>
#include "Core/Maths/Vec4f.h"

namespace ZEngine::Rendering::Shadows {

    struct ShadowPassSpec {
        uint32_t         Width           = 2048;
        uint32_t         Height          = 2048;
        VkFormat         DepthFormat     = VK_FORMAT_D32_SFLOAT;
        float            ConstantBias    = 0.005f;
        float            SlopeBias       = 1.5f;
        float            BiasClamp       = 0.0f;
        uint32_t         CascadeIndex    = 0;     // 0-3 for CSM; 0 for spot/point face
        bool             EnablePCF       = true;
    };

} // namespace ZEngine::Rendering::Shadows
```

### 3.2 ShadowPassNode

Each cascade (and each spot / point-face) is rendered by a dedicated `ShadowPassNode`
that implements `IRenderGraphCallbackPass`.

```cpp
// ZEngine/Rendering/Shadows/ShadowPassNode.h
#pragma once
#include "Rendering/RenderGraph/IRenderGraphCallbackPass.h"
#include "Rendering/Shadows/ShadowPassSpec.h"
#include "Rendering/Textures/TextureHandle.h"
#include "ECS/Scene.h"
#include "Core/Maths/Mat4f.h"

namespace ZEngine::Rendering::Shadows {

    class ShadowPassNode final : public IRenderGraphCallbackPass {
    public:
        explicit ShadowPassNode(
            const ShadowPassSpec&          spec,
            const Core::Maths::Mat4f&      lightSpaceMatrix,
            ECS::Scene*                    scene
        );

        // IRenderGraphCallbackPass
        void Setup(RenderGraphResourceBuilder& builder) override;
        void Execute(RenderGraphExecuteContext& ctx)    override;

        [[nodiscard]] Rendering::Textures::TextureHandle GetDepthOutput() const;

    private:
        ShadowPassSpec                    m_Spec;
        Core::Maths::Mat4f                m_LightSpaceMatrix;
        ECS::Scene*                       m_Scene;
        Rendering::Textures::TextureHandle m_DepthOutput;
    };

} // namespace ZEngine::Rendering::Shadows
```

### 3.3 Setup Phase

During `Setup`, the pass declares its output texture as a `RenderGraph` resource so the
graph can allocate the Vulkan image and schedule it in the correct barrier state before
`Execute` runs:

```cpp
void ShadowPassNode::Setup(RenderGraphResourceBuilder& builder) {
    Rendering::Specifications::TextureSpecification depthSpec{};
    depthSpec.Width      = m_Spec.Width;
    depthSpec.Height     = m_Spec.Height;
    depthSpec.Format     = m_Spec.DepthFormat;       // VK_FORMAT_D32_SFLOAT
    depthSpec.UsageFlags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                         | VK_IMAGE_USAGE_SAMPLED_BIT;
    depthSpec.SampleCount = VK_SAMPLE_COUNT_1_BIT;
    depthSpec.MipLevels   = 1;
    depthSpec.Layers      = 1;

    m_DepthOutput = builder.CreateTexture("ShadowDepth", depthSpec);
    builder.WriteDepthAttachment(m_DepthOutput);
}
```

### 3.4 Execute Phase

During `Execute`, the pass frustum-culls the scene against the cascade's light-space
frustum and renders all opaque meshes with a minimal depth-only vertex shader:

```cpp
void ShadowPassNode::Execute(RenderGraphExecuteContext& ctx) {
    // Bind shadow render pipeline (depth-only, no fragment shader)
    ctx.BindPipeline(ctx.GetShadowDepthPipeline());

    // Push light-space matrix as push constant
    ctx.PushConstants(m_LightSpaceMatrix);

    // Iterate scene; frustum cull against this cascade's light frustum
    m_Scene->ForEachEntity<
        ECS::Components::TransformComponent,
        ECS::Components::MeshComponent,
        ECS::Components::ShadowCasterComponent
    >([&](EntityID id,
          const ECS::Components::TransformComponent& transform,
          const ECS::Components::MeshComponent& mesh,
          const ECS::Components::ShadowCasterComponent& shadow)
    {
        if (!shadow.CastShadow) return;
        if (!FrustumCull(m_LightFrustum, transform, mesh.BoundingBox)) return;
        ctx.DrawMesh(mesh, transform.WorldMatrix);
    });
}
```

The pipeline for shadow passes has:
- No color attachments (`colorAttachmentCount = 0`)
- Depth write enabled
- Front-face culling to reduce peter-panning
- Depth bias configured from `ShadowPassSpec`

The vertex shader is a minimal two-instruction shader:
```glsl
// shadow_depth.vert
layout(push_constant) uniform PC { mat4 LightSpaceMatrix; } pc;
layout(location = 0) in vec3 inPosition;
void main() {
    gl_Position = pc.LightSpaceMatrix * ubo.ModelMatrix * vec4(inPosition, 1.0);
}
```

No fragment shader is bound; the rasterizer writes depth automatically.

### 3.5 CSM Output Handles

The CSM system exposes four shadow map handles for consumption by the lighting pass:

```cpp
struct CSMPassOutput {
    Rendering::Textures::TextureHandle ShadowMap[4]; // one per cascade
};
```

---

## 4. Spot Light Shadows

### 4.1 Overview

Each shadow-casting spot light uses a single **perspective** depth map. The light's
`innerAngle` and `outerAngle` define the field of view for the projection matrix.

Resolution: **1024 × 1024** by default. Cap: **4 simultaneous spot light shadows** in v1.
If more than 4 spot lights with `ShadowCasterComponent::CastShadow = true` exist in the
scene, the 4 closest to the camera are selected each frame.

### 4.2 SpotShadowData GPU Struct

```cpp
// ZEngine/Rendering/Shadows/ShadowData.h (continued)
namespace ZEngine::Rendering::Shadows {

    struct SpotShadowData {
        Core::Maths::Mat4f LightSpaceMatrix; // perspective proj * view from spot origin
        float              NearPlane;        // typically 0.1
        float              FarPlane;         // spot light range
        float              Padding[2];       // std140 alignment
    };

    // sizeof(SpotShadowData) == 80 bytes

} // namespace ZEngine::Rendering::Shadows
```

The projection matrix for a spot light is built as:

```cpp
float fovY = 2.0f * spotLight.OuterAngle; // full cone angle
Core::Maths::Mat4f proj = Core::Maths::Mat4f::Perspective(
    fovY, /*aspect*/ 1.0f, nearPlane, farPlane
);
// Vulkan Y-flip: proj[1][1] *= -1;
```

### 4.3 Pass Structure

One `ShadowPassNode` is instantiated per active shadow-casting spot light each frame.
The pass is identical to the CSM variant except:
- Resolution: `1024 × 1024`
- Light-space matrix: perspective, not orthographic
- Bias: `ConstantBias = 0.005f`, `SlopeBias = 2.0f` (perspective maps are more sensitive
  to slope bias)

---

## 5. Point Light Shadows

### 5.1 Overview

Point lights cast shadows in all directions. The standard technique is a **cube shadow
map**: render the scene six times, once for each face of a virtual cube centered on the
light, and store depth into a `VkImageViewType::VK_IMAGE_VIEW_TYPE_CUBE` image.

Resolution: **512 × 512** per face. Cap: **2 simultaneous point light shadow casters**
in v1 (12 render passes total at most). If more than 2 point lights with
`CastShadow = true` exist, the 2 closest to the camera are selected.

### 5.2 PointShadowData GPU Struct

```cpp
// ZEngine/Rendering/Shadows/ShadowData.h (continued)
namespace ZEngine::Rendering::Shadows {

    struct PointShadowData {
        Core::Maths::Mat4f FaceMatrices[6];  // proj * view for each cube face
        Core::Maths::Vec3f LightPosition;    // world-space light origin
        float              FarPlane;         // used for depth linearisation in shader
        float              Padding[0];       // Vec3f + float = 16 bytes, already aligned
    };

    // sizeof(PointShadowData) == 64*6 + 16 == 400 bytes

} // namespace ZEngine::Rendering::Shadows
```

static_assert(sizeof(PointShadowData) % 16 == 0,
    "PointShadowData must be 16-byte aligned for std140 UBO layout");
static_assert(offsetof(PointShadowData, FaceMatrices) == 0,
    "FaceMatrices must be at the start of PointShadowData for correct GPU layout");

The 6 view matrices correspond to the standard cube-map face directions:
```
+X: lookAt(pos, pos + Right,   -Up)
-X: lookAt(pos, pos - Right,   -Up)
+Y: lookAt(pos, pos + Up,     +Forward)
-Y: lookAt(pos, pos - Up,    -Forward)
+Z: lookAt(pos, pos + Forward, -Up)
-Z: lookAt(pos, pos - Forward, -Up)
```

All 6 faces share the same 90° FOV perspective projection with aspect = 1.0.

### 5.3 Pass Structure — 6 Draw Calls

In v1, ZEngine renders 6 separate `ShadowPassNode` instances per point light (one per
face), relying on multiview rendering or layer-targeted render passes via
`VkRenderingAttachmentInfo::imageView` pointing at the appropriate cube face layer.

A geometry shader alternative (broadcast one draw call to 6 layers via `gl_Layer`) is
noted as a v2 optimisation; it requires `VK_EXT_shader_viewport_index_layer` and
complicates the depth-only pipeline setup.

The point shadow pipeline uses **linear depth** instead of clip-space depth to make the
cube-map lookup distance comparison straightforward in the lighting shader:

```glsl
// point_shadow_depth.frag
layout(push_constant) uniform PC {
    vec3  LightPosition;
    float FarPlane;
} pc;

void main() {
    float dist = length(fragWorldPos - pc.LightPosition);
    gl_FragDepth = dist / pc.FarPlane; // normalise to [0,1]
}
```

---

## 6. Shadow ECS Components

Two ECS components participate in the shadow system.

### 6.1 ShadowCasterComponent

Attached to **mesh entities** that should cast shadows. A mesh without this component
is invisible to shadow passes.

```cpp
// ZEngine/ECS/Components/ShadowCasterComponent.h
#pragma once

namespace ZEngine::ECS::Components {

    struct ShadowCasterComponent {
        bool  CastShadow  = true;
        float ShadowBias  = 0.005f; // per-object constant bias override
        float SlopeBias   = 1.5f;   // per-object slope bias override
    };

} // namespace ZEngine::ECS::Components
```

Per-object bias overrides allow fine-tuning without touching global defaults. A zero-bias
object (e.g., a flat plane) would set `ShadowBias = 0.001f` to prevent shadow acne
without introducing peter-panning.

### 6.2 LightComponent Shadow Fields

The existing `ECS::Components::LightComponent` is extended with shadow fields:

```cpp
// Addition to LightComponent (in actor-ecs-architecture.md)
struct LightShadowSettings {
    bool     EnableShadows     = false;
    uint32_t ShadowMapIndex    = UINT32_MAX; // assigned by ShadowSystem, read-only
    float    NearPlane         = 0.1f;
    float    FarPlane          = 50.0f;
};

// Added as a field in LightComponent:
LightShadowSettings Shadow;
```

The `ShadowMapIndex` field is written by the `ShadowSystem` during frame setup so that
the lighting pass can look up the correct `SpotShadowData` or `PointShadowData` slot.

---

## 7. Shader Integration — Lighting Pass Shadow Sampling

### 7.1 Descriptor Bindings

The main lighting pass fragment shader receives all shadow data via a single uniform
buffer and an array of shadow map textures.

```glsl
// Binding layout in lighting.frag
layout(set = 1, binding = 0) uniform ShadowUBO {
    // see section 8 for full struct
    CascadeData DirectionalCascades[4];
    uint        CascadeCount;
    // ... spot and point data follow
} shadowUbo;

layout(set = 1, binding = 1) uniform sampler2DShadow DirectionalShadowMaps[4];
layout(set = 1, binding = 2) uniform sampler2DShadow SpotShadowMaps[4];
layout(set = 1, binding = 3) uniform samplerCube     PointShadowMaps[2];
```

### 7.2 CSM Cascade Selection

```glsl
int GetCascadeIndex(float viewDepth) {
    for (int i = 0; i < int(shadowUbo.CascadeCount); ++i) {
        if (viewDepth < shadowUbo.DirectionalCascades[i].SplitDepth)
            return i;
    }
    return int(shadowUbo.CascadeCount) - 1;
}
```

### 7.3 Full CSM Shadow Lookup with PCF

```glsl
float ComputeDirectionalShadow(vec3 worldPos, float viewDepth) {
    int   cascadeIdx  = GetCascadeIndex(viewDepth);
    vec4  shadowCoord = shadowUbo.DirectionalCascades[cascadeIdx].LightSpaceMatrix
                      * vec4(worldPos, 1.0);
    shadowCoord.xyz /= shadowCoord.w;
    shadowCoord.xy   = shadowCoord.xy * 0.5 + 0.5;

    // Reject fragments outside shadow map bounds
    if (shadowCoord.z > 1.0 || shadowCoord.z < 0.0)
        return 1.0;

    // PCF 3x3 Poisson disk
    float texelSize = 1.0 / float(SHADOW_MAP_RESOLUTION);
    float shadow    = 0.0;

    const vec2 kPoissonDisk[9] = vec2[](
        vec2( 0.000,  0.000), vec2( 0.527,  0.153), vec2(-0.527,  0.153),
        vec2( 0.000, -0.620), vec2( 0.834, -0.480), vec2(-0.834, -0.480),
        vec2( 0.361,  0.860), vec2(-0.361,  0.860), vec2( 0.000,  1.000)
    );

    for (int i = 0; i < 9; ++i) {
        shadow += texture(DirectionalShadowMaps[cascadeIdx], vec3(
            shadowCoord.xy + kPoissonDisk[i] * texelSize,
            shadowCoord.z
        ));
    }
    return shadow / 9.0;
}
```

### 7.4 Cascade Blend

At cascade boundaries, a 10% overlap zone blends between adjacent cascades to eliminate
the visible seam where shadow quality transitions:

```glsl
float blendRange = 0.1; // 10% of cascade depth range
float splitDepth = shadowUbo.DirectionalCascades[cascadeIdx].SplitDepth;
float blendFactor = smoothstep(
    splitDepth * (1.0 - blendRange),
    splitDepth,
    viewDepth
);

if (blendFactor > 0.0 && cascadeIdx < int(shadowUbo.CascadeCount) - 1) {
    float shadow1 = ComputeCascadeSample(cascadeIdx,     worldPos);
    float shadow2 = ComputeCascadeSample(cascadeIdx + 1, worldPos);
    return mix(shadow1, shadow2, blendFactor);
}
```

### 7.5 Spot Light Shadow Lookup

```glsl
float ComputeSpotShadow(int lightIdx, vec3 worldPos) {
    SpotShadowData spot = shadowUbo.SpotShadows[lightIdx];
    vec4 shadowCoord    = spot.LightSpaceMatrix * vec4(worldPos, 1.0);
    shadowCoord.xyz    /= shadowCoord.w;
    shadowCoord.xy      = shadowCoord.xy * 0.5 + 0.5;

    if (any(lessThan(shadowCoord.xy, vec2(0.0))) ||
        any(greaterThan(shadowCoord.xy, vec2(1.0))))
        return 1.0;

    float texelSize = 1.0 / float(SPOT_SHADOW_MAP_RESOLUTION);
    float shadow    = 0.0;
    for (int i = 0; i < 9; ++i) {
        shadow += texture(SpotShadowMaps[lightIdx], vec3(
            shadowCoord.xy + kPoissonDisk[i] * texelSize,
            shadowCoord.z
        ));
    }
    return shadow / 9.0;
}
```

### 7.6 Point Light Shadow Lookup

Point light shadows use a **cube map** sampler. The lookup vector is the world-space
displacement from the light to the fragment. Depth is linearised using the stored far
plane:

```glsl
float ComputePointShadow(int lightIdx, vec3 worldPos) {
    PointShadowData pt  = shadowUbo.PointShadows[lightIdx];
    vec3  dir           = worldPos - pt.LightPosition;
    float currentDepth  = length(dir) / pt.FarPlane;

    // Simple single-sample lookup (v1; PCF for cube maps is a v2 upgrade)
    float closestDepth  = texture(PointShadowMaps[lightIdx], dir).r;
    float bias          = 0.05;
    return currentDepth - bias > closestDepth ? 0.0 : 1.0;
}
```

---

## 8. GPU Data Layout — ShadowUniformBuffer

The full uniform buffer pushed to the lighting pass descriptor set:

```cpp
// ZEngine/Rendering/Shadows/ShadowUniformBuffer.h
#pragma once
#include "Rendering/Shadows/ShadowData.h"

namespace ZEngine::Rendering::Shadows {

    struct ShadowUniformBuffer {
        // Directional light (CSM)
        CascadeData  DirectionalCascades[4];      // 80 * 4 = 320 bytes
        uint32_t     CascadeCount;                // 4 bytes
        float        DirectionalPadding[3];       // 12 bytes — pad to 16

        // Spot lights
        SpotShadowData SpotShadows[4];            // 80 * 4 = 320 bytes
        uint32_t       ActiveSpotShadowCount;     // 4 bytes
        float          SpotPadding[3];            // 12 bytes

        // Point lights
        PointShadowData PointShadows[2];          // 400 * 2 = 800 bytes
        uint32_t        ActivePointShadowCount;   // 4 bytes
        float           PointPadding[3];          // 12 bytes

        // Total: 320 + 16 + 320 + 16 + 800 + 16 = 1488 bytes
    };

} // namespace ZEngine::Rendering::Shadows
```

This buffer is uploaded once per frame via `PerFrameUploadHeap::Push`. The
`ShadowSystem` fills it on the CPU side each frame and pushes it into the current
frame's heap. The resulting dynamic offset is passed to `vkCmdBindDescriptorSets` at the
lighting pass bind point.

The RRM is not involved in `ShadowUniformBuffer` upload. The RRM manages static asset
lifetime (textures, meshes); per-frame CPU-written data always goes through
`PerFrameUploadHeap`.

---

## 9. RenderGraph Integration

### 9.1 Pass Ordering

Shadow passes are registered in the RenderGraph before the main geometry pass. The graph
scheduler guarantees execution order via resource dependencies: the shadow map textures
are declared as outputs of shadow passes and inputs of the lighting pass.

```
[ShadowPass CSM 0] ──► ShadowMap[0] ──┐
[ShadowPass CSM 1] ──► ShadowMap[1] ──┤
[ShadowPass CSM 2] ──► ShadowMap[2] ──┤──► [Lighting Pass]
[ShadowPass CSM 3] ──► ShadowMap[3] ──┤
[ShadowPass Spot 0..3] ► SpotMap[0..3]┤
[ShadowPass Point 0..1]► CubeMap[0..1]┘
```

### 9.2 Registration Example

```cpp
// In RenderGraphBuilder (called once on scene load or quality change)
void ShadowSystem::RegisterPasses(RenderGraph& graph, ECS::Scene* scene) {
    // CSM — directional light
    for (uint32_t i = 0; i < m_CascadeCount; ++i) {
        ShadowPassSpec spec{};
        spec.Width        = m_ShadowResolution;
        spec.Height       = m_ShadowResolution;
        spec.CascadeIndex = i;

        auto* node = m_Arena.New<ShadowPassNode>(spec, m_CSMData.Cascades[i].LightSpaceMatrix, scene);
        graph.AddPass(node);
    }

    // Spot lights
    uint32_t spotCount = Min(m_ActiveSpotLights.Size(), uint32_t(4));
    for (uint32_t i = 0; i < spotCount; ++i) {
        // ... similar registration
    }

    // Point lights
    uint32_t ptCount = Min(m_ActivePointLights.Size(), uint32_t(2));
    for (uint32_t i = 0; i < ptCount; ++i) {
        for (uint32_t face = 0; face < 6; ++face) {
            // one pass per cube face
        }
    }
}
```

### 9.3 Texture Lifetime

Shadow map textures are declared as **transient** resources in the RenderGraph — they are
valid only within the frame they are produced. They are not retained between frames.
If the scene has no shadow-casting directional light, the CSM passes are skipped and the
`DirectionalShadowMaps` descriptor slots are bound to a 1×1 black depth texture.

---

## 10. Shadow Quality Settings

```cpp
// ZEngine/Rendering/Shadows/ShadowQuality.h
#pragma once
#include <cstdint>

namespace ZEngine::Rendering::Shadows {

    enum class ShadowQualityPreset : uint8_t {
        Low    = 0,  // 1 cascade, 512×512 per map
        Medium = 1,  // 2 cascades, 1024×1024 per map
        High   = 2,  // 4 cascades, 2048×2048 per map  (default)
        Ultra  = 3,  // 4 cascades, 4096×4096 per map
    };

    struct ShadowQualitySettings {
        uint32_t CascadeCount;
        uint32_t ShadowMapResolution;
        uint32_t SpotMapResolution;
        uint32_t PointMapResolution;
    };

    constexpr ShadowQualitySettings kShadowQualityTable[] = {
        // Low
        { 1, 512,  512,  256  },
        // Medium
        { 2, 1024, 1024, 512  },
        // High
        { 4, 2048, 1024, 512  },
        // Ultra
        { 4, 4096, 2048, 1024 },
    };

    inline ShadowQualitySettings GetShadowQuality(ShadowQualityPreset preset) {
        return kShadowQualityTable[static_cast<uint8_t>(preset)];
    }

} // namespace ZEngine::Rendering::Shadows
```

Quality changes are applied at the start of the next frame: the RenderGraph is rebuilt
with the new pass specs, old transient textures are freed, and new ones are allocated.
No mid-frame rebuild is needed because shadow maps are transient resources.

---

## 11. File Layout

```
ZEngine/
└── Rendering/
    └── Shadows/
        ├── ShadowData.h              — CascadeData, CSMData, SpotShadowData, PointShadowData
        ├── ShadowUniformBuffer.h     — ShadowUniformBuffer (full GPU struct)
        ├── ShadowPassSpec.h          — ShadowPassSpec (pass configuration)
        ├── ShadowPassNode.h          — ShadowPassNode class declaration
        ├── ShadowPassNode.cpp        — Setup/Execute implementation
        ├── ShadowQuality.h           — ShadowQualityPreset enum + table
        ├── ShadowSystem.h            — ShadowSystem class (CSM split, pass registration)
        ├── ShadowSystem.cpp          — Cascade frustum computation, light-space matrix build
        └── ShadowCasterComponent.h   — (or lives under ECS/Components/)
ZEngine/
└── ECS/
    └── Components/
        └── ShadowCasterComponent.h   — ShadowCasterComponent struct
ZEngine/
└── Assets/
    └── Shaders/
        ├── shadow_depth.vert         — Depth-only vertex shader (CSM + spot)
        ├── point_shadow_depth.vert   — Point light depth vertex shader
        └── point_shadow_depth.frag   — Linearised depth write
```

---

## 12. Deliverables Checklist

- [ ] `ShadowData.h` — all GPU structs (`CascadeData`, `CSMData`, `SpotShadowData`, `PointShadowData`)
- [ ] `ShadowUniformBuffer.h` — full 1488-byte aligned uniform buffer struct
- [ ] `ShadowPassSpec.h` — pass configuration struct
- [ ] `ShadowPassNode.h` / `.cpp` — depth-only render pass implementing `IRenderGraphCallbackPass`
- [ ] `ShadowQuality.h` — `ShadowQualityPreset` enum and lookup table
- [ ] `ShadowSystem.h` / `.cpp` — frustum splitting, light-space matrix construction, pass registration
- [ ] `ShadowCasterComponent.h` — ECS component for mesh and light entities
- [ ] `shadow_depth.vert` — minimal depth-only vertex shader (push-constant light-space matrix)
- [ ] `point_shadow_depth.vert` / `.frag` — linearised cube-map depth shader
- [ ] Lighting pass shader updated with CSM cascade selection, PCF lookup, spot/point sampling
- [ ] Descriptor set layout 1 wired with `ShadowUniformBuffer` binding + texture arrays
- [ ] RenderGraph integration: shadow passes registered before lighting pass; texture resource declarations
- [ ] `ShadowQualityPreset` exposed in engine settings and changeable at runtime
- [ ] 1×1 fallback depth texture for scenes without a directional light
- [ ] Unit test: cascade split values for lambda=0.7, near=0.1, far=1000, N=4
- [ ] Visual test: Cornell box scene with directional, spot, and point light; verify no acne, correct cascade transitions
