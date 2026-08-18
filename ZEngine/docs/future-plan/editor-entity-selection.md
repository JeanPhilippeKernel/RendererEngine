# ZEngine — Editor Entity Selection

**Priority:** P1 — Required for Sprint 13; inspector cannot work without it
**Status:** Design
**Depends on:** `actor-ecs-architecture.md`, `component-reflection.md`, `physics-system.md`, `render-graph-integration.md`
**Blocks:** Tetragrama inspector, hierarchy view interaction, gizmo transforms

---

> **NOTE: NameComponent and MeshComponent do not yet exist — they are tracked in issue #609.
> EditorSelectionSystemTick cannot be implemented until those components land.**

---

## 1. Overview

Entity selection is the mechanism by which the editor user picks which entity to inspect and
manipulate. It is the entry point for virtually all edit operations: inspecting components,
applying transforms, renaming, duplicating, and deleting entities.

There are two ways to select an entity:

1. **Hierarchy click** — click an entity name in the `HierarchyViewUIComponent` tree. This
   is unambiguous: the entity is named, visible in the list, and the click target is
   well-defined. No raycasting is required.

2. **Viewport click** — click in the 3D `SceneViewportUIComponent` using a physics raycast.
   The mouse position is converted to a world-space ray via the camera's inverse projection
   and inverse view matrices. That ray is submitted to `PhysicsWorld::Raycast`. The first
   entity hit becomes the selection.

The selected entity is stored in `EditorSelectionState` — a plain data singleton owned by
`EditorPlayModeSystem` and shared (as a non-owning pointer) across all editor UI components.

### Design constraints

- No heap allocation for selection state. `EditorSelectionState` is plain data, 16 bytes.
- No new/delete anywhere in the selection path.
- No exceptions. Error conditions use `ZENGINE_VALIDATE_ASSERT`.
- No virtual dispatch in the per-frame path. `EditorSelectionSystem` is a plain ECS system
  function registered via `WorldTick::RegisterSystem`.
- `EditorSelectionState` is NOT an ECS component. It is not stored in `ComponentStorage<T>`.
  It is editor-side bookkeeping, not game-visible data.
- All selection changes that affect the scene (destroy, duplicate) go through
  `ECS::WorldCommands` deferred queue, never through `Scene::DestroyEntity` directly from
  within the system tick function.
- The entire selection system is compiled only when `ZENGINE_EDITOR` is defined.

---

## 2. EditorSelectionState

`EditorSelectionState` is a plain data struct. It carries the currently selected entity and
the entity the mouse is hovering over. All editor UI components receive a non-owning pointer
to the same instance; the instance is owned by `EditorPlayModeSystem`.

```cpp
// ZEngine/Editor/Selection/EditorSelectionState.h
#pragma once

#ifdef ZENGINE_EDITOR

#include <ECS/EntityID.h>

namespace ZEngine::Editor {

    struct EditorSelectionState {
        ECS::EntityID Selected   = ECS::INVALID_ENTITY;
        ECS::EntityID Hovered    = ECS::INVALID_ENTITY;
        bool          HasSelection = false;

        // -----------------------------------------------------------------------
        // Mutation — called by EditorSelectionSystem and HierarchyViewUIComponent
        // -----------------------------------------------------------------------

        void Select(ECS::EntityID id) {
            ZENGINE_VALIDATE_ASSERT(id.IsValid(),
                "EditorSelectionState::Select — called with INVALID_ENTITY");
            Selected     = id;
            HasSelection = true;
        }

        void Deselect() {
            Selected     = ECS::INVALID_ENTITY;
            HasSelection = false;
        }

        void Hover(ECS::EntityID id) {
            Hovered = id;   // id may be INVALID_ENTITY (mouse over empty space)
        }

        void ClearHover() {
            Hovered = ECS::INVALID_ENTITY;
        }

        // -----------------------------------------------------------------------
        // Query — called by any component that reads selection state
        // -----------------------------------------------------------------------

        [[nodiscard]] bool IsSelected(ECS::EntityID id) const {
            return HasSelection && Selected == id;
        }

        [[nodiscard]] bool IsHovered(ECS::EntityID id) const {
            return Hovered.IsValid() && Hovered == id;
        }
    };

}  // namespace ZEngine::Editor

#endif  // ZENGINE_EDITOR
```

### Ownership and lifetime

`EditorSelectionState` is a member of `EditorPlayModeSystem`. Its address is passed to
every editor UI component during initialization via their respective `Initialize(...)` calls.
Components must store a raw (non-owning) pointer: `EditorSelectionState* m_selection`.
The `EditorPlayModeSystem` outlives all UI components; no dangling pointer is possible
within normal engine shutdown order.

### Thread safety

`EditorSelectionState` is accessed exclusively from the main thread. The ECS system tick,
the ImGui UI callbacks, and the outline pass CPU side all run on the main thread.
No synchronization primitives are needed.

---

## 3. Hierarchy click selection

`HierarchyViewUIComponent` renders the entity tree via ImGui. Each node is an `ImGui::Selectable`.
Selection and hover highlight are driven by `EditorSelectionState`.

```cpp
// Conceptual rendering loop inside HierarchyViewUIComponent::Render()
// (pseudocode — actual traversal is a depth-first walk of the scene hierarchy)

void HierarchyViewUIComponent::RenderNode(
    const HierarchyNode& node,
    ECS::Scene& scene,
    ECS::WorldCommands& cmds,
    EditorSelectionState* selection)
{
    const bool is_selected = selection->IsSelected(node.EntityID);

    // Highlight hovered row before the Selectable call
    if (selection->IsHovered(node.EntityID))
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanAvailWidth |
        (node.Children.IsEmpty() ? ImGuiTreeNodeFlags_Leaf : 0) |
        (is_selected             ? ImGuiTreeNodeFlags_Selected : 0);

    bool node_open = ImGui::TreeNodeEx(
        reinterpret_cast<void*>(static_cast<uintptr_t>(node.EntityID.Index)),
        flags,
        "%s", node.Name);

    if (selection->IsHovered(node.EntityID))
        ImGui::PopStyleColor();

    // Single-click selects
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        selection->Select(node.EntityID);

    // Mouse hover updates hover state
    if (ImGui::IsItemHovered())
        selection->Hover(node.EntityID);

    // Right-click context menu
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Rename"))
            m_rename_target = node.EntityID;  // opens rename inline field next frame

        if (ImGui::MenuItem("Duplicate"))
            cmds.DeferDuplicateEntity(node.EntityID);

        if (ImGui::MenuItem("Add Child")) {
            cmds.DeferCreateChildEntity(node.EntityID, [](ECS::EntityID new_id) {
                // new entity starts with a TransformComponent
                (void)new_id;  // caller adds components via WorldCommands callback
            });
        }

        if (ImGui::MenuItem("Delete")) {
            cmds.DeferDestroyEntity(node.EntityID);
            // If this was the selected entity, deselect immediately so the
            // inspector does not try to render a dead entity's components.
            if (selection->IsSelected(node.EntityID))
                selection->Deselect();
        }

        ImGui::EndPopup();
    }

    if (node_open) {
        for (const auto& child : node.Children)
            RenderNode(child, scene, cmds, selection);
        ImGui::TreePop();
    }
}
```

### Multi-selection note

Multi-selection is deferred to v2. `EditorSelectionState` v1 stores a single `Selected`
entity. The v2 design will replace `Selected` with a small inline array (capacity 64,
arena-allocated) and change `IsSelected` to a bitmask lookup. No v1 code should assume
single-selection is permanent; use `IsSelected(id)` rather than `Selected == id` at call
sites outside `EditorSelectionState` itself.

### Hierarchy tree data

The `HierarchyNode` tree is rebuilt once per frame (or on scene-modification events) from
an `ECS::Scene` walk. It is a scratch-arena-allocated tree; no persistent heap allocation.
The walk produces a flat list of root nodes; each node carries a `EntityID`, a display name
(pulled from `NameComponent` if present, otherwise `"Entity#<index>"`), and a `children`
array of `HierarchyNode*`.

---

## 4. Viewport click selection — physics raycast

Clicking in the 3D viewport fires a ray from the camera into the scene and queries
`PhysicsWorld::Raycast` for the first entity hit. Every mesh entity that should be selectable
must have a `ColliderComponent` attached (minimum: `ColliderShape::Box` with half-extents
derived from the mesh's AABB). Entities without a collider are invisible to the raycast and
cannot be selected via the viewport.

### 4.1 Coordinate transform: screen to world-space ray

```cpp
// ZEngine/Editor/Selection/EditorSelectionSystem.cpp

static Core::Maths::Vec3f ScreenToWorldRay(
    const Core::Maths::Vec2f& mouse_pos,
    const Core::Maths::Vec2f& viewport_pos,
    const Core::Maths::Vec2f& viewport_size,
    const Rendering::Camera&  camera)
{
    // Guard against degenerate viewport (e.g., minimized window)
    ZENGINE_VALIDATE_ASSERT(viewport_size.x > 0.f && viewport_size.y > 0.f,
        "ScreenToWorldRay: viewport_size is zero — viewport not yet sized");

    // Step 1: Mouse position in viewport-local space [0, viewport_size]
    const float local_x = mouse_pos.x - viewport_pos.x;
    const float local_y = mouse_pos.y - viewport_pos.y;

    // Step 2: Normalize to NDC [-1, 1].
    // NDC.x: -1 = left edge, +1 = right edge
    // NDC.y: -1 = bottom edge (OpenGL convention used by Vulkan clip space after
    //         Y-flip), +1 = top edge.  The 1 - (2*y/h) expression inverts screen Y.
    const Core::Maths::Vec2f ndc = {
        (local_x / viewport_size.x) * 2.f - 1.f,
        1.f - (local_y / viewport_size.y) * 2.f
    };

    // Step 3: Unproject from clip space to view space.
    // ray_clip.z = -1: the near plane in Vulkan NDC.
    // ray_clip.w =  1: homogeneous, direction (not a point).
    const Core::Maths::Vec4f ray_clip  = { ndc.x, ndc.y, -1.f, 1.f };
    Core::Maths::Vec4f       ray_eye   = camera.InverseProjection() * ray_clip;
    ray_eye.z = -1.f;  // force "into the screen"
    ray_eye.w =  0.f;  // direction, not position

    // Step 4: Unproject from view space to world space.
    const Core::Maths::Vec3f ray_world =
        (camera.InverseView() * ray_eye).xyz().Normalized();

    return ray_world;
}
```

### 4.2 Full viewport click handler

```cpp
void EditorSelectionSystem::HandleViewportClick(
    const Core::Maths::Vec2f&  mouse_pos,
    const Core::Maths::Vec2f&  viewport_pos,
    const Core::Maths::Vec2f&  viewport_size,
    const Rendering::Camera&   camera,
    Physics::PhysicsWorld*     physics,
    EditorSelectionState*      selection)
{
    ZENGINE_VALIDATE_ASSERT(physics != nullptr,
        "EditorSelectionSystem::HandleViewportClick — PhysicsWorld is null");
    ZENGINE_VALIDATE_ASSERT(selection != nullptr,
        "EditorSelectionSystem::HandleViewportClick — EditorSelectionState is null");

    const Core::Maths::Vec3f ray_dir    = ScreenToWorldRay(
        mouse_pos, viewport_pos, viewport_size, camera);
    const Core::Maths::Vec3f ray_origin = camera.Position();

    // Query all object layers. The editor selects any entity that has a collider,
    // regardless of physics layer. Use ObjectLayers::AllMask for broad coverage.
    // Max distance 1000 m is an editor-only constant; adjust per scene scale.
    constexpr float kMaxPickDistance = 1000.f;

    const Physics::RaycastResult result = physics->Raycast(
        ray_origin, ray_dir, kMaxPickDistance,
        Physics::ObjectLayers::AllMask);

    if (result.Hit) {
        selection->Select(result.HitEntity);
    } else {
        // Clicking empty space deselects. This is intentional; it mirrors the
        // standard behavior in Unity, Unreal, and Godot editors.
        selection->Deselect();
    }
}
```

### 4.3 Collider requirement

The physics system uses `ColliderComponent` for all raycasting. Every mesh entity that
the editor user expects to click in the viewport must have a `ColliderComponent`. A
`ColliderShape::Box` covering the mesh's AABB is the minimum acceptable collider. An
editor-only "auto-fit AABB collider" pass runs during scene load in Edit mode; it
iterates all entities with a `MeshComponent` but no `ColliderComponent` and adds a
`ColliderComponent{ .Shape = ColliderShape::Box, .HalfExtents = mesh.AABBHalfExtents() }`
via `WorldCommands`. This collider is never serialized to the scene file; it is
reconstructed on every edit-mode load.

---

## 5. EditorSelectionSystem — ECS system

`EditorSelectionSystem` is an ECS system function registered only when the engine is in
`Edit` or `Pause` mode. It is not registered in `Play` mode. It is responsible for:

- Reading input each tick and deciding whether a selection action occurred.
- Calling `HandleViewportClick` when LMB is pressed inside the viewport.
- Handling keyboard shortcuts: Escape (deselect), Delete (destroy), F (frame camera),
  T/R/S/G (gizmo mode), Ctrl+D (duplicate), H (toggle visibility).
- Running `ImGuizmo` manipulation for the currently selected entity's transform.

### 5.1 System dependencies

```cpp
ECS::SystemDeps kEditorSelectionDeps = {
    // Reads TransformComponent and MeshComponent to position the gizmo and
    // outline pass. Does not write any component; all mutations go through
    // WorldCommands (deferred) or EditorSelectionState (editor-only state).
    .ReadMask  = MaskBit(ComponentTypeOf<ECS::Components::TransformComponent>())
               | MaskBit(ComponentTypeOf<ECS::Components::MeshComponent>()),
    .WriteMask = 0,
};
```

`EditorSelectionSystem` is read-only with respect to the ECS. All transform changes
applied by ImGuizmo manipulation are written directly to `TransformComponent` via
`scene.GetComponent<TransformComponent>(id)` after ImGuizmo finishes the drag, because
gizmo manipulation happens synchronously on the main thread and the system is the only
writer at that moment. For undo support (v2), each gizmo-committed transform change will
be wrapped in a `WorldCommands` deferred write that the undo stack can replay or revert.

### 5.2 System tick implementation

```cpp
// ZEngine/Editor/Selection/EditorSelectionSystem.cpp

void EditorSelectionSystemTick(ECS::Scene& scene, float /*dt*/)
{
    EditorSelectionContext* ctx = EditorSelectionContext::Get();
    ZENGINE_VALIDATE_ASSERT(ctx != nullptr,
        "EditorSelectionSystemTick: context not initialized — was EditorPlayModeSystem set up?");

    EditorSelectionState*     selection = ctx->Selection;
    Physics::PhysicsWorld*    physics   = ctx->Physics;
    ECS::WorldCommands*       cmds      = ctx->Commands;
    Rendering::Camera*        camera    = ctx->Camera;
    SceneViewportBounds       viewport  = ctx->ViewportBounds;

    // -----------------------------------------------------------------------
    // 1. Keyboard shortcuts — process before mouse to avoid simultaneous fires
    // -----------------------------------------------------------------------

    // Escape: deselect
    if (Inputs::InputManager::Get().GetButton(Inputs::KeyboardSlot::Escape) ==
        Inputs::ButtonState::JustPressed)
    {
        selection->Deselect();
    }

    // Delete: destroy selected entity (deferred)
    if (selection->HasSelection &&
        Inputs::InputManager::Get().GetButton(Inputs::KeyboardSlot::Delete) ==
        Inputs::ButtonState::JustPressed)
    {
        cmds->DeferDestroyEntity(selection->Selected);
        selection->Deselect();
    }

    // Ctrl+D: duplicate
    if (selection->HasSelection &&
        Inputs::InputManager::Get().GetButton(Inputs::KeyboardSlot::D) ==
        Inputs::ButtonState::JustPressed &&
        Inputs::InputManager::Get().GetButton(Inputs::KeyboardSlot::LeftCtrl) ==
        Inputs::ButtonState::Held)
    {
        DuplicateSelected(scene, *cmds, selection);
    }

    // H: toggle visibility of selected entity
    if (selection->HasSelection &&
        Inputs::InputManager::Get().GetButton(Inputs::KeyboardSlot::H) ==
        Inputs::ButtonState::JustPressed)
    {
        ToggleVisibility(scene, selection->Selected);
    }

    // F: frame camera on selected entity
    if (selection->HasSelection &&
        Inputs::InputManager::Get().GetButton(Inputs::KeyboardSlot::F) ==
        Inputs::ButtonState::JustPressed)
    {
        FrameCameraOnSelected(scene, selection->Selected, camera);
    }

    // Gizmo mode keys
    if (Inputs::InputManager::Get().GetButton(Inputs::KeyboardSlot::T) ==
        Inputs::ButtonState::JustPressed)
        ctx->GizmoOperation = ImGuizmo::TRANSLATE;

    if (Inputs::InputManager::Get().GetButton(Inputs::KeyboardSlot::R) ==
        Inputs::ButtonState::JustPressed)
        ctx->GizmoOperation = ImGuizmo::ROTATE;

    if (Inputs::InputManager::Get().GetButton(Inputs::KeyboardSlot::S) ==
        Inputs::ButtonState::JustPressed)
        ctx->GizmoOperation = ImGuizmo::SCALE;

    // G: toggle global / local gizmo space
    if (Inputs::InputManager::Get().GetButton(Inputs::KeyboardSlot::G) ==
        Inputs::ButtonState::JustPressed)
    {
        ctx->GizmoMode = (ctx->GizmoMode == ImGuizmo::WORLD)
            ? ImGuizmo::LOCAL
            : ImGuizmo::WORLD;
    }

    // -----------------------------------------------------------------------
    // 2. Mouse position and viewport bounds check
    // -----------------------------------------------------------------------

    const Core::Maths::Vec2f mouse_pos = Inputs::InputManager::Get().GetMousePosition();

    const bool mouse_in_viewport =
        mouse_pos.x >= viewport.Pos.x &&
        mouse_pos.y >= viewport.Pos.y &&
        mouse_pos.x <  viewport.Pos.x + viewport.Size.x &&
        mouse_pos.y <  viewport.Pos.y + viewport.Size.y;

    // Do not fire selection/hover when ImGuizmo is consuming mouse input
    const bool gizmo_active = ImGuizmo::IsUsing() || ImGuizmo::IsOver();

    // -----------------------------------------------------------------------
    // 3. Hover raycast (every frame, cheap enough for edit mode)
    // -----------------------------------------------------------------------

    if (mouse_in_viewport && !gizmo_active) {
        const Core::Maths::Vec3f ray_dir = ScreenToWorldRay(
            mouse_pos, viewport.Pos, viewport.Size, *camera);
        const Core::Maths::Vec3f ray_origin = camera->Position();

        constexpr float kHoverDistance = 1000.f;
        const Physics::RaycastResult hover = physics->Raycast(
            ray_origin, ray_dir, kHoverDistance, Physics::ObjectLayers::AllMask);

        selection->Hover(hover.Hit ? hover.HitEntity : ECS::INVALID_ENTITY);
    } else {
        selection->ClearHover();
    }

    // -----------------------------------------------------------------------
    // 4. LMB click — fire selection raycast
    // -----------------------------------------------------------------------

    const bool lmb_just_pressed =
        Inputs::InputManager::Get().GetButton(Inputs::MouseSlot::Left) ==
        Inputs::ButtonState::JustPressed;

    if (lmb_just_pressed && mouse_in_viewport && !gizmo_active) {
        HandleViewportClick(
            mouse_pos, viewport.Pos, viewport.Size, *camera, physics, selection);
    }

    // -----------------------------------------------------------------------
    // 5. ImGuizmo gizmo manipulation for selected entity
    // -----------------------------------------------------------------------

    if (selection->HasSelection) {
        auto* transform = scene.GetComponent<ECS::Components::TransformComponent>(
            selection->Selected);

        if (transform) {
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(
                viewport.Pos.x, viewport.Pos.y,
                viewport.Size.x, viewport.Size.y);

            const Core::Maths::Mat4f view       = camera->ViewMatrix();
            const Core::Maths::Mat4f projection = camera->ProjectionMatrix();

            Core::Maths::Mat4f model = Core::Maths::TRS(
                transform->Position,
                Core::Maths::FromEulerAngles(transform->Rotation),
                transform->Scale);

            if (ImGuizmo::Manipulate(
                    view.m_data[0],
                    projection.m_data[0],
                    ctx->GizmoOperation,
                    ctx->GizmoMode,
                    model.m_data[0]))
            {
                // Decompose modified matrix back into Position/Rotation/Scale.
                // This write is safe because EditorSelectionSystem runs with
                // WriteMask = 0 — no other system in the same wave touches
                // TransformComponent. The write takes effect this frame.
                DecomposeTransformComponent(
                    model,
                    transform->Position,
                    transform->Rotation,
                    transform->Scale);
                // v2: wrap in cmds->DeferWriteComponent for undo stack support.
            }
        }
    }
}
```

### 5.3 EditorSelectionContext

`EditorSelectionContext` is a plain struct holding all external dependencies that the system
function needs but cannot receive via ECS component query. It is a singleton accessed via a
static getter set during `EditorPlayModeSystem::Initialize`.

```cpp
// ZEngine/Editor/Selection/EditorSelectionSystem.h
#pragma once
#ifdef ZENGINE_EDITOR

#include <Editor/Selection/EditorSelectionState.h>
#include <Physics/PhysicsWorld.h>
#include <ECS/WorldCommands.h>
#include <Rendering/Camera.h>
#include <Core/Maths/Vector.h>
#include <imgui.h>
#include <ImGuizmo.h>

namespace ZEngine::Editor {

    struct SceneViewportBounds {
        Core::Maths::Vec2f Pos;   // top-left in screen pixels
        Core::Maths::Vec2f Size;  // width x height in screen pixels
    };

    struct EditorSelectionContext {
        EditorSelectionState*     Selection       = nullptr;
        Physics::PhysicsWorld*    Physics         = nullptr;
        ECS::WorldCommands*       Commands        = nullptr;
        Rendering::Camera*        Camera          = nullptr;
        SceneViewportBounds       ViewportBounds  = {};
        ImGuizmo::OPERATION       GizmoOperation  = ImGuizmo::TRANSLATE;
        ImGuizmo::MODE            GizmoMode       = ImGuizmo::WORLD;

        static EditorSelectionContext* Get();
        static void                    Set(EditorSelectionContext* ctx);

    private:
        static EditorSelectionContext* s_Instance;
    };

    // The ECS system function — registered with WorldTick in Edit and Pause modes.
    void EditorSelectionSystemTick(ECS::Scene& scene, float dt);

    // Standalone helpers called by the system tick:
    void DuplicateSelected(ECS::Scene& scene, ECS::WorldCommands& cmds,
                           EditorSelectionState* selection);
    void ToggleVisibility(ECS::Scene& scene, ECS::EntityID id);
    void FrameCameraOnSelected(ECS::Scene& scene, ECS::EntityID id,
                               Rendering::Camera* camera);
    Core::Maths::Vec3f ScreenToWorldRay(
        const Core::Maths::Vec2f& mouse_pos,
        const Core::Maths::Vec2f& viewport_pos,
        const Core::Maths::Vec2f& viewport_size,
        const Rendering::Camera&  camera);
    void DecomposeTransformComponent(
        const Core::Maths::Mat4f& model,
        Core::Maths::Vec3f&       out_position,
        Core::Maths::Vec3f&       out_rotation_euler,
        Core::Maths::Vec3f&       out_scale);
    void HandleViewportClick(
        const Core::Maths::Vec2f& mouse_pos,
        const Core::Maths::Vec2f& viewport_pos,
        const Core::Maths::Vec2f& viewport_size,
        const Rendering::Camera&  camera,
        Physics::PhysicsWorld*    physics,
        EditorSelectionState*     selection);

}  // namespace ZEngine::Editor

#endif  // ZENGINE_EDITOR
```

---

## 6. Outline rendering for selected entity

The selected entity is visually highlighted with an outline (silhouette) drawn in flat
orange. The hovered entity (not selected) is highlighted with a dimmer grey/white tint.
This is implemented as an editor-only RenderGraph callback pass: `"EditorOutlinePass"`.

The pass runs after `"UIPass"` (which produces `ldr_final`) and before `"OverlayPass"`.
It composites the outline into `ldr_final` in-place, using the depth buffer to avoid
drawing the outline behind occluding geometry, and a stencil buffer to avoid overdrawing
the selected mesh body with the outline color.

### 6.1 Pass position in the frame pass order

```
...
"UIPass"              → ldr_final
"EditorOutlinePass"   → ldr_final (in-place, ZENGINE_EDITOR only)
"TextPass"            → ldr_final (in-place)
"OverlayPass"         → swapchain image
```

### 6.2 Setup

```cpp
// ZEngine/Editor/Rendering/EditorOutlinePass.cpp
#ifdef ZENGINE_EDITOR

void EditorOutlinePass::Setup(RenderGraphResourceBuilder* builder)
{
    // Declare this node in the graph — mandatory for edge building.
    builder->CreateRenderPassNode(RenderGraphRenderPassCreation{
        .Name    = "EditorOutlinePass",
        .Inputs  = { "ldr_final", "hdr_depth" },
        .Outputs = { "ldr_final" },
    });

    // ldr_final is produced by UIPass and consumed in-place here.
    // hdr_depth is produced by DepthPrePass — used for depth testing so the
    // outline is not drawn in front of geometry that is in front of the mesh.
    // Both are external resources; no new allocations needed.
}
```

### 6.3 Compile

```cpp
void EditorOutlinePass::Compile(
    RenderGraphResourceBuilder*   builder,
    RenderGraphResourceInspector* inspector,
    RenderPass**                  output_pass)
{
    // Retrieve handles allocated by producing passes
    m_color_target = inspector->GetRenderTarget("ldr_final");
    m_depth_input  = inspector->GetRenderTarget("hdr_depth");

    // Pipeline: vertex shader scales the mesh by 1.02 in object space;
    // fragment shader outputs flat orange with full alpha.
    // Depth test: LESS_OR_EQUAL — do not overdraw occluders.
    // Depth write: disabled — this is a post-scene overlay pass.
    // Stencil:
    //   - First subpass: render the actual mesh with stencil write = 1.
    //   - Second subpass: render the scaled mesh; stencil test = NOT_EQUAL(1).
    //     This produces only the outline ring, not the filled interior.
    // Blend: opaque for the outline ring.
    //
    // The stencil buffer is a scratch resource created here (not shared across passes).
    m_outline_stencil_target = AllocateScratchStencilBuffer(Device->Arena, m_color_target);

    // Build render pass and pipeline — allocate from Device->Arena, not new/delete.
    m_render_pass = ZPushStructCtor(Device->Arena, VulkanRenderPass);
    m_render_pass->Initialize(Device,
        m_color_target, m_depth_input, m_outline_stencil_target,
        kOutlinePipelineSpec);

    *output_pass = m_render_pass->GetHandle();

    // Grey tint pipeline for hover highlight (no stencil trick needed —
    // just render the mesh with a semi-transparent grey overlay at scale 1.0)
    m_hover_pipeline = ZPushStructCtor(Device->Arena, VulkanPipeline);
    m_hover_pipeline->Initialize(Device, kHoverPipelineSpec);
}
```

### 6.4 Execute

```cpp
void EditorOutlinePass::Execute(
    RenderGraphResourceInspector* inspector,
    VkCommandBuffer               cmd)
{
    const EditorSelectionState* selection =
        EditorSelectionContext::Get() ? EditorSelectionContext::Get()->Selection : nullptr;

    // Early out — no editor context or nothing selected/hovered
    if (!selection) return;
    if (!selection->HasSelection && !selection->Hovered.IsValid()) return;

    ECS::Scene* scene = EditorPlayModeSystem::GetScene();
    ZENGINE_VALIDATE_ASSERT(scene != nullptr, "EditorOutlinePass: scene is null");

    vkCmdBeginRenderPass(cmd, &m_render_pass->GetBeginInfo(), VK_SUBPASS_CONTENTS_INLINE);

    // -----------------------------------------------------------------
    // Hover highlight (rendered first, dimmer, no stencil trick)
    // -----------------------------------------------------------------
    if (selection->Hovered.IsValid() &&
        selection->Hovered != selection->Selected)
    {
        auto* hover_transform = scene->GetComponent<
            ECS::Components::TransformComponent>(selection->Hovered);
        auto* hover_mesh = scene->GetComponent<
            ECS::Components::MeshComponent>(selection->Hovered);

        if (hover_transform && hover_mesh) {
            // Bind hover pipeline (grey tint, no stencil, scale 1.0)
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_hover_pipeline->GetHandle());

            // Push constants: model matrix at scale 1.0, tint color (0.7, 0.7, 0.7, 0.25)
            const OutlinePushConstants hover_pc = {
                .ModelMatrix = Core::Maths::TRS(
                    hover_transform->Position,
                    Core::Maths::FromEulerAngles(hover_transform->Rotation),
                    hover_transform->Scale),
                .TintColor   = { 0.7f, 0.7f, 0.7f, 0.25f },
            };
            vkCmdPushConstants(cmd, m_pipeline_layout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(OutlinePushConstants), &hover_pc);

            DrawMesh(cmd, hover_mesh);
        }
    }

    // -----------------------------------------------------------------
    // Selection outline (two-subpass stencil approach)
    // -----------------------------------------------------------------
    if (selection->HasSelection) {
        auto* sel_transform = scene->GetComponent<
            ECS::Components::TransformComponent>(selection->Selected);
        auto* sel_mesh = scene->GetComponent<
            ECS::Components::MeshComponent>(selection->Selected);

        if (sel_transform && sel_mesh) {
            // Subpass 0: render mesh at normal scale, write stencil = 1
            vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_stencil_fill_pipeline->GetHandle());

            const Core::Maths::Mat4f normal_model = Core::Maths::TRS(
                sel_transform->Position,
                Core::Maths::FromEulerAngles(sel_transform->Rotation),
                sel_transform->Scale);

            const OutlinePushConstants stencil_pc = {
                .ModelMatrix = normal_model,
                .TintColor   = { 0.f, 0.f, 0.f, 0.f },  // color output masked off
            };
            vkCmdPushConstants(cmd, m_pipeline_layout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(OutlinePushConstants), &stencil_pc);
            DrawMesh(cmd, sel_mesh);

            // Subpass 1: render mesh at scale 1.02, stencil test != 1 → outline ring only
            vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_outline_ring_pipeline->GetHandle());

            const Core::Maths::Mat4f scaled_model = Core::Maths::TRS(
                sel_transform->Position,
                Core::Maths::FromEulerAngles(sel_transform->Rotation),
                sel_transform->Scale * 1.02f);

            const OutlinePushConstants outline_pc = {
                .ModelMatrix = scaled_model,
                .TintColor   = { 1.0f, 0.5f, 0.0f, 1.0f },  // orange
            };
            vkCmdPushConstants(cmd, m_pipeline_layout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(OutlinePushConstants), &outline_pc);
            DrawMesh(cmd, sel_mesh);
        }
    }

    vkCmdEndRenderPass(cmd);
}
```

### 6.5 Push constants layout

```cpp
struct OutlinePushConstants {
    Core::Maths::Mat4f ModelMatrix;  // 64 bytes
    Core::Maths::Vec4f TintColor;    // 16 bytes
    // Total: 80 bytes — within the Vulkan minimum guaranteed push constant size of 128 bytes
};
static_assert(sizeof(OutlinePushConstants) <= 128,
    "OutlinePushConstants exceeds Vulkan minimum push constant size");
```

### 6.6 Shader spec

The outline vertex shader is a copy of the geometry pass vertex shader with one difference:
the model matrix is provided via push constants instead of a per-object UBO. The fragment
shader ignores all lighting inputs and outputs only `TintColor`.

```glsl
// editor_outline.vert (GLSL 450)
layout(push_constant) uniform PC {
    mat4 u_Model;
    vec4 u_TintColor;
} pc;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 u_View;
    mat4 u_Projection;
} cam;

layout(location = 0) in vec3 a_Position;

void main() {
    gl_Position = cam.u_Projection * cam.u_View * pc.u_Model * vec4(a_Position, 1.0);
}

// editor_outline.frag (GLSL 450)
layout(push_constant) uniform PC {
    mat4 u_Model;
    vec4 u_TintColor;
} pc;

layout(location = 0) out vec4 out_Color;

void main() {
    out_Color = pc.u_TintColor;
}
```

These shaders are in `ZEngine/Shaders/Editor/`. They are compiled to SPIR-V by the
existing shader cook pipeline but only when `ZENGINE_EDITOR` is set at cook time.

---

## 7. Gizmo — transform handles

Once an entity is selected, `ImGuizmo` renders translate, rotate, and scale handles
superimposed over the entity's position in the viewport. The gizmo is rendered via the
existing `ImGuizmo` integration that Tetragrama already provides. This section specifies
the post-migration wiring.

### 7.1 Gizmo mode

```cpp
// EditorSelectionContext stores the current operation and space.
// Both are set by keyboard shortcuts (Section 9) and persist across frames.

enum class GizmoMode { Translate, Rotate, Scale };

// Mapping to ImGuizmo constants:
//   GizmoMode::Translate → ImGuizmo::TRANSLATE
//   GizmoMode::Rotate    → ImGuizmo::ROTATE
//   GizmoMode::Scale     → ImGuizmo::SCALE

// Space toggle (G key):
//   ImGuizmo::WORLD  — handles aligned to world axes
//   ImGuizmo::LOCAL  — handles aligned to entity's local axes
```

### 7.2 Gizmo invocation (inside EditorSelectionSystemTick, Section 5.2)

```cpp
if (selection->HasSelection) {
    auto* transform = scene.GetComponent<ECS::Components::TransformComponent>(
        selection->Selected);
    if (transform) {
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(
            viewport.Pos.x, viewport.Pos.y,
            viewport.Size.x, viewport.Size.y);

        const Core::Maths::Mat4f view       = camera->ViewMatrix();
        const Core::Maths::Mat4f projection = camera->ProjectionMatrix();
        Core::Maths::Mat4f model = Core::Maths::TRS(
            transform->Position,
            Core::Maths::FromEulerAngles(transform->Rotation),
            transform->Scale);

        if (ImGuizmo::Manipulate(
                view.m_data[0],
                projection.m_data[0],
                ctx->GizmoOperation,
                ctx->GizmoMode,
                model.m_data[0]))
        {
            DecomposeTransformComponent(
                model,
                transform->Position,
                transform->Rotation,
                transform->Scale);
        }
    }
}
```

### 7.3 DecomposeTransformComponent

`ImGuizmo::Manipulate` writes a 4x4 model matrix in-place. Decomposing it back into
`Position`, `Rotation` (Euler angles, radians), and `Scale` uses `ImGuizmo::DecomposeMatrixToComponents`:

```cpp
void DecomposeTransformComponent(
    const Core::Maths::Mat4f& model,
    Core::Maths::Vec3f&       out_position,
    Core::Maths::Vec3f&       out_rotation_euler,
    Core::Maths::Vec3f&       out_scale)
{
    float translation[3], rotation[3], scale[3];
    ImGuizmo::DecomposeMatrixToComponents(
        model.m_data[0], translation, rotation, scale);

    out_position = { translation[0], translation[1], translation[2] };
    // ImGuizmo returns rotation in degrees; convert to radians
    constexpr float kDegToRad = 3.14159265f / 180.f;
    out_rotation_euler = {
        rotation[0] * kDegToRad,
        rotation[1] * kDegToRad,
        rotation[2] * kDegToRad
    };
    out_scale = { scale[0], scale[1], scale[2] };
}
```

### 7.4 Gizmo and hover/selection conflict

`ImGuizmo::IsOver()` returns true when the mouse is on a gizmo handle. When that is true,
the viewport click handler and hover raycast must not fire — the mouse is intended for the
gizmo, not for entity picking. This is already handled in Section 5.2 via `gizmo_active`.

---

## 8. Viewport hover highlight

Every frame in Edit mode, while the mouse is inside the viewport and ImGuizmo is not
consuming input, a raycast is fired against the physics world to determine which entity
the mouse is hovering over. This drives:

1. `EditorSelectionState::Hovered` — updated every frame (no click needed).
2. The `EditorOutlinePass` hover highlight (grey/white tint, no stencil trick, scale 1.0).
3. The cursor tooltip (entity name + EntityID) rendered by `SceneViewportUIComponent`.

### 8.1 Hover raycast (already shown in Section 5.2)

```cpp
// Every frame in Edit mode (not just on click):
if (mouse_in_viewport && !gizmo_active) {
    const Core::Maths::Vec3f ray_dir =
        ScreenToWorldRay(mouse_pos, viewport.Pos, viewport.Size, *camera);
    const Core::Maths::Vec3f ray_origin = camera->Position();

    constexpr float kHoverDistance = 1000.f;
    const Physics::RaycastResult hover = physics->Raycast(
        ray_origin, ray_dir, kHoverDistance, Physics::ObjectLayers::AllMask);

    selection->Hover(hover.Hit ? hover.HitEntity : ECS::INVALID_ENTITY);
} else {
    selection->ClearHover();
}
```

### 8.2 Performance note

`PhysicsWorld::Raycast` is a broadphase + narrowphase query. In a scene with a few
hundred to a few thousand entities, this runs in under 0.1 ms on typical desktop hardware
with Jolt's AABB tree. In Edit mode, the engine is not running game simulation, so the
physics budget is not shared with gameplay raycasts.

If profiling reveals the hover raycast is a bottleneck (unlikely below 10k entities),
the v2 optimization is to throttle the hover raycast to every other frame, or to use
the broadphase-only AABB test (`PhysicsWorld::BroadphaseRaycast`) and accept that the
hover highlight fires on bounding boxes rather than exact mesh colliders.

### 8.3 Tooltip

The hover tooltip is rendered inside `SceneViewportUIComponent::Render()`:

```cpp
if (selection->Hovered.IsValid()) {
    ImGui::BeginTooltip();

    const auto* name = scene.GetComponent<ECS::Components::NameComponent>(
        selection->Hovered);
    if (name)
        ImGui::Text("%s", name->Name);
    else
        ImGui::Text("Entity #%u (gen %u)",
            selection->Hovered.Index, selection->Hovered.Generation);

    ImGui::Text("ID: [%u, %u]",
        selection->Hovered.Index, selection->Hovered.Generation);

    ImGui::EndTooltip();
}
```

---

## 9. Keyboard shortcuts

| Key | Action | System |
|---|---|---|
| LMB in viewport | Select entity under cursor (raycast) | EditorSelectionSystem |
| Escape | Deselect | EditorSelectionSystem |
| Delete | Destroy selected entity via WorldCommands | EditorSelectionSystem |
| F | Frame camera on selected entity's AABB center | EditorSelectionSystem |
| T | Gizmo: Translate mode | EditorSelectionSystem |
| R | Gizmo: Rotate mode | EditorSelectionSystem |
| S | Gizmo: Scale mode | EditorSelectionSystem |
| G | Toggle Global / Local gizmo space | EditorSelectionSystem |
| Ctrl+D | Duplicate selected entity (all components copied) | EditorSelectionSystem |
| H | Toggle selected entity visibility (shows/hides MeshComponent) | EditorSelectionSystem |

All keyboard shortcuts are consumed only when the scene viewport ImGui window has focus.
The focus check uses `ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)` inside
the `SceneViewportUIComponent` window context, and that result is propagated to the
`EditorSelectionContext` each frame. The system tick checks `ctx->ViewportHasFocus` before
processing any keyboard shortcut.

### Frame camera (F key) implementation

```cpp
void FrameCameraOnSelected(
    ECS::Scene& scene,
    ECS::EntityID id,
    Rendering::Camera* camera)
{
    const auto* transform = scene.GetComponent<
        ECS::Components::TransformComponent>(id);
    const auto* mesh = scene.GetComponent<
        ECS::Components::MeshComponent>(id);

    if (!transform) return;

    // Compute bounding sphere radius from mesh AABB if available;
    // fall back to a fixed editor radius.
    float radius = 2.f;
    if (mesh) {
        const Core::Maths::Vec3f extents = mesh->AABBHalfExtents();
        radius = extents.Length() * 1.5f;
        if (radius < 0.5f) radius = 0.5f;
    }

    // Move camera to look at the entity from a comfortable distance.
    // The camera's forward direction is preserved; only position changes.
    const Core::Maths::Vec3f target    = transform->Position;
    const Core::Maths::Vec3f direction = camera->Forward();  // normalized

    camera->SetPosition(target - direction * radius);
    camera->LookAt(target);
}
```

---

## 10. Duplicate entity

When Ctrl+D is pressed on a selected entity, a copy is created with all reflected
components duplicated via `ComponentReflectionRegistry`. The duplicate is offset by
`+1` on the X axis so it is immediately visible and not stacked on the original.
After creation the selection switches to the new entity.

```cpp
void DuplicateSelected(
    ECS::Scene&           scene,
    ECS::WorldCommands&   cmds,
    EditorSelectionState* selection)
{
    ZENGINE_VALIDATE_ASSERT(selection->HasSelection,
        "DuplicateSelected called with no active selection");

    const ECS::EntityID src = selection->Selected;

    // Deferred create: the lambda runs after this tick completes.
    // Captures src by value (EntityID is 8 bytes, trivially copyable).
    // Captures selection by pointer — lifetime is guaranteed by EditorPlayModeSystem.
    cmds.DeferCreateEntity([src, &scene, selection](ECS::EntityID new_id) {
        // Iterate all registered reflectable components and copy each one
        // that exists on the source entity.
        ECS::ComponentReflectionRegistry::Get().ForEach(
            [&](const ECS::ComponentMeta& meta) {
                void* src_data = scene.GetComponentRaw(src, meta.TypeID);
                if (!src_data) return;  // source entity does not have this component

                // Add the component to new_id (zero-initialized)
                scene.AddComponentRaw(new_id, meta.TypeID, meta.Size, meta.Align);
                void* dst_data = scene.GetComponentRaw(new_id, meta.TypeID);
                ZENGINE_VALIDATE_ASSERT(dst_data != nullptr,
                    "DuplicateSelected: AddComponentRaw did not produce a valid pointer");

                // Binary copy — safe because ECS components are plain data structs
                // with no pointer members (enforced by ComponentOwnershipRules).
                memcpy(dst_data, src_data, meta.Size);
            });

        // Offset position so duplicate is not stacked on original.
        if (auto* t = scene.GetComponent<ECS::Components::TransformComponent>(new_id))
            t->Position.x += 1.f;

        // Select the newly created entity.
        selection->Select(new_id);
    });
}
```

### Constraints on component copying

`memcpy` is correct only because ECS components are plain data structs with no owning
pointer members. This is enforced by `ComponentOwnershipRules` in `actor-ecs-architecture.md`
(Section 5). Asset handles (e.g., `MeshHandle`, `MaterialHandle`) are stable opaque
integers — copying them is safe and correct; both entities will reference the same asset
without double-ownership.

Components that store `EntityID` fields (e.g., a `ParentComponent { EntityID Parent }`)
are copied verbatim, which means the duplicate starts with the same parent as the original.
Rename the duplicate and re-parent as needed in a subsequent editor operation.

---

## 11. Toggle visibility (H key)

Visibility toggling hides or shows a mesh without destroying the entity. The mechanism
is a `VisibilityComponent` flag:

```cpp
struct VisibilityComponent {
    bool Visible = true;
};
```

The `GeometryPass` skips entities where `VisibilityComponent::Visible == false`. If the
entity does not have a `VisibilityComponent`, it is treated as visible.

```cpp
void ToggleVisibility(ECS::Scene& scene, ECS::EntityID id)
{
    auto* vis = scene.GetComponent<ECS::Components::VisibilityComponent>(id);
    if (vis) {
        vis->Visible = !vis->Visible;
    } else {
        // Entity had no visibility component — add one in the hidden state
        // (pressing H for the first time on an entity hides it).
        scene.AddComponent<ECS::Components::VisibilityComponent>(
            id, ECS::Components::VisibilityComponent{ .Visible = false });
    }
}
```

---

## 12. File layout

All files are compiled only when `ZENGINE_EDITOR` is defined. The outline pass shaders
are compiled only when the shader cook pipeline has `ZENGINE_EDITOR` set.

```
ZEngine/
  Editor/
    Selection/
      EditorSelectionState.h         — EditorSelectionState struct
      EditorSelectionSystem.h        — EditorSelectionContext, function declarations
      EditorSelectionSystem.cpp      — EditorSelectionSystemTick, all helpers
    Rendering/
      EditorOutlinePass.h            — EditorOutlinePass class declaration
      EditorOutlinePass.cpp          — Setup, Compile, Execute implementations
  Shaders/
    Editor/
      editor_outline.vert            — outline vertex shader (GLSL 450)
      editor_outline.frag            — outline fragment shader (GLSL 450)
      editor_hover.frag              — hover tint fragment shader (GLSL 450)
```

The `Selection/` and `Rendering/` directories are new. They require additions to
`ZEngine/Editor/CMakeLists.txt` under an `if(ZENGINE_EDITOR)` guard.

---

## 13. Integration with EditorPlayModeSystem

`EditorPlayModeSystem` is the central coordinator for all editor-mode state. It:

1. Owns `EditorSelectionState` as a member.
2. Owns `EditorSelectionContext` and sets the static instance via
   `EditorSelectionContext::Set(&m_ctx)`.
3. Registers `EditorSelectionSystemTick` with `WorldTick` in Edit and Pause modes.
4. Passes `&m_selection_state` to `HierarchyViewUIComponent`,
   `InspectorViewUIComponent`, and `SceneViewportUIComponent` during their
   `Initialize(...)` calls.
5. Registers `EditorOutlinePass` with `RenderGraph::AddCallbackPass` in
   `AppRenderPipeline::Initialize`, guarded by `#ifdef ZENGINE_EDITOR`.
6. Updates `EditorSelectionContext::ViewportBounds` each frame from
   `SceneViewportUIComponent::GetBounds()`.

`EditorPlayModeSystem` does not own `PhysicsWorld`, `WorldCommands`, or the `Camera`. It
receives non-owning pointers to these at initialization time and stores them in the context.

---

## 14. Deliverables checklist

### Core selection state

- [ ] `EditorSelectionState.h` — struct with `Selected`, `Hovered`, `HasSelection`,
      `Select()`, `Deselect()`, `Hover()`, `ClearHover()`, `IsSelected()`, `IsHovered()`
- [ ] Confirm `EditorSelectionState` is 16 bytes (two EntityIDs + one bool + padding)
- [ ] Confirm `Select(INVALID_ENTITY)` triggers the assert
- [ ] Confirm `IsSelected(INVALID_ENTITY)` returns false when `HasSelection = true`
      and `Selected` is a valid but different entity

### EditorSelectionSystem

- [ ] `EditorSelectionContext` struct and `Set/Get` static methods
- [ ] `EditorSelectionSystemTick` registered with `WorldTick` in Edit and Pause modes
- [ ] System not registered in Play mode — verify by running game and checking no
      editor ray is fired
- [ ] `kEditorSelectionDeps.WriteMask == 0` — verified by scheduler at Commit time
- [ ] Hover raycast fires every frame (not only on click)
- [ ] Hover clears when mouse leaves viewport
- [ ] Hover suppressed when `ImGuizmo::IsOver() || ImGuizmo::IsUsing()`
- [ ] Viewport focus check prevents hotkeys from firing when a text field has focus

### Hierarchy click selection

- [ ] `HierarchyViewUIComponent::RenderNode` uses `IsSelected(id)` for highlight
- [ ] Single-click fires `selection->Select(node.EntityID)`
- [ ] Right-click opens context menu with Rename, Duplicate, Add Child, Delete
- [ ] Delete deselects immediately before queuing `DeferDestroyEntity`
- [ ] Tree node shows selected state via `ImGuiTreeNodeFlags_Selected`

### Viewport raycast selection

- [ ] `ScreenToWorldRay` produces correct NDC for corners: (0,0) top-left → (-1, 1),
      (viewport_size.x, viewport_size.y) bottom-right → (1, -1)
- [ ] Click on entity fires selection
- [ ] Click on empty space fires deselect
- [ ] `ObjectLayers::AllMask` used so all entity layers are pickable in editor

### Outline pass

- [ ] `EditorOutlinePass::Setup` declares inputs `ldr_final`, `hdr_depth`
- [ ] Pass is registered only under `#ifdef ZENGINE_EDITOR`
- [ ] Outline is orange (1, 0.5, 0, 1)
- [ ] Hover tint is grey (0.7, 0.7, 0.7, 0.25)
- [ ] Outline does not appear in front of geometry that occludes the selected entity
      (depth test enabled)
- [ ] Outline ring only (not filled body) via stencil subpass technique
- [ ] Pass early-returns with zero draw calls when `!HasSelection && !Hovered.IsValid()`
- [ ] `editor_outline.vert/frag` and `editor_hover.frag` compiled to SPIR-V
- [ ] Shaders excluded from non-editor cook

### Gizmo

- [ ] ImGuizmo renders translate handles by default on selection
- [ ] T key → Translate, R key → Rotate, S key → Scale
- [ ] G key → toggles world/local space
- [ ] `DecomposeTransformComponent` converts ImGuizmo degree output to radians
- [ ] Gizmo fires only when `selection->HasSelection && transform != nullptr`
- [ ] `ImGuizmo::SetRect` called with current viewport bounds every frame
- [ ] Mouse click suppressed while `ImGuizmo::IsOver()` or `ImGuizmo::IsUsing()`

### Keyboard shortcuts

- [ ] Escape deselects
- [ ] Delete destroys and deselects (deferred via `WorldCommands`)
- [ ] F frames camera on entity AABB center; no-op if no transform
- [ ] Ctrl+D duplicates entity; selection switches to duplicate
- [ ] H toggles visibility via `VisibilityComponent`
- [ ] None of the shortcuts fire when viewport does not have focus

### Duplicate entity

- [ ] `DuplicateSelected` uses `ComponentReflectionRegistry::ForEach` to copy all components
- [ ] Binary `memcpy` used for component body; no virtual copy constructors
- [ ] Duplicate is offset +1 on X axis
- [ ] Selection switches to the new entity after deferred create executes
- [ ] Source entity is unchanged after duplication

### Frame camera (F key)

- [ ] Camera moves to look at entity center from distance proportional to mesh AABB
- [ ] Camera forward direction is preserved (only position changes)
- [ ] No-op if entity has no `TransformComponent`
- [ ] Fallback radius 2.0 m when entity has no `MeshComponent`

### Tests

- [ ] `Tests/Editor/Selection/EditorSelectionStateTest.cpp`
      — Select/Deselect/Hover/ClearHover state transitions
      — `IsSelected(INVALID_ENTITY)` always returns false
      — `Select` followed by `Deselect` leaves `HasSelection = false`
- [ ] `Tests/Editor/Selection/ScreenToWorldRayTest.cpp`
      — Corner pixel → known NDC → known world direction (unit test with fixed camera)
      — Viewport center → ray aligned with camera forward
- [ ] `Tests/Editor/Selection/DuplicateTest.cpp`
      — Source entity components unchanged after duplicate
      — Duplicate has same component values except `TransformComponent::Position.x += 1`
      — Selection moved to new entity
- [ ] Manual integration test: open a scene, click an entity in the hierarchy →
      inspector shows components; click in the viewport → outline appears, gizmo appears;
      press Delete → entity removed, inspector clears; press Escape → selection clears
