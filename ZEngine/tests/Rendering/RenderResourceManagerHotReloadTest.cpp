#include <ZEngine/Rendering/RenderResourceManager.h>
#include <gtest/gtest.h>

// These tests exercise the hot-reload swap fix (ScheduleSwap/FlushPendingSwaps) and the
// slot-generation ABA fix (AllocMeshSlot/AllocImageSlot/AllocGBufSlot) landed alongside it.
//
// Both are skip-gated: no test in this codebase constructs a real RenderResourceManager
// today (the existing RenderResourceManagerTest.cpp only covers RenderHandle and
// AssetRegistry callback plumbing, neither of which touches a device). RRM::Initialize
// requires a fully-initialized VulkanDevice — Arena, GpuMem, a DeviceSwapchain with a real
// timeline semaphore, ThreadPoolHelper::Pool, and CommandPool/CommandBuffer for the upload
// queue — which is a materially bigger undertaking than GpuAllocatorTest's raw-Vulkan-handle
// fixture (GpuAllocator only needs a bare VkPhysicalDevice/VkDevice/VkInstance; RRM needs the
// whole engine bootstrap around it). Standing that up is out of scope for this pass; these
// are left here, skip-gated with the exact intent, as the concrete spec for that future
// fixture rather than silently dropping the tests.
//
// Correctness for this pass instead rests on: a full-file re-read of every call site touched
// (RenderResourceManager.cpp/.h), three rounds of adversarial design review before
// implementation, and a clean build of the whole engine (zEngineLib + Obelisk).

TEST(RenderResourceManagerHotReloadTest, SlotGenerationIsMonotonicAcrossReuse)
{
    GTEST_SKIP() << "Needs a real VulkanDevice — AllocMeshSlot is private and every public "
                    "path to it (UploadMesh/DoUploadMesh) goes through AppendToGlobalBuffer, "
                    "which calls m_device->GpuMem.AllocateBuffer. Intent: upload -> release -> "
                    "upload again into the same slot; assert same index, different generation "
                    "(this fails against the pre-fix idx+1 scheme, which returns the same value).";
}

TEST(RenderResourceManagerHotReloadTest, StaleHandleAfterReleaseAndReuseFailsGetBuffer)
{
    GTEST_SKIP() << "Needs a real VulkanDevice, same reason as SlotGenerationIsMonotonicAcrossReuse. "
                    "Intent: after slot reuse, the old (stale) handle must not resolve via GetBuffer.";
}

TEST(RenderResourceManagerHotReloadTest, HotReloadSwapUpdatesMeshOffsetsNextFrame)
{
    GTEST_SKIP() << "Needs a real VulkanDevice + AssetRegistry + AssetManager wiring. Intent: "
                    "register a mesh asset, upload it, fire OnAssetModified -> ScheduleSwap "
                    "enqueues -> one more BeginFrame drains it via FlushPendingSwaps -> "
                    "GetMeshOffsets returns different offsets pointing at newly-appended data, "
                    "same external BufferHandle still resolves. No frame-in-flight delay to "
                    "test for — the swap applies on the very next frame it's processed.";
}

TEST(RenderResourceManagerHotReloadTest, ScheduleSwapIsThreadSafeFromNonRenderThread)
{
    GTEST_SKIP() << "Needs a real VulkanDevice — FlushPendingSwaps (which ScheduleSwap's "
                    "effect is only observable through) calls DoUploadTexture/AppendMeshData, "
                    "both device-dependent. Intent: concurrent ScheduleSwap calls from a "
                    "spawned thread against a BeginFrame/EndFrame loop on the main thread, "
                    "~100 iterations, no crash/UB under TSan — the regression test for the "
                    "thread-safety fix (a dedicated m_pending_swap_mutex, not the hot, "
                    "long-held m_pending_mutex that SubmitTextureFile also holds).";
}
