# ZEngine — Render Graph Integration Guide

**Priority:** P0 — renderer and editor overlays depend on it
**Status:** Active implementation reference
**Related:** rendering-flow.md, render-graph-redesign.md, pso-cache-architecture.md

## 1. Purpose

RenderGraph rebuilds a virtual graph for each frame, validates declarations,
derives dependencies and synchronization, allocates or reuses graph resources,
and records the resulting queue work. Persistent callback passes declare their
per-frame work; the graph owns attachment compatibility, resource state, and
schedule.

This document describes the current callback API. Earlier Setup/Compile-only
examples are retired.

## 2. Callback lifecycle

IRenderGraphCallbackPass provides this contract:

| Callback | When | Responsibility |
|---|---|---|
| Register | Once while constructing this frame's virtual graph | Declare exact resource versions, accesses, queue/flags, and whether the pass participates. |
| BuildGraphicsPipelineDescription or compute-shader query | When graph creates compatible backend work | Provide static pipeline description only; no per-frame resource or extent ownership. |
| Prepare | After graph compilation and resolved resource binding | Refresh frame-local descriptors, constants, and pass-local state. |
| Execute | During command recording for non-graph-managed work | Record the complete draw/dispatch body. |
| RecordDraw | During graph-managed dynamic rendering when supported | Record graphics work inside the active rendering instance and report whether work was recorded. |
| Deinitialize | Persistent pass teardown | Release only persistent resources owned by the callback. |

Register returns false to omit a pass, its virtual resources, and its
synchronization from that frame. Execute is still the required callback for a
pass that cannot use graph-managed recording.

Callbacks do not own transient attachments or create ad hoc per-frame Vulkan
images/framebuffers. Per-frame data is supplied through `SceneData` and the
render-state handoff. Camera, sky, light, resize, and overlay configuration are
copied into `RenderFrameState`; its `RenderScene*` is still borrowed in the
current implementation. Callback code must not reach into mutable main-thread
ECS/editor state directly, and the planned immutable scene snapshot will remove
that remaining borrowed-scene boundary.

## 3. Resource declarations

Register uses RenderGraphResourceBuilder and typed RGResourceHandle values:

- declare texture/buffer writes and reads with exact versions;
- import externally owned textures only with their valid initial layout;
- declare bindless reads precisely when the graph can identify the image;
- use typed handles while recording rather than repeated name lookups;
- declare queue transfers and conditional rendering through graph APIs; and
- request CPU readback from an exact GPU buffer version.

The graph derives write/read, write/write, and read/write dependencies, barriers,
queue ownership transitions, lifetimes, culling, and topological/queue schedule.
Names remain diagnostics and registration keys; typed handles carry the exact
resource version consumed by a pass.

Resource declaration must be complete before command recording. A callback may
not discover a new dependency while Execute or RecordDraw is running.

## 4. Persistent versus frame-local ownership

| Resource | Owner and lifetime |
|---|---|
| Virtual pass/resource declarations | RenderGraph frame arena; rebuilt every frame. |
| Transient textures/buffers/framebuffers | RenderGraph allocator/cache; graph-controlled lifetime. |
| Pipelines and persistent callback state | Callback/resource manager; compatible with graph attachment contract. |
| Per-frame descriptors and camera | Renderer `SceneData` plus copied `RenderFrameState` configuration. Scene draw input is currently rebuilt from a borrowed `RenderScene`; replace it with an immutable scene snapshot before production editor mutations. |
| Readback bytes | Graph readback ring; valid only during completion callback. |
| GPU upload tickets and queue timelines | RenderGraph/resource manager synchronization domain. |

A render pass may retain a stable pipeline or static geometry allocation, but
never a pointer into a mutable Scene or transient resource from another frame.

## 5. Readback contract

DeclareReadback copies one exact GPU buffer version into the graph-managed mapped
readback ring. Its callback runs on the render thread after the exact submission
timeline has completed, and the byte pointer is valid only for that callback.

The API is buffer-based. Image picking must first copy the selected image pixel
or region into a declared transfer buffer. Code must not assume a result arrives
the next frame, block command recording waiting for it, or retain the byte
pointer beyond the callback. Main-thread consumers receive durable copied data
with a frame token and validate it against their current scene/viewport state.

## 6. Current renderer and editor integration

Copied frame configuration is published through the latest-state channel
described by rendering-flow.md. The renderer constructs per-frame scene draw
data, camera data, and lights before RenderGraph execution. There is no shipped
editor overlay snapshot, object-ID picking, outline, or gizmo pass yet.

The current high-level dependency path is:

~~~text
frustum culling -> depth pre-pass -> G-buffer -> lighting
  -> optional environment background -> editor overlays -> final ZUI composition
~~~

Exact pass participation is data-driven. The current compatibility grid receives
its configuration through the borrowed render scene; environment settings are
copied with the frame state. New editor outlines, object-ID picking, gizmo
handles, and the analytic grid must instead consume immutable snapshot data and
declare their exact depth/color/transfer dependencies rather than rely on a
manually maintained global pass order.

The editor-overlay color stage belongs after scene tone mapping and before final
ZUI composition. If the active renderer currently uses a different color stage,
the change is a graph contract change with validation/reference-image coverage,
not an overlay-local shortcut.

## 7. Writing a new pass

1. Define immutable frame input and persistent callback state separately.
2. In Register, validate enable conditions and declare all exact resource access.
3. Specify static graphics or compute pipeline requirements only.
4. In Prepare, resolve frame-local descriptors and constants from typed handles.
5. Record in Execute or RecordDraw without allocating/reading mutable scene state.
6. Declare timing/readback/conditional behavior through graph APIs.
7. Test disabled, culled, resized, device-recreated, and validation-layer paths.

Use cstring for new engine-owned pass/resource names. External C APIs retain
their required character-pointer signatures.

## 8. Required validation

- Resource declaration validation catches absent producers, invalid versions,
  incompatible formats/usages, and feedback loops.
- Dependency schedule and barriers are correct across graphics/transfer/compute
  queue combinations.
- Resize and transient reuse never expose stale texture/framebuffer handles.
- Callback state survives graph rebuilds and releases persistent resources safely.
- Disabled/cullable passes leave no undeclared reads or presentation gap.
- Readback completion, pointer lifetime, and image-to-buffer staging are tested.
- The future editor overlays consume immutable snapshot data and pass GPU validation.

## 9. Documentation maintenance

When a RenderGraph API changes, update this guide, rendering-flow.md, and every
active design document that shows callback signatures before merging dependent
work. Historical completed documents may retain their original implementation
record but should be labelled historical when their API examples differ.
