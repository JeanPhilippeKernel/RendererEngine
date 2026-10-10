# ZEngine — Bindless Texture Descriptors

**Status:** Implemented current path; texture-residency eviction remains future work.
**Depends on:** Vulkan descriptor-indexing support when available, shader reflection,
and the device/resource lifetime path.

## Current contract

ZEngine owns one global texture-handle pool in `VulkanDevice`. Its requested upper
capacity is `MaxGlobalTexture = 8192`, clamped during device initialization to the
device's `maxPerStageDescriptorUpdateAfterBindSampledImages - 1` limit. The effective
capacity is device-dependent; 8192 is not a portability guarantee.

Reserved shader set 1 currently contains:

| Binding | Resource |
|---|---|
| 0 | `TextureArray` of sampled images |
| 1 | `LinearWrapSampler` |
| 2 | `LinearClampSampler` |

There is one reserved descriptor set for each configured swapchain buffered frame. The
same texture handle index is written into each set. Descriptor-indexing flags and the
update-after-bind descriptor-pool flag are enabled only when
`PhysicalDeviceSupportSampledImageBindless` is available.

Any shader that dynamically indexes `TextureArray` must use
`GL_EXT_nonuniform_qualifier` and `nonuniformEXT(index)`. This is already the
contract in `Resources/Shaders/zui_draw.frag`; new shader paths must preserve it.

## Update and lifetime path

`VulkanDevice::FlushBindlessTextureUpdates()` drains pending texture handles and
batches its normal descriptor writes into one `vkUpdateDescriptorSets` call. The
one-frame fallback path still writes fallback descriptors individually before requeuing
the real update. It also emits one 75%-capacity watermark warning per process.

Texture destruction is timeline-deferred through the device's resource-lifetime path.
A valid handle index does not prove that a texture is ready for a particular frame;
callers must follow the existing fallback/update flow.

## Open work

Geometry streaming is present, but it is not texture streaming. A production texture
residency design must add measured memory accounting, cancellation, reference/visibility
policy, protected engine/UI slots, eviction, and fallback behavior without invalidating
descriptors referenced by in-flight work. It should be planned with
[asset streaming](../future-plan/next-year-plans/asset-streaming.md) rather than assumed to be a
prerequisite that is already solved.
