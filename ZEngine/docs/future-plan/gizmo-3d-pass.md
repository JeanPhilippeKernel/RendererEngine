# ZEngine — 3D Gizmo Pass

**Priority:** P2 — Required to drop ImGuizmo and therefore ImGui entirely  
**Status:** Design  
**Depends on:** `ui-system.md` (UIContext, all 8 steps done), `editor-entity-selection.md`, `actor-ecs-architecture.md` (ECS + TransformComponent), `physics-system.md`, `gpu-allocator-rearchitecture.md`  
**Blocks:** Complete removal of `imgui` and `ImGuizmo` from `__externals/`

---

## 1. Why this replaces ImGuizmo

ImGuizmo is structurally welded to ImGui. It draws through `ImDrawList`, reads input from
`ImGui::GetIO()`, and uses `ImGui::GetWindowPos()` for viewport origin. Remove ImGui and
ImGuizmo stops compiling. There is no injection point.

The 3D Gizmo Pass replaces **both** dependencies with engine-native equivalents:

| ImGuizmo responsibility | ZEngine replacement |
|---|---|
| Draw gizmo handles | `GizmoPass` — `IRenderGraphCallbackPass`, real 3D mesh geometry |
| Entity picking (raycast) | `EntityPickingPass` — GPU readback of R32_UINT entity ID buffer |
| Handle hover detection | Same picking buffer, gizmo handle IDs encoded with flag bit |
| Transform manipulation math | `GizmoInteraction` — axis projection, angle delta, scale ratio |
| Decompose matrix | `DecomposeTransform` — replaces `ImGuizmo::DecomposeMatrixToComponents` |

Additionally, this pass fixes two existing gaps in `editor-entity-selection.md`:
- The design doc assumes `transform->Position / Rotation / Scale` as public fields. The
  actual `TransformComponent` uses private fields with `GetPosition()` / `SetPosition()` etc.
  The gizmo interaction code in this doc uses the correct accessor API.
- `EditorSelectionContext` stores `ImGuizmo::OPERATION` and `ImGuizmo::MODE`. Those types
  are removed and replaced by `GizmoOperation` and `GizmoSpace` enums defined here.

---

## 2. System Overview

```
Frame N — main thread:

  InputFrame::BeginFrame()
  EntityPickingPass::Schedule()     ← reads last frame's readback result (non-stalling)
  EditorSelectionSystemTick()       ← updates selection + gizmo drag using picked ID
  UIContext::BeginFrame()
  ...
  UIContext::EndFrame()

  RenderGraph::Execute(cmd):
    DepthPrePass
    GeometryPass
    ...
    PostProcessPass
    EntityPickingPass::Execute()    ← renders entity+handle IDs to R32_UINT texture
    GizmoPass::Execute()            ← renders visible gizmo handles over scene
    UIPass::Execute()
    OverlayPass::Execute()

  EntityPickingPass::ScheduleReadback()  ← kicks async readback for Frame N+1
```

The picking readback is **one frame behind** — Frame N's readback result is read at the
start of Frame N+1. This eliminates any GPU stall. The one-frame lag is imperceptible at
60 fps.

---

## 3. Enumerations and GizmoID Encoding

### 3.1 Enumerations

```cpp
// ZEngine/ZEngine/Editor/Gizmo/GizmoTypes.h
#pragma once
#include <cstdint>

namespace ZEngine::Editor
{
    enum class GizmoOperation : uint8_t
    {
        Translate = 0,
        Rotate    = 1,
        Scale     = 2,
    };

    enum class GizmoSpace : uint8_t
    {
        World = 0,
        Local = 1,
    };

    enum class GizmoAxis : uint8_t
    {
        X        = 0,
        Y        = 1,
        Z        = 2,
        PlaneXY  = 3,   // translate: XY-plane square handle
        PlaneYZ  = 4,   // translate: YZ-plane square handle
        PlaneXZ  = 5,   // translate: XZ-plane square handle
        Free     = 6,   // rotate: free-rotate outer circle
        Uniform  = 7,   // scale: uniform scale center sphere
        None     = 0xFF,
    };
}
```

### 3.2 GizmoID encoding

The picking buffer uses one `uint32_t` per pixel. Bit 31 distinguishes gizmo handles
from entity IDs (entities use generational indices, all well below 2^31):

```
Bit 31 = 1  → gizmo handle
  Bits 0-2  : GizmoAxis (3 bits)
  Bits 3-4  : GizmoOperation (2 bits)
  Bits 5-30 : reserved (zero)

Bit 31 = 0  → entity ID (EntityID::Index packed as uint32)
  Value 0   : empty / no entity
```

```cpp
// ZEngine/ZEngine/Editor/Gizmo/GizmoTypes.h (continued)
namespace ZEngine::Editor
{
    static constexpr uint32_t k_GizmoIDFlag    = 0x80000000u;
    static constexpr uint32_t k_GizmoAxisMask  = 0x07u;
    static constexpr uint32_t k_GizmoOpMask    = 0x03u;
    static constexpr uint32_t k_GizmoOpShift   = 3u;
    static constexpr uint32_t k_EmptyPickID    = 0u;

    inline uint32_t MakeGizmoID(GizmoOperation op, GizmoAxis axis)
    {
        return k_GizmoIDFlag
             | (uint32_t(op)   << k_GizmoOpShift)
             | (uint32_t(axis) & k_GizmoAxisMask);
    }

    inline bool       IsGizmoHandle(uint32_t id)  { return (id & k_GizmoIDFlag) != 0; }
    inline GizmoAxis  GizmoAxisOf(uint32_t id)    { return GizmoAxis(id & k_GizmoAxisMask); }
    inline GizmoOperation GizmoOpOf(uint32_t id)  { return GizmoOperation((id >> k_GizmoOpShift) & k_GizmoOpMask); }
}
```

---

## 4. GizmoState

```cpp
// ZEngine/ZEngine/Editor/Gizmo/GizmoState.h
#pragma once
#include <ZEngine/Editor/Gizmo/GizmoTypes.h>
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/ECS/EntityID.h>

namespace ZEngine::Editor
{
    struct GizmoState
    {
        GizmoOperation Operation  = GizmoOperation::Translate;
        GizmoSpace     Space      = GizmoSpace::World;
        GizmoAxis      ActiveAxis = GizmoAxis::None;  // axis currently being dragged
        GizmoAxis      HoveredAxis= GizmoAxis::None;  // axis under cursor (from picking)

        bool IsDragging = false;

        // Drag start state — captured at mouse-press
        Core::Maths::Vec3f DragStartEntityPos     = {};
        Core::Maths::Vec3f DragStartEntityRot     = {};  // Euler radians
        Core::Maths::Vec3f DragStartEntityScale   = {};
        Core::Maths::Vec2f DragStartMousePos      = {};  // screen pixels

        // Screen-space projections of world-space axis at drag start.
        // Used to project mouse delta onto the correct axis.
        Core::Maths::Vec2f DragAxisScreenDir      = {};  // normalized 2D direction

        // How far along the axis the drag has moved since DragStartEntityPos
        float DragAccumulated = 0.f;

        // Angle accumulated during rotate drag (radians)
        float RotateAccumulated = 0.f;

        // Gizmo scale: computed from camera distance to entity each frame
        // so handles stay a fixed screen size regardless of zoom level.
        float ScreenScaleFactor = 1.f;
    };
}
```

---

## 5. GizmoGeometry — Static Handle Meshes

Pre-built, never re-allocated after initialization. Built from scratch (no assimp).

```cpp
// ZEngine/ZEngine/Editor/Gizmo/GizmoGeometry.h
#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <cstdint>

namespace ZEngine::Editor
{
    // A single vertex in the gizmo mesh.
    // No UV, no normal — gizmo shader uses world position and push constants only.
    struct GizmoVertex { Core::Maths::Vec3f Position; };

    struct GizmoMeshRange
    {
        uint32_t VertexOffset = 0;
        uint32_t VertexCount  = 0;
        uint32_t IndexOffset  = 0;
        uint32_t IndexCount   = 0;
    };

    // All six handle types share a single VkBuffer (vertex + index packed together).
    // Call GizmoMeshRange to retrieve the draw range for each handle.
    struct GizmoGeometry
    {
        void Initialize(Core::Memory::ArenaAllocator* arena,
                        Hardwares::VulkanDevice*       device);
        void Destroy(Hardwares::VulkanDevice* device);

        // Axis handles
        GizmoMeshRange Arrow;         // translate shaft + cone (X, Y, Z each use same mesh)
        GizmoMeshRange PlaneSquare;   // translate XY/YZ/XZ plane square
        GizmoMeshRange Ring;          // rotate ring
        GizmoMeshRange ScaleBox;      // scale end-box

        // Extra
        GizmoMeshRange CenterSphere;  // uniform scale / view-axis free rotate

        // Allocated via GpuAllocator (GpuMemoryDomain::DeviceGeometry).
        // Never use raw VkDeviceMemory — all memory is VMA-managed after
        // gpu-allocator-rearchitecture.md lands.
        VkBuffer       Buffer     = VK_NULL_HANDLE;
        VmaAllocation  Allocation = nullptr;

    private:
        void BuildArrow(Core::Memory::ArenaAllocator* scratch,
                        uint32_t shaft_segments, uint32_t cone_segments);
        void BuildPlaneSquare(Core::Memory::ArenaAllocator* scratch);
        void BuildRing(Core::Memory::ArenaAllocator* scratch, uint32_t segments);
        void BuildScaleBox(Core::Memory::ArenaAllocator* scratch);
        void BuildCenterSphere(Core::Memory::ArenaAllocator* scratch, uint32_t stacks);
        // Allocates Buffer via Device->GpuMem.AllocateBuffer(DeviceGeometry).
        // Uploads via Device->GpuMem.Ring (StagingRing) + vkCmdCopyBuffer.
        // Stores the resulting VkBuffer and VmaAllocation in this struct.
        void UploadToGPU(Core::Memory::ArenaAllocator* scratch,
                         Hardwares::VulkanDevice* device,
                         GizmoVertex* all_verts, uint32_t vert_count,
                         uint32_t* all_indices, uint32_t index_count);
    };
}
```

### Handle geometry parameters

| Handle | Shape | Parameters |
|---|---|---|
| Arrow | Cylinder shaft (8 segments) + cone tip (8 segments) | Shaft r=0.04, len=0.8; Cone r=0.08, len=0.2 |
| PlaneSquare | Flat quad (2 triangles) | 0.25 × 0.25 units, at 0.3 units offset from origin |
| Ring | Torus section (32 segments) | Major r=1.0, minor r=0.04 |
| ScaleBox | Cube | 0.12 × 0.12 × 0.12 units at 0.9 units from origin |
| CenterSphere | Sphere (16 stacks × 16 slices) | r=0.12 |

Axis handle transforms are baked by the gizmo shader via `u_Model` push constant.
Each of X/Y/Z uses the same mesh; the push constant rotates it 0°/90°/−90° around Z.

---

## 6. EntityPickingPass

### 6.1 Purpose

Renders entity IDs to a `VK_FORMAT_R32_UINT` attachment at full viewport resolution.
Also renders gizmo handle IDs (with `k_GizmoIDFlag`) in the same pass. After execution,
the pixel at the cursor position is read back to a host-visible 1×1 staging buffer.
The readback result is consumed at the top of the next frame.

### 6.2 Header

```cpp
// ZEngine/ZEngine/Editor/Gizmo/EntityPickingPass.h
#pragma once
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/Editor/Gizmo/GizmoTypes.h>
#include <ZEngine/Editor/Gizmo/GizmoGeometry.h>
#include <ZEngine/Editor/Gizmo/GizmoState.h>

namespace ZEngine::Editor
{
    class EntityPickingPass : public Rendering::Renderers::IRenderGraphCallbackPass
    {
    public:
        void Initialize(Core::Memory::ArenaAllocator* arena,
                        Hardwares::VulkanDevice*       device,
                        ECS::Scene*                    scene,
                        GizmoGeometry*                 gizmo_geo,
                        GizmoState*                    gizmo_state);
        void Destroy();

        // IRenderGraphCallbackPass
        void Setup(Hardwares::VulkanDevicePtr const device,
                   cstring name,
                   Rendering::Renderers::RenderGraphResourceBuilderPtr const builder,
                   Rendering::Renderers::RenderGraphResourceInspectorPtr inspector) override;

        void Compile(Hardwares::VulkanDevicePtr const device,
                     Rendering::Scenes::SceneDataPtr const scene,
                     Rendering::RenderPasses::RenderPassBuilder* pass_builder,
                     Rendering::Renderers::RenderGraphResourceInspectorPtr inspector,
                     Rendering::RenderPasses::RenderPass** output_pass) override;

        void Execute(Hardwares::VulkanDevicePtr const device,
                     Rendering::Renderers::RenderGraphResourceInspectorPtr inspector,
                     Rendering::Scenes::SceneDataPtr const scene,
                     Rendering::RenderPasses::RenderPass* const pass,
                     Rendering::Buffers::FramebufferVNext* const framebuffer,
                     Hardwares::CommandBufferPtr const cmd) override;

        const char* GetName() const override { return "EntityPickingPass"; }

        // Call at top of next frame (before EditorSelectionSystemTick).
        // Reads the readback buffer from last frame and returns the picked ID.
        // Returns k_EmptyPickID if the readback is not ready (first frame).
        uint32_t ConsumePickedID();

        // Schedule a readback of the pixel at `cursor_pos` after this frame's Execute.
        // cursor_pos is in viewport-local pixels (0,0 = top-left of viewport).
        void ScheduleReadback(Core::Maths::Vec2f cursor_pos);

    private:
        void RenderEntities(Hardwares::CommandBufferPtr cmd, Rendering::Scenes::SceneDataPtr scene);
        void RenderGizmoHandles(Hardwares::CommandBufferPtr cmd);
        void DoReadback(Hardwares::CommandBufferPtr cmd);

        Hardwares::VulkanDevice*  m_device       = nullptr;
        ECS::Scene*               m_scene        = nullptr;
        GizmoGeometry*            m_gizmo_geo    = nullptr;
        GizmoState*               m_gizmo_state  = nullptr;
        Core::Memory::ArenaAllocator* m_arena    = nullptr;

        // R32_UINT picking texture (full viewport resolution).
        // Allocated via GpuAllocator (GpuMemoryDomain::RenderTarget).
        VkImage       m_pick_img     = VK_NULL_HANDLE;
        VkImageView   m_pick_view    = VK_NULL_HANDLE;
        VmaAllocation m_pick_alloc   = nullptr;

        // D32_SFLOAT depth (same resolution as picking texture).
        VkImage       m_depth_img    = VK_NULL_HANDLE;
        VkImageView   m_depth_view   = VK_NULL_HANDLE;
        VmaAllocation m_depth_alloc  = nullptr;

        // 1×1 host-visible readback buffer (4 bytes = one uint32_t).
        // Allocated via GpuAllocator (GpuMemoryDomain::HostStaging).
        // Persistently mapped — MappedPtr held here after Initialize.
        VkBuffer      m_readback_buf = VK_NULL_HANDLE;
        VmaAllocation m_readback_alloc = nullptr;
        void*         m_readback_ptr = nullptr;  // persistently mapped

        // Pipeline for entity ID rendering
        VkPipeline            m_entity_pipeline = VK_NULL_HANDLE;
        VkPipeline            m_gizmo_pipeline  = VK_NULL_HANDLE;
        VkPipelineLayout      m_pipeline_layout = VK_NULL_HANDLE;

        // Cursor pixel to read back this frame
        Core::Maths::Vec2f    m_cursor_pos          = {};
        bool                  m_readback_scheduled   = false;
        bool                  m_readback_ready       = false;
        uint32_t              m_pending_picked_id    = k_EmptyPickID;

        VkRenderPass m_render_pass = VK_NULL_HANDLE;
        VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    };
}
```

### 6.3 Push constants

```cpp
// Entity pick push constant (32 bytes)
struct EntityPickPC
{
    Core::Maths::Mat4f MVP;       // 64 bytes — view_proj * model
    uint32_t           EntityID;  // packed entity index (bit 31 = 0)
    uint32_t           _pad[3];
};
// Total: 76 bytes — within 128-byte Vulkan minimum

// Gizmo handle pick push constant
struct GizmoPickPC
{
    Core::Maths::Mat4f MVP;       // 64 bytes
    uint32_t           HandleID;  // MakeGizmoID(op, axis) — bit 31 = 1
    uint32_t           _pad[3];
};
```

### 6.4 Shaders

```glsl
// editor_pick.vert
layout(push_constant) uniform PC { mat4 u_MVP; uint u_ID; uint _pad[3]; } pc;
layout(location=0) in vec3 a_Position;
void main() { gl_Position = pc.u_MVP * vec4(a_Position, 1.0); }

// editor_pick.frag
layout(push_constant) uniform PC { mat4 u_MVP; uint u_ID; uint _pad[3]; } pc;
layout(location=0) out uint out_ID;
void main() { out_ID = pc.u_ID; }
```

Pipeline: depth test LESS_OR_EQUAL, depth write enabled.
Separate pipeline for gizmo handles: depth test ALWAYS (always win over scene), depth write disabled — gizmo handles must be pickable even when visually in front of scene geometry.

### 6.5 Readback mechanism

```
Execute():
  vkCmdBeginRenderPass(cmd, R32_UINT + D32_SFLOAT)
    For each entity with MeshComponent + TransformComponent:
      vkCmdPushConstants(cmd, ..., EntityPickPC{MVP, entityIndex})
      vkCmdDraw(cmd, ...)
    For each gizmo handle (if selection active):
      vkCmdPushConstants(cmd, ..., GizmoPickPC{MVP, MakeGizmoID(op, axis)})
      vkCmdDraw(cmd, ...)
  vkCmdEndRenderPass(cmd)

  if m_readback_scheduled:
    VkBufferImageCopy region;
    region.imageOffset = { (int32_t)m_cursor_pos.x, (int32_t)m_cursor_pos.y, 0 };
    region.imageExtent = { 1, 1, 1 };
    region.bufferOffset = 0;
    // Transition pick image to TRANSFER_SRC
    vkCmdCopyImageToBuffer(cmd, m_pick_img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           m_readback_buf, 1, &region)
    m_readback_ready = true
    m_readback_scheduled = false

ConsumePickedID():
  if !m_readback_ready: return k_EmptyPickID
  m_readback_ready = false
  uint32_t id = 0
  memcpy(&id, m_readback_ptr, sizeof(uint32_t))
  return id
```

The readback uses a persistently mapped `HOST_VISIBLE | HOST_COHERENT` buffer. No
`vkMapMemory` on the hot path. The CPU reads it at the top of the next frame — the GPU
has already finished writing it by then because the RenderGraph fence has signalled.

---

## 7. GizmoPass

### 7.1 Purpose

Renders the gizmo handle meshes visible in the viewport. Always draws on top of scene
geometry (depth test disabled for handles, or manual depth offset). Colors:

| Axis / state | Color |
|---|---|
| X axis, idle | `(0.85, 0.15, 0.15, 1)` — red |
| Y axis, idle | `(0.15, 0.85, 0.15, 1)` — green |
| Z axis, idle | `(0.15, 0.15, 0.85, 1)` — blue |
| Any axis, hovered | `(1.0,  0.85, 0.2,  1)` — yellow |
| Any axis, dragging | `(1.0,  1.0,  0.2,  1)` — bright yellow |
| Center sphere | `(0.9,  0.9,  0.9,  1)` — white |

### 7.2 Header

```cpp
// ZEngine/ZEngine/Editor/Gizmo/GizmoPass.h
#pragma once
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Editor/Gizmo/GizmoGeometry.h>
#include <ZEngine/Editor/Gizmo/GizmoState.h>
#include <ZEngine/Editor/Selection/EditorSelectionState.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/Rendering/Cameras/Camera.h>

namespace ZEngine::Editor
{
    class GizmoPass : public Rendering::Renderers::IRenderGraphCallbackPass
    {
    public:
        void Initialize(Core::Memory::ArenaAllocator* arena,
                        Hardwares::VulkanDevice*       device,
                        GizmoGeometry*                 geo,
                        GizmoState*                    state,
                        EditorSelectionState*          selection);
        void Destroy();

        void SetScene(ECS::Scene* scene)               { m_scene  = scene; }
        void SetCamera(Rendering::Cameras::Camera* cam){ m_camera = cam;   }

        // IRenderGraphCallbackPass
        void Setup(Hardwares::VulkanDevicePtr const device,
                   cstring name,
                   Rendering::Renderers::RenderGraphResourceBuilderPtr const builder,
                   Rendering::Renderers::RenderGraphResourceInspectorPtr inspector) override;

        void Compile(Hardwares::VulkanDevicePtr const device,
                     Rendering::Scenes::SceneDataPtr const scene,
                     Rendering::RenderPasses::RenderPassBuilder* pass_builder,
                     Rendering::Renderers::RenderGraphResourceInspectorPtr inspector,
                     Rendering::RenderPasses::RenderPass** output_pass) override;

        void Execute(Hardwares::VulkanDevicePtr const device,
                     Rendering::Renderers::RenderGraphResourceInspectorPtr inspector,
                     Rendering::Scenes::SceneDataPtr const scene,
                     Rendering::RenderPasses::RenderPass* const pass,
                     Rendering::Buffers::FramebufferVNext* const framebuffer,
                     Hardwares::CommandBufferPtr const cmd) override;

        const char* GetName() const override { return "GizmoPass"; }

    private:
        void DrawTranslateGizmo(Hardwares::CommandBufferPtr cmd,
                                const Core::Maths::Mat4f& view_proj,
                                const Core::Maths::Vec3f& entity_pos);
        void DrawRotateGizmo   (Hardwares::CommandBufferPtr cmd,
                                const Core::Maths::Mat4f& view_proj,
                                const Core::Maths::Vec3f& entity_pos,
                                const Core::Maths::Quaternion<float>& entity_rot);
        void DrawScaleGizmo    (Hardwares::CommandBufferPtr cmd,
                                const Core::Maths::Mat4f& view_proj,
                                const Core::Maths::Vec3f& entity_pos,
                                const Core::Maths::Vec3f& entity_scale);

        Core::Maths::Vec4f HandleColor(GizmoAxis axis) const;
        float              ComputeScreenScale(const Core::Maths::Vec3f& entity_pos) const;

        GizmoGeometry*             m_geo       = nullptr;
        GizmoState*                m_state     = nullptr;
        EditorSelectionState*      m_selection = nullptr;
        ECS::Scene*                m_scene     = nullptr;
        Rendering::Cameras::Camera* m_camera   = nullptr;
        Hardwares::VulkanDevice*   m_device    = nullptr;

        VkPipeline       m_pipeline        = VK_NULL_HANDLE;
        VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;

        // Camera UBO descriptor (same view/proj used by scene passes)
        VkDescriptorSetLayout m_desc_layout = VK_NULL_HANDLE;
        VkDescriptorSet       m_desc_set    = VK_NULL_HANDLE;
    };
}
```

### 7.3 Gizmo push constants

```cpp
struct GizmoPushConstants
{
    Core::Maths::Mat4f Model;   // 64 bytes — positions/orients the handle
    Core::Maths::Vec4f Color;   // 16 bytes
    // Total: 80 bytes
};
static_assert(sizeof(GizmoPushConstants) <= 128);
```

### 7.4 Shader

```glsl
// editor_gizmo.vert
layout(push_constant) uniform PC { mat4 u_Model; vec4 u_Color; } pc;
layout(set=0, binding=0) uniform CameraUBO { mat4 u_View; mat4 u_Projection; } cam;
layout(location=0) in vec3 a_Position;
layout(location=0) out vec4 v_Color;
void main() {
    gl_Position = cam.u_Projection * cam.u_View * pc.u_Model * vec4(a_Position, 1.0);
    v_Color = pc.u_Color;
}

// editor_gizmo.frag
layout(location=0) in  vec4 v_Color;
layout(location=0) out vec4 out_Color;
void main() { out_Color = v_Color; }
```

Pipeline: depth test ALWAYS (always render on top), depth write disabled, blend disabled.
Line width 2.0 for ring outlines (where hardware supports it; fallback to triangulated
tubes on platforms without wide-line support — Metal/MoltenVK).

### 7.5 Translate gizmo draw (example)

```cpp
void GizmoPass::DrawTranslateGizmo(
    Hardwares::CommandBufferPtr     cmd,
    const Core::Maths::Mat4f&       view_proj,
    const Core::Maths::Vec3f&       pos)
{
    using namespace Core::Maths;
    const float s = m_state->ScreenScaleFactor;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline_layout, 0, 1, &m_desc_set, 0, nullptr);
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_geo->Buffer, &kZeroOffset);
    vkCmdBindIndexBuffer(cmd, m_geo->Buffer, m_geo->Arrow.IndexOffset * sizeof(uint32_t),
                         VK_INDEX_TYPE_UINT32);

    // ── X arrow ──────────────────────────────────────────────────────────────
    GizmoPushConstants pc;
    // Arrow points along +X: no rotation needed; scale by s; place at entity pos
    pc.Model = TRS(pos, Quaternion<float>::Identity(), Vec3f{s, s, s});
    pc.Color = HandleColor(GizmoAxis::X);
    vkCmdPushConstants(cmd, m_pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cmd, m_geo->Arrow.IndexCount, 1,
                     m_geo->Arrow.IndexOffset, m_geo->Arrow.VertexOffset, 0);

    // ── Y arrow ──────────────────────────────────────────────────────────────
    // Arrow mesh points along +X; rotate −90° around Z to point along +Y
    pc.Model = TRS(pos,
        Quaternion<float>::FromEuler(0.f, 0.f, -PI_F * 0.5f),
        Vec3f{s, s, s});
    pc.Color = HandleColor(GizmoAxis::Y);
    vkCmdPushConstants(cmd, m_pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cmd, m_geo->Arrow.IndexCount, 1,
                     m_geo->Arrow.IndexOffset, m_geo->Arrow.VertexOffset, 0);

    // ── Z arrow ──────────────────────────────────────────────────────────────
    // Rotate +90° around Y to point along +Z
    pc.Model = TRS(pos,
        Quaternion<float>::FromEuler(0.f, PI_F * 0.5f, 0.f),
        Vec3f{s, s, s});
    pc.Color = HandleColor(GizmoAxis::Z);
    vkCmdPushConstants(cmd, m_pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cmd, m_geo->Arrow.IndexCount, 1,
                     m_geo->Arrow.IndexOffset, m_geo->Arrow.VertexOffset, 0);

    // ── Plane squares (XY, YZ, XZ) ────────────────────────────────────────
    // ... similar pattern, use PlaneSquare mesh with appropriate rotation
}
```

---

## 8. GizmoInteraction — Transform Manipulation Math

This is the hottest code path. No allocation, no virtual calls.

```cpp
// ZEngine/ZEngine/Editor/Gizmo/GizmoInteraction.h
#pragma once
#include <ZEngine/Editor/Gizmo/GizmoTypes.h>
#include <ZEngine/Editor/Gizmo/GizmoState.h>
#include <ZEngine/Rendering/Cameras/Camera.h>
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/Core/Maths/Matrix.h>

namespace ZEngine::Editor::GizmoInteraction
{
    // Called on mouse press when a gizmo handle is picked.
    // Captures drag start state from the entity's current transform.
    void BeginDrag(GizmoState*                    state,
                   GizmoAxis                       axis,
                   const Core::Maths::Vec3f&       entity_pos,
                   const Core::Maths::Vec3f&       entity_rot_euler,
                   const Core::Maths::Vec3f&       entity_scale,
                   const Core::Maths::Vec2f&       mouse_screen_pos,
                   const Rendering::Cameras::Camera& camera,
                   const Core::Maths::Vec2f&       viewport_size);

    // Called each frame while dragging. Computes the new transform values.
    // Writes out_pos / out_rot_euler / out_scale — call SetPosition etc. on TransformComponent.
    void UpdateDrag(const GizmoState*               state,
                    const Core::Maths::Vec2f&        mouse_screen_pos,
                    const Rendering::Cameras::Camera& camera,
                    const Core::Maths::Vec2f&        viewport_size,
                    Core::Maths::Vec3f&              out_pos,
                    Core::Maths::Vec3f&              out_rot_euler,
                    Core::Maths::Vec3f&              out_scale);

    // Called on mouse release.
    void EndDrag(GizmoState* state);

    // Project a world-space axis onto screen space.
    // Returns a normalized 2D direction vector in screen pixels.
    Core::Maths::Vec2f ProjectAxisToScreen(
        const Core::Maths::Vec3f&       world_pos,
        const Core::Maths::Vec3f&       axis_dir,
        const Rendering::Cameras::Camera& camera,
        const Core::Maths::Vec2f&        viewport_size);

    // Compute screen-space scale factor so gizmo handles stay a fixed visual size.
    float ComputeScreenScale(
        const Core::Maths::Vec3f&       entity_pos,
        const Rendering::Cameras::Camera& camera,
        float                            target_screen_pixels = 100.f);
}
```

### 8.1 Translate drag

```
BeginDrag:
  state->DragAxisScreenDir = ProjectAxisToScreen(entity_pos, WorldAxis(axis), camera, vp)
  state->DragStartEntityPos = entity_pos
  state->DragStartMousePos  = mouse_pos
  state->DragAccumulated    = 0
  state->IsDragging         = true

UpdateDrag (translate):
  delta_px = mouse_pos - state->DragStartMousePos
  // Project delta onto the screen-space axis direction
  state->DragAccumulated = Dot(delta_px, state->DragAxisScreenDir)
  // Scale from pixels to world units via camera distance
  world_per_px = ComputeWorldUnitsPerPixel(state->DragStartEntityPos, camera, vp)
  displacement = state->DragAccumulated * world_per_px
  out_pos = state->DragStartEntityPos + WorldAxis(axis) * displacement
```

For plane handles (XY/YZ/XZ): unproject mouse position onto the plane rather than axis.

### 8.2 Rotate drag

```
BeginDrag (rotate):
  // Capture the vector from entity center to cursor in screen space.
  // Angle delta = change in angle of this vector over time.
  screen_center = WorldToScreen(entity_pos, camera, vp)
  start_angle   = Atan2(mouse_pos - screen_center)
  state->DragStartMousePos = mouse_pos
  state->RotateAccumulated = 0

UpdateDrag (rotate):
  screen_center  = WorldToScreen(state->DragStartEntityPos, camera, vp)
  current_angle  = Atan2(mouse_pos - screen_center)
  start_angle    = Atan2(state->DragStartMousePos - screen_center)
  delta          = current_angle - start_angle  // radians
  state->RotateAccumulated = delta
  // Apply delta rotation around the world/local axis
  out_rot_euler = state->DragStartEntityRot + AxisMask(axis) * delta
```

### 8.3 Scale drag

```
UpdateDrag (scale):
  delta_px = mouse_pos.x - state->DragStartMousePos.x  // X-only for uniform feel
  state->DragAccumulated = delta_px
  factor = 1.f + delta_px * 0.005f   // 200 pixels = 2× scale
  if axis == Uniform: out_scale = state->DragStartEntityScale * factor
  else:               out_scale = state->DragStartEntityScale;
                      out_scale[AxisIndex(axis)] *= factor
```

---

## 9. Updated EditorSelectionSystem

`EditorSelectionSystemTick` is rewritten to use `GizmoState`, `EntityPickingPass`, and
`GizmoInteraction` instead of ImGuizmo. The `ImGuizmo::OPERATION` / `ImGuizmo::MODE`
types in `EditorSelectionContext` are replaced.

### 9.1 Updated EditorSelectionContext

```cpp
// Replaces ImGuizmo::OPERATION + ImGuizmo::MODE:
struct EditorSelectionContext
{
    EditorSelectionState* Selection      = nullptr;
    Physics::PhysicsWorld* Physics       = nullptr;  // still used for hover raycast
    ECS::WorldCommands*   Commands       = nullptr;
    Rendering::Cameras::Camera* Camera   = nullptr;
    SceneViewportBounds   ViewportBounds = {};

    // Gizmo state (replaces ImGuizmo fields)
    GizmoOperation        Operation      = GizmoOperation::Translate;
    GizmoSpace            Space          = GizmoSpace::World;
    GizmoState*           Gizmo          = nullptr;
    EntityPickingPass*    PickingPass    = nullptr;

    bool                  ViewportHasFocus = false;

    static EditorSelectionContext* Get();
    static void                    Set(EditorSelectionContext* ctx);
private:
    static EditorSelectionContext* s_Instance;
};
```

### 9.2 Updated system tick (key changes only)

```cpp
void EditorSelectionSystemTick(ECS::Scene& scene, float /*dt*/)
{
    EditorSelectionContext* ctx = EditorSelectionContext::Get();
    // ... null checks ...

    // ── 1. Consume last frame's picked ID ──────────────────────────────────
    const uint32_t picked_id = ctx->PickingPass->ConsumePickedID();

    // ── 2. Keyboard shortcuts ──────────────────────────────────────────────
    // (T/R/S change ctx->Operation; G toggles ctx->Space — same keys as before)
    // Escape, Delete, F, Ctrl+D, H — unchanged logic

    // ── 3. Hover state from picking ────────────────────────────────────────
    // No physics raycast for hover — use picking buffer instead.
    if (picked_id == k_EmptyPickID) {
        ctx->Selection->ClearHover();
    } else if (!IsGizmoHandle(picked_id)) {
        ctx->Selection->Hover(ECS::EntityID{ picked_id });
    } else {
        ctx->Selection->ClearHover();
        ctx->Gizmo->HoveredAxis = GizmoAxisOf(picked_id);
    }

    // ── 4. LMB press ───────────────────────────────────────────────────────
    const bool lmb_just_pressed =
        Windows::Inputs::InputFrame::Get().IsMouseJustPressed(ZENGINE_KEY_MOUSE_BUTTON_LEFT);

    if (lmb_just_pressed && ctx->ViewportHasFocus) {
        if (IsGizmoHandle(picked_id) && ctx->Selection->HasSelection) {
            // Start gizmo drag
            auto* xform = scene.GetComponent<
                ECS::Components::TransformComponent>(ctx->Selection->Selected);
            if (xform) {
                GizmoInteraction::BeginDrag(
                    ctx->Gizmo,
                    GizmoAxisOf(picked_id),
                    xform->GetPosition(),
                    xform->GetRotationEulerAngles() * k_DegToRad,
                    xform->GetScaleSize(),
                    Windows::Inputs::InputFrame::Get().MousePos(),
                    *ctx->Camera,
                    { ctx->ViewportBounds.Size.x, ctx->ViewportBounds.Size.y });
            }
        } else if (!IsGizmoHandle(picked_id)) {
            if (picked_id == k_EmptyPickID) {
                ctx->Selection->Deselect();
            } else {
                ctx->Selection->Select(ECS::EntityID{ picked_id });
            }
        }
    }

    // ── 5. Gizmo drag update ────────────────────────────────────────────────
    const bool lmb_held =
        Windows::Inputs::InputFrame::Get().IsMouseDown(ZENGINE_KEY_MOUSE_BUTTON_LEFT);

    if (ctx->Gizmo->IsDragging && lmb_held && ctx->Selection->HasSelection) {
        auto* xform = scene.GetComponent<
            ECS::Components::TransformComponent>(ctx->Selection->Selected);
        if (xform) {
            Core::Maths::Vec3f new_pos, new_rot, new_scale;
            GizmoInteraction::UpdateDrag(
                ctx->Gizmo,
                Windows::Inputs::InputFrame::Get().MousePos(),
                *ctx->Camera,
                { ctx->ViewportBounds.Size.x, ctx->ViewportBounds.Size.y },
                new_pos, new_rot, new_scale);

            // Use TransformComponent's accessor API (not direct field access)
            xform->SetPosition(new_pos);
            xform->SetRotationEulerAngles(new_rot * k_RadToDeg);  // SetRotationEulerAngles takes degrees
            xform->SetScaleSize(new_scale);
        }
    }

    // ── 6. LMB release — end drag ──────────────────────────────────────────
    const bool lmb_just_released =
        Windows::Inputs::InputFrame::Get().IsMouseJustReleased(ZENGINE_KEY_MOUSE_BUTTON_LEFT);

    if (lmb_just_released && ctx->Gizmo->IsDragging) {
        GizmoInteraction::EndDrag(ctx->Gizmo);
        // v2: push transform change onto undo stack via WorldCommands
    }

    // ── 7. Schedule picking readback for next frame ─────────────────────────
    const Core::Maths::Vec2f mouse_pos = Windows::Inputs::InputFrame::Get().MousePos();
    const Core::Maths::Vec2f local_pos = {
        mouse_pos.x - ctx->ViewportBounds.Pos.x,
        mouse_pos.y - ctx->ViewportBounds.Pos.y,
    };
    if (local_pos.x >= 0.f && local_pos.y >= 0.f &&
        local_pos.x <  ctx->ViewportBounds.Size.x &&
        local_pos.y <  ctx->ViewportBounds.Size.y)
    {
        ctx->PickingPass->ScheduleReadback(local_pos);
    }
}
```

**Note on `DecomposeTransformComponent`:** The function still exists as a free utility
but no longer wraps `ImGuizmo::DecomposeMatrixToComponents`. It uses the engine's own
matrix decomposition:

```cpp
void DecomposeTransform(
    const Core::Maths::Mat4f& model,
    Core::Maths::Vec3f&       out_position,
    Core::Maths::Vec3f&       out_rotation_euler_deg,
    Core::Maths::Vec3f&       out_scale)
{
    // Column 0, 1, 2 magnitudes = scale
    out_scale.x = Vec3f{ model[0][0], model[1][0], model[2][0] }.Length();
    out_scale.y = Vec3f{ model[0][1], model[1][1], model[2][1] }.Length();
    out_scale.z = Vec3f{ model[0][2], model[1][2], model[2][2] }.Length();

    // Remove scale to isolate rotation
    Core::Maths::Mat3f rot = {
        model[0][0] / out_scale.x, model[1][0] / out_scale.x, model[2][0] / out_scale.x,
        model[0][1] / out_scale.y, model[1][1] / out_scale.y, model[2][1] / out_scale.y,
        model[0][2] / out_scale.z, model[1][2] / out_scale.z, model[2][2] / out_scale.z,
    };
    out_rotation_euler_deg = Core::Maths::Mat3ToEulerDegrees(rot);

    // Translation is column 3
    out_position = { model[0][3], model[1][3], model[2][3] };
}
```

---

## 10. Pass Position in RenderGraph Frame

```
DepthPrePass           → hdr_depth
GeometryPass           → hdr_color, hdr_depth
...
PostProcessPass        → ldr_final
EntityPickingPass      → pick_buffer (R32_UINT), pick_depth (D32)   ← new, ZENGINE_EDITOR only
GizmoPass              → ldr_final (in-place, alpha blend off)       ← new, ZENGINE_EDITOR only
EditorOutlinePass      → ldr_final (in-place, stencil)               ← existing
UIPass                 → ldr_final (in-place)
OverlayPass            → swapchain
```

Both new passes are registered with `RenderGraph::AddCallbackPass` inside an
`#ifdef ZENGINE_EDITOR` guard in `AppRenderPipeline::Initialize`.

---

## 11. TransformComponent Accessor Gap Fix

The existing `editor-entity-selection.md` design assumes public fields:
```cpp
transform->Position  // DOES NOT EXIST in current TransformComponent
transform->Rotation  // DOES NOT EXIST
transform->Scale     // DOES NOT EXIST
```

The actual API uses accessors. All gizmo code in this document uses the correct API.
The existing design doc must be updated to match:

| Incorrect (design doc) | Correct (actual API) |
|---|---|
| `transform->Position` | `transform->GetPosition()` |
| `transform->Rotation` | `transform->GetRotation()` (radians) |
| `transform->Scale` | `transform->GetScaleSize()` |
| `transform->Position = v` | `transform->SetPosition(v)` |
| `transform->Rotation = v` | `transform->SetRotation(v)` (radians) or `SetRotationEulerAngles(v)` (degrees) |
| `transform->Scale = v` | `transform->SetScaleSize(v)` |

---

## 12. Removing ImGuizmo and ImGui

Once this pass is implemented and all Tetragrama panels are migrated to UIContext:

1. Remove `ImGuizmo` includes from `EditorSelectionSystem.h` and `EditorSelectionSystem.cpp`
2. Remove `#include <ImGuizmo.h>` everywhere
3. Delete `__externals/ImGuizmo` submodule
4. Remove `ImGuizmo` from `dependencies.cmake` and `CMakeLists.txt`
5. With no remaining ImGui users: remove `ImGUIRenderer` registration from RenderGraph
6. Delete `__externals/imgui` submodule
7. Remove imgui from CMake
8. Confirm build succeeds with `ZENGINE_EDITOR` defined and without it

---

## 13. File Layout

```
ZEngine/ZEngine/Editor/
├── Gizmo/
│   ├── GizmoTypes.h              — GizmoOperation, GizmoSpace, GizmoAxis, encoding helpers
│   ├── GizmoState.h              — GizmoState struct
│   ├── GizmoGeometry.h/.cpp      — pre-built handle mesh data + GPU upload
│   ├── GizmoInteraction.h/.cpp   — BeginDrag, UpdateDrag, EndDrag, ProjectAxisToScreen
│   ├── EntityPickingPass.h/.cpp  — IRenderGraphCallbackPass: R32_UINT pick buffer
│   └── GizmoPass.h/.cpp          — IRenderGraphCallbackPass: gizmo handle rendering
│
└── Selection/
    ├── EditorSelectionState.h    — unchanged
    ├── EditorSelectionSystem.h   — updated: ImGuizmo types replaced, new dependencies
    └── EditorSelectionSystem.cpp — updated: ImGuizmo calls replaced with GizmoInteraction

Resources/Shaders/Editor/
    ├── editor_pick.vert           — entity/handle ID output
    ├── editor_pick.frag
    ├── editor_gizmo.vert          — gizmo handle rendering
    └── editor_gizmo.frag
    (+ .spv cache files)
```

---

## 14. Deliverables Checklist

### GizmoTypes
- [ ] `GizmoOperation`, `GizmoSpace`, `GizmoAxis` enums defined
- [ ] `k_GizmoIDFlag`, `MakeGizmoID`, `IsGizmoHandle`, `GizmoAxisOf`, `GizmoOpOf` implemented
- [ ] `GizmoState` struct; all fields zero/default initialized

### GizmoGeometry
- [ ] Arrow (shaft + cone), PlaneSquare, Ring, ScaleBox, CenterSphere meshes built procedurally
- [ ] All geometry uploaded to a single shared `VkBuffer` (vertex + index interleaved)
- [ ] `GizmoMeshRange` values correct (vertex/index offsets and counts)

### EntityPickingPass
- [ ] `R32_UINT` attachment created at viewport resolution
- [ ] `D32_SFLOAT` depth attachment created
- [ ] All entities with `MeshComponent + TransformComponent` rendered with their `EntityID::Index` as the pick color
- [ ] Gizmo handles rendered with `MakeGizmoID(op, axis)` as pick color; depth test ALWAYS
- [ ] 1×1 host-visible readback buffer; `ScheduleReadback` copies pixel at cursor position
- [ ] `ConsumePickedID` returns correct entity or gizmo ID one frame after scheduling
- [ ] Returns `k_EmptyPickID` on first frame (no readback yet)
- [ ] Pass registered only under `#ifdef ZENGINE_EDITOR`

### GizmoPass
- [ ] Translate handles: 3 arrows (X/Y/Z) + 3 plane squares rendered correctly
- [ ] Rotate handles: 3 rings rendered in the correct orientation
- [ ] Scale handles: 3 shafts + 3 end-boxes rendered
- [ ] Hovered handle is yellow; dragging handle is bright yellow
- [ ] Screen scale factor keeps handles constant size regardless of zoom
- [ ] Depth test ALWAYS — handles render on top of scene
- [ ] Pass registered only under `#ifdef ZENGINE_EDITOR`

### GizmoInteraction
- [ ] `BeginDrag` captures entity start state; projects axis to screen space
- [ ] `UpdateDrag` (translate): mouse delta projected onto screen-space axis direction → world displacement correct
- [ ] `UpdateDrag` (rotate): angle delta around correct axis
- [ ] `UpdateDrag` (scale): uniform and per-axis scale correct; never goes negative
- [ ] `EndDrag` clears `IsDragging`, `ActiveAxis`
- [ ] Plane handles (XY/YZ/XZ) project mouse onto plane, not axis

### EditorSelectionSystem
- [ ] `ImGuizmo::OPERATION` / `ImGuizmo::MODE` removed from `EditorSelectionContext`; replaced by `GizmoOperation` / `GizmoSpace`
- [ ] All `ImGuizmo::` call sites removed
- [ ] `TransformComponent` accessed via `GetPosition()` / `SetPosition()` etc. (not direct fields)
- [ ] Hover state driven by picking buffer, not physics raycast
- [ ] Entity selection driven by picking buffer (bit 31 = 0 → entity ID)
- [ ] Gizmo drag begins on picking result with bit 31 = 1
- [ ] `ScheduleReadback` called every frame while cursor is inside viewport
- [ ] `DecomposeTransform` no longer uses `ImGuizmo::DecomposeMatrixToComponents`

### Shader
- [ ] `editor_pick.vert/frag` — `R32_UINT` output via `out uint`
- [ ] `editor_gizmo.vert/frag` — push constant model + color; depth test ALWAYS
- [ ] All `.spv` files compiled and cached in `Resources/Shaders/Cache/`
- [ ] Shaders excluded from non-editor cook

### ImGuizmo removal
- [ ] All `#include <ImGuizmo.h>` removed
- [ ] `__externals/ImGuizmo` submodule removed
- [ ] Build succeeds with `ZENGINE_EDITOR` defined and without it after removal

### ImGui removal (after all UIContext panel migrations complete)
- [ ] `ImGUIRenderer` removed from RenderGraph registration
- [ ] All `#include <imgui.h>` removed from engine code
- [ ] `__externals/imgui` submodule removed
- [ ] Build succeeds

### Tests
- [ ] `MakeGizmoID` / `IsGizmoHandle` / `GizmoAxisOf` round-trip unit test
- [ ] `DecomposeTransform`: TRS(pos, rot, scale) → decompose → values match input
- [ ] `ProjectAxisToScreen`: world X-axis projected at camera looking along -Z → screen-space horizontal
- [ ] `ConsumePickedID` returns `k_EmptyPickID` when no readback scheduled
- [ ] Manual: click entity in viewport → selected, gizmo appears
- [ ] Manual: drag translate arrow → entity moves along correct axis only
- [ ] Manual: drag rotate ring → entity rotates around correct axis
- [ ] Manual: drag scale box → scale changes on correct axis only
- [ ] Manual: click empty space → selection clears, gizmo disappears
- [ ] Manual: build with ImGuizmo removed → no compile errors

---

## 15. Verification

- [ ] One-frame picking lag not perceptible at 60 fps (cursor velocity < 16px/frame at normal mouse speed)
- [ ] `EntityPickingPass` adds < 0.3 ms GPU time (single draw call per entity, simple shader)
- [ ] `GizmoPass` adds < 0.1 ms GPU time (< 500 triangles for full gizmo set)
- [ ] No physics world dependency for entity hover/selection — `Physics::PhysicsWorld*` may be null and picking still works
- [ ] `ImGuizmo` submodule absent from `__externals/` after final removal commit
- [ ] `ImGui` submodule absent from `__externals/` after final migration commit
