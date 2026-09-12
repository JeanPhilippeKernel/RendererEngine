# Rendering Flow — Engine + Editor

**Relates to:** `render-graph-redesign.md`, `pso-cache-architecture.md`, `gpu-allocator-rearchitecture.md`
**Scope:** The current frame path from editor input to presentation. This document describes the implemented ZUI and render-graph path; it is not the retired ImGui design.

---

## 1. Thread ownership and the mailbox

The engine has a main thread and a render thread. During runtime, Vulkan command recording, queue submission, and presentation run on the render thread; startup resource creation completes before that split. The main thread owns events, simulation, editor logic, and ZUI tree construction.

```
MAIN THREAD                                      RENDER THREAD
-----------                                      -------------
Poll window and input                            consume next mailbox slot
tick world, imports, schedulers                  apply viewport resize, if any
update application and camera                    BeginFrame: acquire + upload/retirement work
build ZUI tree                                   RenderScene
prepare immutable ZUI payload                    RenderGraph::Execute
prepare scene/camera/resize payload              EndFrame: submit + present
publish mailbox slot  ------------------------>  retire mailbox slot
```

`AppRenderPipeline` owns three `RenderPayload` slots. The producer publishes the next slot with a release store on `MailBoxBufferHead`; the render thread consumes it with an acquire load and advances `MailBoxBufferTail` only after the frame is finished. If the mailbox is full, the main loop yields to the frame cap instead of overwriting a payload that the render thread may still read.

The payload contains:

- the render-scene and camera pointers;
- one coalesced viewport render-target extent;
- a mailbox-slot-arena-backed `ZUIRenderPayload` (vertices, indices, commands, scale, and framebuffer scale).

The ZUI payload arena is selected by mailbox slot, so its storage stays valid until the render thread has consumed that slot. The UI tree itself is built and discarded only on the main thread.

---

## 2. Main-thread frame

For every non-minimized window frame, `Engine::MainThreadRun()` performs the following work.

1. Poll window events, tick the VFS watcher, run fixed world steps, progress imports, and drain main-thread callbacks.
2. Call `GameApplication::Update(dt)`. It polls input, calls the application update, then updates the camera controller.
3. When overlay rendering is enabled, call `AppRenderPipeline::BeginOverlayFrame(dt)`, `GameApplication::OnRenderUI()`, and `EndOverlayFrame()`.
4. `BeginOverlayFrame()` updates the ZUI context with the logical GLFW window size and the physical/logical framebuffer scale. That keeps GLFW cursor coordinates, ZUI layout, and high-DPI scissor conversion in the same coordinate system.
5. Tetragrama builds the editor through `ZUILayer`: `ZUIPanelManagerComponent`, the dockspace shell, status bar, and panels such as `ViewportPanel`. This replaces the old ImGui component hierarchy.
6. `FillOverlayPayload()` walks the completed ZUI box tree and writes its draw list into the arena assigned to the mailbox slot.
7. The ECS scene is synchronized into the render scene. `GameApplication::PrepareScene()` drains all pending viewport-resize requests and keeps only the last requested extent, then attaches the current scene and camera.
8. Publish the mailbox slot.

`ViewportPanel` reads `GraphicRenderer::GetFrameOutput()` and emits an image box using its bindless texture index. The handle is published by the render thread after a real graph execution, using an index/generation pair guarded by a sequence counter. There is intentionally no output handle during renderer initialization: before ZUI declares its read of `FrameColor`, graph culling may legitimately leave that transient resource unallocated.

---

## 3. Render-thread frame

The render thread waits for a published payload. Its frame lifecycle is:

```
if payload contains a resize:
    AppRenderPipeline::ResizeRenderTarget()

BeginFrame()
    flush shader reload and asynchronous pipeline creation
    acquire a swapchain image
    RenderResourceManager::BeginFrame(frame index)
    collect swapchain async operations and texture releases
    reset command pools and retire completed texture-upload slots
    complete texture deferrals
    begin the application-owned primary graphics command buffer

RenderScene(camera, scene, zui payload)
    rebuild per-frame scene input and upload it
    set ZUIPass payload
    GraphicRenderer::DrawScene()
        RenderGraph::Execute()
    clear ZUIPass payload

EndFrame()
    finish deferred resource-manager batch work
    transition an otherwise-unused acquired image back to present, if necessary
    enqueue/end command buffers
    present the swapchain image
    submit deferred asynchronous uploads
```

An invalid acquired frame skips `RenderScene()` but still runs `EndFrame()` so the swapchain lifecycle remains balanced.

### Per-frame scene input

`AppRenderPipeline::RenderScene()` snapshots render-scene instances, resolves resident mesh allocations, calculates world-space bounds, extracts the camera frustum, and fills per-frame arrays for transforms, sub-mesh draw data, and frustum-culling input. Non-resident mesh handles are requested for streaming and are omitted until available.

The pipeline uploads those arrays along with the light array. `GraphicRenderer::DrawScene()` uploads materials and pushes `UBOCameraLayout` into the active frame heap, retaining the resulting dynamic-uniform offset in `SceneData`. The compute culling pass writes the indirect-draw buffer consumed by the depth pre-pass and G-buffer pass.

---

## 4. Default render graph

`GraphicRenderer` installs the persistent callback passes below. Each `RenderGraph::Execute()` registers the frame's virtual resources, validates declarations, culls dead work, builds a topological and queue schedule, allocates/reuses transient resources, derives synchronization2 barriers, compiles/binds passes, and records/submits the graph batches. Recordable dependency levels can use worker secondary command buffers; the final graphics batch remains available to the application primary buffer.

| Pass | Main inputs | Main result |
|---|---|---|
| Frustum Culling | culling input and indirect buffer | culled indirect commands |
| Depth Pre-Pass | global geometry, transforms, draw data, culled indirect commands | `FrameDepth` |
| G-Buffer | global geometry, transforms, draw data, materials, bindless textures, depth | albedo/AO, normal/roughness, metallic/emissive |
| Lighting | G-buffer textures, depth, lights, camera | sampled-capable `FrameColor` |
| Skybox | optional environment map, depth, `FrameColor` | `FrameColor` loaded and extended |
| Grid | depth and `FrameColor` | `FrameColor` loaded and extended |
| ZUI Draw | `FrameColor`, ZUI geometry, bindless texture array | acquired swapchain image |

Skybox and grid registration is conditional on their configuration. The ZUI pass declares both its `FrameColor` read and its swapchain write, making it the graph's presentation side effect and retaining the scene-color producer. When no ZUI draw geometry exists, its execution is empty; `EndFrame()` still ensures the acquired image is ready for presentation.

The graph owns resource state transitions and inter-pass synchronization. Individual callback passes own their draw body and use the resolved framebuffer/resource bindings supplied by the graph. `FrameColor` is recreated at the editor viewport extent, not the window/swapchain extent, and is sampled by ZUI through the global bindless texture array.

---

## 5. Viewport resize and output publication

```
ViewportPanel layout changes
    -> push requested viewport extent into ApplicationState queue
    -> PrepareScene drains queue and retains final extent
    -> payload crosses the mailbox
    -> render thread calls RenderGraph::Resize(width, height)
    -> next graph execution allocates/binds resized transient targets
    -> GraphicRenderer publishes the allocated FrameColor handle
    -> following ZUI build consumes that handle as its image texture
```

Requests are coalesced before they cross the mailbox, so intermediate panel extents from a dock/window drag do not produce a separate resize call. `RenderGraph::Resize()` is intentionally a slow path: it waits for the Vulkan device to be idle before replacing image/framebuffer backing and schedules old Vulkan objects for deferred destruction in valid lifetime order. A window/swapchain recreation does not force the viewport targets to the window extent; the panel controls the viewport extent.

`GraphicRenderer` publishes only valid physical `FrameColor` handles after `RenderGraph::Execute()`. It then queues the bindless descriptor update. This ordering matters: a UI image must never use the old setup-time handle for a graph resource that was culled or recreated.

---

## 6. Resource upload and retirement

`RenderResourceManager` owns buffer, texture, geometry-streaming, and descriptor-update work.

| Resource class | Current path |
|---|---|
| Host-visible per-frame buffers (transforms, draw data, lights, materials, ZUI vertices/indices) | `vmaCopyMemoryToAllocation`, plus a flush when memory is not coherent |
| Device-local buffer with available ring space | copy through the mapped staging ring, record/submit a graphics copy, then retire the ring chunk against the render timeline |
| Device-local buffer when the ring cannot serve it | create a staging buffer and join the resource manager's deferred batch |
| Texture uploads | deferral/streaming work completed at frame boundaries and submitted through the asynchronous upload queues |
| Texture descriptors | queued by producers and flushed after graph registration, before commands consume bindless textures |

`BeginFrame()` advances streaming, compaction, pending mesh swaps, and texture reload/release work. `CompleteDeferrals()` processes texture data that could not claim an upload slot earlier. `EndFrame()` finishes a deferred batch, and `SubmitAsyncUploads()` submits the resulting asynchronous upload operations after presentation has prepared the frame's waits.

---

## 7. Current constraints

- A render-target resize calls `vkDeviceWaitIdle`; it is correct and coalesced, but intentionally not a hitch-free resize path.
- The mapped-ring device-local `UpdateBuffer()` path performs a fence-synchronized graphics copy. It is appropriate for the current loading/update cadence but should not be mistaken for a fully asynchronous high-frequency streaming path.
- ZUI has fixed per-frame GPU capacities (65,536 vertices and 131,072 indices). Excess draw data is clipped for that frame rather than growing GPU buffers while rendering.
- The viewport texture reaches the UI through the main/render mailbox, so a newly allocated or resized viewport becomes visible after the next payload cycle. This is normal frame pipelining, not a stale-handle path.

---

## 8. Dependency sketch

```
main-thread ZUI build --payload--> ZUI Draw Pass --swapchain write--> present
                                  ^
                                  | sampled FrameColor
Frustum Culling --> Depth --> G-Buffer --> Lighting --> [Skybox] --> [Grid]
```

The graph supplies the actual scheduling, resource lifetimes, aliases, barriers, and queue waits; the sketch only expresses the default data flow.
