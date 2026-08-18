# Rendering Flow — Engine + Editor (Post-Rearchitecture)

**Relates to:** `gpu-allocator-rearchitecture.md`, `per-frame-upload-heap.md`
**Scope:** Full per-frame rendering pipeline from user interaction to GPU submission, covering both the runtime engine and the Tetragrama editor.

---

## 1. Two-Thread Architecture

The engine splits work across two threads. This is fixed — not configurable.

```
+------------------------------------------------------+
|                    MAIN THREAD                        |
|                                                       |
|  PollEvent  ->  Update  ->  OnRenderUI  ->  PrepareScene |
|                             (ImGui)      (payload)    |
|                                              |        |
|                               MailBoxBuffer.store()   |
+------------------------------------------------------+
                                      |
                              double-buffer handoff
                                      |
+------------------------------------------------------+
|                   RENDER THREAD                       |
|                                                       |
|  BeginFrame  ->  RenderScene  ->  RenderOverlay  ->  EndFrame |
|  (acquire)     (3D scene)       (ImGui)           (submit+present) |
+------------------------------------------------------+
```

The two threads never share a mutex on the hot path. The handoff is a single atomic
store/load on `MailBoxBufferHead`. The main thread builds ImGui draw lists and scene
payload; the render thread consumes them and touches Vulkan exclusively.

---

## 2. Per-Frame Lifecycle — Step by Step

Frame index `fi` = `SwapchainPtr->CurrentFrame->Index` (0, 1, or 2 in a triple-buffered setup).

### MAIN THREAD

```
[1] PollEvent()
    GLFW events -> window resize flag, input state

[2] g_app->Update(dt)
    Editor::OnUpdate()
      UILayer->Update(dt)
        HierarchyViewUIComponent::Update()
          Pop PendingOnLoadHierarchies
          CreateOrGetMeshAllocation()     <- memcpy vertices/indices into CPU arrays
          MeshAllocationDirty[0,1,2] = true
          TransformBufferDirty[0,1,2] = true
        other panels (InspectorView, etc.) -- pure CPU

[3] pipeline->BeginOverlayFrame()
    ImGui_ImplGlfw_NewFrame()
    ImGui::NewFrame()
    ImGuizmo::BeginFrame()

[4] g_app->OnRenderUI()
    Editor::OnRenderUI()
      UILayer->Render()
        DockspaceUIComponent::Render()    <- docking layout, menu bar
        SceneViewportUIComponent::Render()
          ImGui::Image(FrameColorRenderTarget.Index, ...)  <- bindless handle
          ImGuizmo::Manipulate() -> mutates GlobalTransforms[selected]
          [BUG] TransformBufferDirty NOT set here (known TODO)
        HierarchyViewUIComponent::Render()  <- scene tree, selection
        InspectorViewUIComponent::Render()  <- property editor (no GPU)

[5] pipeline->EndOverlayFrame()
    ImGui::Render()           <- finalises draw lists
    FillOverlayPayload()      <- copies ImDrawData ptr into payload struct

[6] g_app->PrepareScene(payload)
    sets payload.Scene = current_scene, payload.Camera = editor_camera

[7] MailBoxBufferHead.store(next_slot)    <- render thread sees new frame payload
```

### RENDER THREAD

```
[1] BeginFrame()
    swapchain->AcquireNextImage(mailbox_head)
      vkAcquireNextImageKHR
      Device->TickMemory()                  <- NEW (post-rearchitecture)
        vkGetSemaphoreCounterValue(RenderTimeline)
        PendingFree.Drain(completed)        <- free GPU resources GPU is done with
        GpuMem.Ring.Drain(completed)        <- reclaim staging ring chunks
        GpuMem.SampleBudgets()              <- per-heap pressure check
        FrameHeaps[fi].Reset()              <- bump pointer = 0, O(1)
    CommandBufferMgr->ResetPool(fi, thread)
    RRM->CompleteDeferrals()
    GetCommandBuffer(GRAPHIC, fi, thread)
    CurrentCmdBuf->Begin()

[2] RenderScene(payload)

    [RESIZE CHECK]  if RenderTargetResizeRequests not empty:
      RenderGraph->Resize()
        Device->DeferFree({old FrameColorRT, old FrameDepthRT})
        // vmaDestroyImage is called by DeferredFreeQueue::Drain once the timeline
        // confirms the GPU is done with those images — the caller only enqueues.
        GpuMem.AllocateImage(new size, RenderTarget)
        Device->TextureHandleToUpdates.Push(new_handle)
        Rebuild all VkFramebuffer objects

    [MESH DIRTY]  if MeshAllocationDirty[fi]:
      vtx_buffer_set->At(fi).Write(fi, 0, scene->Vertices)
        RRM->UploadBuffer()
          vmaGetAllocationMemoryProperties -> HOST_VISIBLE?
          YES: vmaCopyMemoryToAllocation + vmaFlushAllocation (if !coherent)
          NO:  GpuMem.Ring.Allocate() -> vkCmdCopyBuffer -> Ring.Submit(signal_val)
      idx_buffer_set->At(fi).Write(...)       <- same path
      rd_buffer_set->At(fi).Write(...)        <- SubMeshAllocations
      indirect_buffer_set->At(fi).Write(...)  <- VkDrawIndirectCommands
      [POST-REARCHITECTURE: all 4 writes push into FrameHeaps[fi] instead]

    [TRANSFORM DIRTY]  if TransformBufferDirty[fi]:
      transform_buffer_set->At(fi).Write(fi, 0, scene->GlobalTransforms)
      [POST-REARCHITECTURE: heap.Push(GlobalTransforms, ...)]

    GraphicRenderer::DrawScene(fi, thread, cmd, camera)
      camera_buf->Write(fi, thread, &UBOCameraLayout)    <- view/proj/pos
      material_buf->Write(fi, thread, GPUMeshMaterials)  <- full material array
      RenderGraph->Execute(cmd)

        // UploadPass has been removed. SkyboxPass and GridPass now bind the
        // global vertex/index buffers registered via RRM::RegisterBuiltinGeometry;
        // no per-pass VB/IB writes are needed.

        // Passes below reflect the current live implementation.
        // Shadow, post-process, and gizmo passes are added by their
        // respective future-plan docs and are not listed here.

        [DepthPrePass]
          TransitionImageLayout(FrameDepthRT -> DEPTH_STENCIL_ATTACHMENT)
          BeginRenderPass(DepthOnlyFramebuffer)
          BindPipeline(depth_prepass_pipeline)
          BindDescriptorSets(fi)  <- UBCamera, VertexSB, IndexSB, TransformSB, DrawDataSB
          DrawIndirect(indirect_buffer, 0, draw_count)
          EndRenderPass

        [BasePass]  (deferred lighting / fullscreen triangle)
          TransitionImageLayout(FrameColorRT -> COLOR_ATTACHMENT)
          BeginRenderPass(OffscreenFramebuffer)
          BindPipeline(base_pipeline)
          BindDescriptorSets(fi)  <- all SBs + MatSB + TextureArray (bindless) + EnvMap
          Draw(3, 1, 0, 0)        <- fullscreen triangle
          EndRenderPass

        [SkyboxPass]
          BindVertexBuffer / BindIndexBuffer (global VB/IB via RRM::RegisterBuiltinGeometry; cube geometry, 36 indices)
          BindDescriptorSets(fi)
          DrawIndexed(36, 1, 0, 0, 0)

        [GridPass]
          BindVertexBuffer / BindIndexBuffer (global VB/IB via RRM::RegisterBuiltinGeometry; quad geometry, 6 indices)
          PushConstants(grid_params)
          DrawIndexed(6, 1, 0, 0, 0)

[3] RenderOverlay(payload)   <- ImGui on swapchain framebuffer
    TransitionImageLayout(SwapchainImage -> COLOR_ATTACHMENT)
    BeginRenderPass(SwapchainFramebuffer[image_index], UIPass)
    Write per-frame ImGui geometry:
      imgui_vtx_buf->Write(fi, 0, ImDrawData->Vertices)  <- VkBuffer in HOST_VISIBLE
      imgui_idx_buf->Write(fi, 0, ImDrawData->Indices)
    Secondary command buffer per ImGui viewport:
      BindPipeline(imgui_pipeline)
      BindVertexBuffer(imgui_vtx_buf)
      BindIndexBuffer(imgui_idx_buf)
      BindDescriptorSets(fi)   <- bindless TextureArray (ImGui font + scene textures)
      for each ImDrawCmd:
        SetScissor(clip_rect)
        PushConstants(scale, translate, texture_id)
        DrawIndexed(elem_count, 1, idx_offset, vtx_offset, 0)
    EndRenderPass

[4] EndFrame()
    RRM->SubmitTextureJobs()       <- flush timeline semaphore job queue
    CommandBufferMgr->EndEnqueuedBuffers()
    SwapchainPtr->Present()
      3-submit timeline pattern (acquire bridge -> render -> present bridge)
      vkQueuePresentKHR
      IdleFrameCount++ (if no work submitted)
```

---

## 3. GPU Memory Timeline — One Frame

```
CPU                                          GPU
----------------------------------------------------------------------
AcquireNextImage
  vkGetSemaphoreCounterValue(timeline N-2)
  PendingFree.Drain()      ----free N-2 resources---->  (already idle)
  Ring.Drain()             ----reclaim N-2 chunks---->
  FrameHeaps[fi].Reset()

  [HOST_VISIBLE buffers]
  vmaCopyMemoryToAllocation ----write-------------->  (not started)
  vmaFlushAllocation

  [DEVICE_LOCAL buffers]
  Ring.Allocate(chunk)
  memcpy -> ring mapped ptr
  vkCmdCopyBuffer (staged)  ----will copy---------->  (queued)

  vkCmdDrawIndirect         ----draw-------------->   (queued after copy)
  vkCmdDraw (fullscreen tri) ---draw-------------->   (queued)
  ...

EndFrame
  vkQueueSubmit (signal N)                             <- GPU starts
                                       ...executing...
                            <---signal N signalled---  GPU done with frame N
```

Frame `fi` resources are safe to destroy when `timeline_completed >= N` (the signal value
recorded when they were enqueued in `DeferFree`).

---

## 4. Editor User Actions — GPU Impact Map

| User Action | CPU Side | GPU Side | Frames Until Visible |
|---|---|---|---|
| Open `.zemesh` file | `secure_memcpy` vertices/indices into `EditorScene::Vertices`, set `MeshAllocationDirty[0,1,2]` | All 3 frame buffer sets re-written on their respective frames | 1-3 frames (each frame slot catches up) |
| Move object (ImGuizmo) | Mutates `GlobalTransforms[node]` in place | Nothing -- `TransformBufferDirty` NOT set (known bug) | Never (until another dirty event fires) |
| Select object | Sets `SelectedSceneNode` | Nothing | Immediate (CPU-only highlight) |
| Resize viewport | Pushes `RenderTargetResizeRequest` | `vmaDestroyImage` + `GpuMem.AllocateImage` for color+depth RT, rebuild framebuffers, update bindless descriptor slot | 1 frame |
| Change material property | Inspector writes to `EditorScene::Materials` CPU array | `MaterialBufferHandle->Write()` fires next frame (storage buffer re-upload) | 1 frame |
| Add/remove light | Updates CPU light array | `LightBufferHandle->Write()` next frame | 1 frame |
| Camera move (WASD/mouse) | `FlyCamera` updates `ViewMatrix`, `ProjectionMatrix` | `UBOCameraLayout` written into `camera_buffer->At(fi)` each frame (always dirty) | 0 -- same frame |

---

## 5. RRM Upload Path (Current Implementation)

`RenderResourceManager (RRM)` now owns all uploads — buffer and texture alike. `AsyncResourceLoader` has been fully replaced by RRM.

### Buffer domains

| Buffer | Domain | Write path | Stall |
|---|---|---|---|
| Vertex (geometry) | DeviceGeometry | Ring staging + dedicated cmd + vkWaitForFences | Once when MeshAllocationDirty |
| Index (geometry) | DeviceGeometry | Ring staging + dedicated cmd + vkWaitForFences | Once when MeshAllocationDirty |
| Transform | HostUniform | vmaCopyMemoryToAllocation | Zero (HOST_VISIBLE) |
| RenderData (sub-mesh) | HostUniform | vmaCopyMemoryToAllocation | Zero |
| Material | HostUniform | vmaCopyMemoryToAllocation | Zero |
| ImGui VB/IB | HostUniform | vmaCopyMemoryToAllocation | Zero |

### MeshAllocationDirty collapse

`MeshAllocationDirty[3]` collapsed to a single `atomic_bool`. Because geometry uses one DEVICE_LOCAL buffer shared by all frames, there is no per-frame triple copy — when the flag is set and cleared, all frames see the same buffer.

### RRM dedicated command pool

```
RRM::Initialize()
  vkCreateCommandPool(GRAPHIC, RESET_COMMAND_BUFFER_BIT) -> m_upload_pool
  vkAllocateCommandBuffers(m_upload_pool, 1)             -> m_upload_cmd
  vkCreateFence()                                        -> m_upload_fence
```

`UpdateBuffer(dst, data, byte_size, offset)`:
- HOST_VISIBLE dst: `vmaCopyMemoryToAllocation` + optional flush, return immediately
- DEVICE_LOCAL dst: `GpuMem.Ring.Allocate` -> `vkCmdCopyBuffer` -> `vkQueueSubmit` -> `vkWaitForFences`

No per-pool slot tracking. No `RetireValues` array. No `AsyncTimelineJobQueue` for buffer uploads.

### Frame boundary placement

```
AppRenderPipeline::BeginFrame()
  swapchain->AcquireNextImage()       <- frame_index valid after this
  RRM::BeginFrame(fi)                 <- flush pending asset uploads
  CommandBufferMgr::ResetPool(fi)
  RRM::CompleteDeferrals()

AppRenderPipeline::EndFrame()
  RRM::SubmitTextureJobs()   <- texture uploads only
  Present()
  RRM::EndFrame(fi)                   <- drain aged swap entries

---

## 6. Known Gaps and Bugs in Current Flow

| Type | Location | Issue |
|---|---|---|
| BUG | `HierarchyViewUIComponent.cpp` | ImGuizmo manipulate mutates `GlobalTransforms` but does NOT set `TransformBufferDirty`. Transform change never reaches GPU. |
| GAP | `RenderScene()` | No LRU eviction path when `GpuMem.AllocateBuffer` returns null (pool full). Engine would assert rather than gracefully degrade. |
| GAP | `SceneViewportUIComponent.cpp` | Viewport resize request is not rate-limited. Rapid window dragging fires one `RenderGraph::Resize()` per render frame during the drag. |
| GAP | `RRM::UpdateBuffer` | vkWaitForFences on DEVICE_LOCAL uploads stalls the render thread. For mesh data written at load time this is acceptable; for streaming it is not. Needs async transfer queue path. |
| GAP | Multi-instance drag-drop | Re-dropping an already-loaded mesh is a no-op. Multi-instance support requires calling `SetState(Loaded)` on the existing hierarchy UUID to re-fire the hierarchy callback without re-importing. Deferred to scene-instancing milestone. |

---

## 7. Multi-Threaded Rendering Extension (Design)

The current architecture uses two threads (main + render). Extending to N render workers requires these changes:

### Command buffer recording
Each worker thread gets its own `CommandBuffer` from `CommandBufferMgr->GetCommandBuffer(GRAPHIC, fi, thread_idx, 0)`. Secondary command buffers are recorded in parallel then submitted as a batch on the primary.

### RRM per-thread upload pools
`m_upload_cmd` and `m_upload_fence` become arrays indexed by `thread_idx`:
```cpp
VkCommandPool   m_upload_pool[MAX_RENDER_THREADS];
VkCommandBuffer m_upload_cmd[MAX_RENDER_THREADS];
VkFence         m_upload_fence[MAX_RENDER_THREADS];
```
No mutex needed — each thread owns its slot.

### GpuAllocator::Ring
`Ring.Allocate` is already thread-safe (spinlock on `WritePos`). Multiple worker threads can push staging data simultaneously.

### Descriptor set binding
Descriptor sets are immutable after `vkUpdateDescriptorSets`. Workers bind them read-only — no synchronization needed.

### TransformBufferDirty
Transforms are HOST_VISIBLE; the main thread writes them via `vmaCopyMemoryToAllocation` before the render thread starts. A `std::atomic_thread_fence(release)` on the main thread + `acquire` on the render thread ensures visibility — no GPU sync needed.

---

## 8. Full Dependency Graph — Frame N

```
MAIN THREAD                         RENDER THREAD
------------------                  -----------------------------------------
PollEvent
  |
Update(dt)
  |- ImGuizmo -> GlobalTransforms
  `- MeshAllocationDirty = true
                                    AcquireNextImage(N)
  BeginOverlayFrame                   TickMemory()
  OnRenderUI -> ImDrawData               PendingFree.Drain(completed N-2)
  EndOverlayFrame                        Ring.Drain(completed N-2)
  PrepareScene -> payload                FrameHeaps[fi].Reset()
  MailBox.store(N)  ------------->    CommandPool reset
                                      CompleteDeferrals
                                      cmd->Begin()

                                      RenderScene(payload)
                                        dirty uploads -> heap.Push or Ring
                                        heap.Flush()
                                        GraphicRenderer::DrawScene()
                                          camera_buf write
                                          material_buf write
                                          RenderGraph::Execute()
                                            DepthPrePass
                                            BasePass
                                            SkyboxPass
                                            GridPass

                                      RenderOverlay(payload)
                                        imgui vtx/idx write
                                        secondary CB
                                        DrawIndexed x N

                                      EndFrame()
                                        SubmitAsyncJobs()
                                        EndEnqueuedBuffers()
                                        Present()
                                          vkQueueSubmit -> signal timeline N
                                          vkQueuePresentKHR
```
