# ZEngine — Bindless Descriptor Architecture

**Status:** Decided — Phase 1 implemented, Phase 2 deferred to streaming milestone
**Closes:** Issue #665
**Depends on:** Current bindless texture pipeline (PR #751)
**Blocks:** Virtual geometry streaming (#622) — Phase 2 of this doc is a prerequisite

---

## 1. Context

The engine uses a single global bindless `TextureArray` (set=1, binding=0) shared by all
consumers: scene PBR materials, the ZUI font atlas, UI panels, and the fallback texture.

Issue #665 was filed when `MaxGlobalTexture = 1024`. A code audit conducted before this
design was finalised found the field is now `8192`, hardware-clamped at device init to
`maxPerStageDescriptorUpdateAfterBindSampledImages - 1` (typically 500K+ on modern GPUs).
The original exhaustion concern is no longer valid at the current project scale.

---

## 2. Architecture Decision

### Rejected options

**Option A — Per-scene descriptor sets**: switching sets per draw call kills the benefit of
bindless. Rejected.

**Option C — Separate pools per consumer type**: splitting scene vs. UI into separate
descriptor sets (set=1 vs. set=2) adds shader complexity and multiple update paths for no
real gain at 8192 slots. The only scenario where independent eviction policies per consumer
matter is during streaming — deferred to Phase 2.

### Chosen direction

**Phase 1 (this milestone):** Keep the single flat `TextureArray[]` pool at `MaxGlobalTexture
= 8192` with dynamic hardware clamping. Fix three gaps in the current implementation:

1. Missing `nonuniformEXT` qualifier in `zui_draw.frag` — correctness bug on hardware with
   divergent warp indexing (the extension is `require`d but the index is not wrapped).
2. Descriptor writes batched per frame — currently one `vkUpdateDescriptorSets` call per
   dequeued texture; accumulate all writes across the drain loop and emit one call.
3. Watermark logging — `HandleManager::Size()` exists but nothing reads it; warn at 75% of
   `MaxGlobalTexture` so slot exhaustion is caught before it silently drops textures.

**Phase 2 (streaming milestone, #622):** When virtual geometry streaming lands, the texture
side needs matching LRU eviction — the flat pool becomes a fixed-capacity heap with
reference-counted slots and an eviction policy. This is Option B from the issue. Per-consumer
pool separation (Option C) may also become relevant at that point if UI/thumbnail slots need
to be protected from eviction by scene textures. Both are deferred until #622 defines its
VRAM budget model.

---

## 3. Current descriptor layout (Phase 1, unchanged)

| Set | Binding | Count | Type | Bindless flags |
|-----|---------|-------|------|----------------|
| 1 | 0 | MaxGlobalTexture (8192) | `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE` | `UPDATE_AFTER_BIND` + `PARTIALLY_BOUND` + `UPDATE_UNUSED_WHILE_PENDING` |
| 1 | 1 | 1 | `VK_DESCRIPTOR_TYPE_SAMPLER` (LinearWrap) | none |
| 1 | 2 | 1 | `VK_DESCRIPTOR_TYPE_SAMPLER` (LinearClamp) | none |

One `VkDescriptorSet` per swapchain frame (3 total). All frames see the same slot index for
a given texture because `dstArrayElement = handle.Index` for all writes.

---

## 4. Shader access contract

Every shader that indexes `TextureArray[]` with a value that may diverge across a warp must
wrap the index in `nonuniformEXT()`:

```glsl
#extension GL_EXT_nonuniform_qualifier : require

// Correct — both shaders must follow this pattern
texture(sampler2D(TextureArray[nonuniformEXT(texId)], LinearWrapSampler), uv)
```

`nonuniformEXT` is not optional when `GL_EXT_nonuniform_qualifier` is required — omitting it
is a spec violation that produces silently wrong results on non-NVIDIA hardware (where the
implementation does not implicitly broadcast non-uniform indices).

---

## 5. Descriptor write path (Phase 1, batched)

All pending textures are accumulated into a single `vkUpdateDescriptorSets` call per frame
inside `DeviceSwapchain::Present()`. The drain loop in Present() builds the full write list
across all dequeued handles before calling the API once:

```
for each dequeued handle:
    for each (DstSet, Binding) in BindlessTextureSlotRequests:
        push VkWriteDescriptorSet { dstArrayElement = handle.Index, ... }
vkUpdateDescriptorSets(all_writes)   // one call covering all handles × all sets
```

Previously one `vkUpdateDescriptorSets` was called per handle inside the inner loop — N calls
for N textures arriving in the same frame (e.g. after a scene load).

---

## 6. Watermark policy

A `ZENGINE_CORE_WARN` is emitted in `DeviceSwapchain::Present()` when
`GlobalTextures.Size() > MaxGlobalTexture * 3 / 4`. This fires at most once — a static
local `bool` gate prevents spam on subsequent frames. The warning includes the live count and
capacity so it's actionable without a profiler.

---

## 7. Phase 2 design notes (streaming era)

When #622 lands, the texture side of the pool needs:

- **Slot reference counting**: each draw command holds a ref on the texture slots it uses;
  slots with zero refs are eviction candidates.
- **LRU eviction**: on `CreateTexture` when the pool is full, evict the least-recently-used
  zero-ref slot rather than returning a null handle silently.
- **Per-consumer reservation**: reserve a fixed block (e.g. 64 slots) for UI/font/engine
  textures that must never be evicted, separate from the scene budget. This is Option C from
  #665, deferred here.
- **`GeometryPool` alignment**: the geometry streaming pool's page-eviction policy should
  mirror the texture eviction policy so both are driven by the same visibility system.
