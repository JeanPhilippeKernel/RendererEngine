# ZEngine — Game Loop

**Status:** Implemented fixed-step main loop and mailbox-driven render loop; the ordering and ownership below are the current contract.

## Current frame flow

`Engine::Run()` starts `RenderThreadRun()` and runs `MainThreadRun()` on the application thread. The main thread publishes `RenderFrameState` through the bounded latest-state channel; the render thread retains the newest complete state when no new state is available. ZUI uses a separate multi-slot payload handoff, so UI geometry can retain its last valid payload independently of scene state. Camera, sky, light, resize, and overlay configuration are copied into the state, but its `RenderScene*` remains a borrowed pointer; a full immutable scene snapshot is pending production authoring work.

The main-thread order is currently:

```text
poll window events
tick VFS watcher
skip simulation/render-state publication while minimized
measure and clamp wall-clock delta
accumulate fixed simulation time
for each due fixed step:
  WorldTick -> WorldCommands flush -> ActorManager tick -> transform snapshot
tick import coordinator and main-thread scheduler
GameApplication::Update(raw delta)
build/publish a ZUI payload when enabled
sync hierarchy, ECS transforms, and ECS lights into RenderScene
prepare and publish RenderFrameState
apply the 300 FPS CPU cap
```

`FixedTimestepAccumulator` uses 1/60 second steps and accepts at most five steps per main-loop iteration. `FrameTimer` clamps a raw delta to 250 ms and maintains an eight-sample smoothed value. `FrameRateCap` sleeps and then yields through the final 0.5 ms of a 300 FPS budget. The render thread separately measures presentation-paced delta after `EndFrame()`, including vsync wait.

The render-thread order is:

```text
exit when termination is requested
freeze safely when the Vulkan device is lost
read newest completed RenderFrameState, or retain previous state
read latest ZUI payload when available
apply a requested render-target resize
BeginFrame
  -> flush shader/PSO jobs, acquire image, begin RRM frame
  -> retire frame-local work and reset command pools
RenderScene when frame and scene are valid
EndFrame
  -> end RRM frame, transition/present swapchain, submit async uploads
release superseded UI payload and advance frame context
```

`RenderFrameState` is a copied configuration packet, but its current `RenderScene*` member is still a borrowed pointer. It is only safe because the main thread owns scene mutation and the application must keep the scene alive until the render thread is joined. This does not yet satisfy the editor's production requirement for a renderer-owned immutable scene snapshot; see `rendering-flow.md` and `scene-serialization.md`.

## Input timing correction

`GameApplication::Update()` currently polls `InputManager` after the fixed simulation steps, then invokes `OnUpdate(raw_dt)` and the camera controller. It therefore supplies current input to presentation-paced application/camera work, not to the fixed `WorldTick` executed earlier in the iteration.

Any document or system that claims the current implementation polls input once per fixed simulation step is incorrect. Deterministic gameplay input and rollback need an explicit input-frame handoff at the start of each fixed step, with replay injecting stored frames rather than polling GLFW.

## Production completion criteria

1. Publish a renderer-owned immutable scene snapshot together with camera, overlay, selection, and editor-overlay state. Do not let the render thread dereference mutable ECS or `RenderScene` storage.
2. Move action sampling into the fixed-step input boundary and define input latching when zero, one, or multiple simulation steps occur in a display frame. Keep UI pointer capture on the presentation path.
3. Define lifecycle epochs for scene replacement, Play/Stop restore, and resize. Discard stale mailbox data, GPU-pick readbacks, and async asset work at an epoch change.
4. Add deterministic loop tests: accumulator cap, command flush order, input latching, mailbox retention, minimized-window behavior, and render-thread shutdown/device-lost behavior.
