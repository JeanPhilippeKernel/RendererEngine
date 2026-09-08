// Route STBI allocation through the per-worker TLSFSlab when available so
// stbi_load pixel buffers stay on the slab rather than the system heap.
// Falls back to malloc/free/realloc on the main thread (slab = nullptr).
#include <ZEngine/Core/Memory/TLSFSlab.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <cstdlib>
#define STBI_MALLOC(sz)        (ZEngine::Helpers::GetWorkerSlab() ? ZEngine::Helpers::GetWorkerSlab()->Alloc(sz) : std::malloc(sz))
#define STBI_REALLOC(p, newsz) (ZEngine::Helpers::GetWorkerSlab() ? ZEngine::Helpers::GetWorkerSlab()->Realloc(p, newsz) : std::realloc(p, newsz))
#define STBI_FREE(p)                                    \
    do                                                  \
    {                                                   \
        if (ZEngine::Helpers::GetWorkerSlab())          \
            ZEngine::Helpers::GetWorkerSlab()->Free(p); \
        else                                            \
            std::free(p);                               \
    } while (0)
#define STB_IMAGE_IMPLEMENTATION
#ifdef __GNUC__
#define STBI_NO_SIMD
#endif
#include <stb/stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Buffers/Bitmap.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/ZEngineDef.h>
#include <stb/deprecated/stb_image_resize.h>
#include <stb/stb_image_write.h>
#include <cstring>
#include <filesystem>
#include <limits>
#include <set>

using namespace ZEngine::Core::Memory;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::Hardwares;
using namespace ZEngine::Importers;
using namespace ZEngine::Managers;
using namespace ZEngine::Helpers;

namespace ZEngine::Rendering
{

    void RenderResourceManager::Initialize(VulkanDevice* device, Core::VFS::AssetRegistry* registry)
    {
        ZENGINE_VALIDATE_ASSERT(device != nullptr, "RenderResourceManager::Initialize: device must not be null")
        ZENGINE_VALIDATE_ASSERT(registry != nullptr, "RenderResourceManager::Initialize: registry must not be null")

        m_device   = device;
        m_registry = registry;

        InitUploadPool();
        InitGlobalBuffers();
        InitTextureTimelines();
        InitUploadSlabs(static_cast<uint32_t>(Helpers::ThreadPoolHelper::Pool->WorkerCount));
        Helpers::ThreadPoolHelper::Pool->InitClosureSlab(m_device->Arena, ZKilo(512));

        registry->SetOnReadyCallback(this, [](void* ctx, const uuids::uuid& uuid, AssetHandle handle) {
            auto*              rrm = static_cast<RenderResourceManager*>(ctx);

            const AssetRecord* rec = rrm->m_registry->FindByUUID(uuid);
            if (!rec || rec->Type != AssetType::MESH)
                return; // textures are ingested directly by AssetManager::IngestTexture, not via this path

            // Deduplicate: hold both locks together so two concurrent callbacks
            // for the same UUID can't both pass the check before either pushes.
            std::lock_guard map_lock(rrm->m_uuid_map_mutex);
            std::lock_guard pend_lock(rrm->m_pending_mutex);

            for (uint32_t i = 0; i < rrm->m_uuid_to_buffer_count; ++i)
                if (rrm->m_uuid_to_buffer[i].UUID == uuid)
                    return;
            for (uint32_t i = 0; i < rrm->m_pending_count; ++i)
                if (rrm->m_pending[i].UUID == uuid)
                    return;

            if (rrm->m_pending_count >= MAX_PENDING)
            {
                ZENGINE_CORE_WARN("[RRM] Pending upload queue full — dropping asset")
                return;
            }

            rrm->m_pending[rrm->m_pending_count++] = {handle, uuid};
        });

        registry->SetOnStaleCallback(this, [](void* ctx, const uuids::uuid& uuid) {
            auto*           rrm = static_cast<RenderResourceManager*>(ctx);

            // Look up current GPU handle for this UUID and schedule a swap. Mesh/buffer only —
            // texture hot-reload is triggered via TextureImporter/ImportCoordinator instead.
            std::lock_guard lock(rrm->m_uuid_map_mutex);
            for (uint32_t i = 0; i < rrm->m_uuid_to_buffer_count; ++i)
            {
                if (rrm->m_uuid_to_buffer[i].UUID == uuid)
                {
                    AssetHandle new_asset = 0;
                    {
                        const AssetRecord* rec = rrm->m_registry->FindByUUID(uuid);
                        if (rec)
                            new_asset = rec->SlotHandle;
                    }
                    rrm->ScheduleSwap(rrm->m_uuid_to_buffer[i].Handle, new_asset);
                    return;
                }
            }
        });

        registry->SetOnRemovedCallback(this, [](void* ctx, const uuids::uuid& uuid, AssetType type) {
            if (type != AssetType::TEXTURE)
                return;
            static_cast<RenderResourceManager*>(ctx)->ReleaseTexture(uuid);
        });
    }

    void RenderResourceManager::InitUploadPool()
    {
        // Pre-signaled so the first Wait() before a submit returns immediately.
        uint32_t frame_count = m_device->SwapchainPtr->BufferredFrameCount;
        m_sync_upload_fence  = ZPushStructCtorArgs(m_device->Arena, Rendering::Primitives::Fence, m_device, true);

        // LastSignal == 0 means "never used yet" for a given frame index.
        m_batch_timeline     = ZPushStructCtorArgs(m_device->Arena, Rendering::Primitives::Semaphore, m_device, true);
        m_batch_frames.init(m_device->Arena, frame_count, frame_count);
        for (uint32_t i = 0; i < frame_count; ++i)
            m_batch_frames[i] = BatchFrameState{};

        // RenderThread-only, single thread slot — cycles through BufferedFrameCount distinct
        // command buffers instead of resetting and resubmitting the same one every call (see
        // issue #764 follow-up: reusing a single command buffer across many upload cycles was
        // suspected as a factor in an otherwise-unexplained GPU stall).
        m_upload_cmd_mgr = ZPushStructCtor(m_device->Arena, CommandBufferManager);
        m_upload_cmd_mgr->Initialize(m_device, m_device->SwapchainPtr->BufferredFrameCount, 1);

        m_async_uploads.Initialize(m_device);
    }

    void RenderResourceManager::Shutdown()
    {
        if (!m_device)
            return;

        m_device->QueueWaitAll();
        ShutdownTextureTimelines();

        // Shut down per-worker upload slabs. Clear the worker init callback first so
        // any worker that wakes after this does not call SetWorkerSlab on a dead slab.
        if (Helpers::ThreadPoolHelper::Pool)
            Helpers::ThreadPoolHelper::Pool->RegisterWorkerInit(nullptr, nullptr);
        for (uint32_t i = 0; i < m_upload_slab_count; ++i)
            m_upload_slabs[i].Shutdown();
        m_upload_slab_count = 0;

        // Arena-allocated objects have no automatic destructor — explicit calls are required.
        if (m_upload_cmd_mgr)
        {
            m_upload_cmd_mgr->Deinitialize();
            m_upload_cmd_mgr = nullptr;
        }
        m_async_uploads.Deinitialize();
        if (m_sync_upload_fence)
        {
            m_sync_upload_fence->~Fence();
            m_sync_upload_fence = nullptr;
        }
        if (m_batch_timeline)
        {
            m_batch_timeline->~Semaphore();
            m_batch_timeline = nullptr;
        }

        // Free all live buffer slots
        if (m_pool.VertexBuffer)
            m_device->GpuMem.FreeBuffer(m_pool.VertexBuffer);
        if (m_pool.IndexBuffer)
            m_device->GpuMem.FreeBuffer(m_pool.IndexBuffer);
        if (m_builtin_vertex_buf)
            m_device->GpuMem.FreeBuffer(m_builtin_vertex_buf);
        if (m_builtin_index_buf)
            m_device->GpuMem.FreeBuffer(m_builtin_index_buf);

        for (uint32_t i = 0; i < m_gbuf_slot_count; ++i)
        {
            if (m_gbuf_slots[i].Generation != 0 && m_gbuf_slots[i].Data)
                m_device->GpuMem.FreeBuffer(m_gbuf_slots[i].Data);
        }

        m_streaming_mgr.Deinitialize();
        m_device   = nullptr;
        m_registry = nullptr;
    }

    void RenderResourceManager::BeginFrame(uint32_t frame_index)
    {
        m_active_frame_index = static_cast<uint8_t>(frame_index);
        RetireBatchStagings();
        m_streaming_mgr.Tick(frame_index);
        if (m_streaming_mgr.IsCompactionRequested())
            RunCompaction();
        FlushPendingUploads(frame_index);
        FlushPendingSwaps(frame_index);
        FlushPendingTextureReloads();
        FlushPendingTextureReleases();
    }

    void RenderResourceManager::EndFrame()
    {
        if (m_batch_mode)
            EndBatchUpload();
    }

    void RenderResourceManager::FlushPendingSwaps(uint32_t frame_index)
    {
        uint32_t    count = 0;
        PendingSwap local[MAX_PENDING];
        {
            std::lock_guard lock(m_pending_swap_mutex);
            count = m_pending_swap_count;
            secure_memcpy(local, sizeof(local), m_pending_swaps, count * sizeof(PendingSwap));
            m_pending_swap_count = 0;
        }
        if (count == 0)
        {
            return;
        }

        // Idempotent — no-op if FlushPendingUploads already opened the batch this frame.
        EnsureBatchOpen(static_cast<uint8_t>(frame_index));

        for (uint32_t i = 0; i < count; ++i)
        {
            const PendingSwap& s = local[i];
            if (s.OldBuffer.Generation & GBUF_GEN_TAG)
            {
                // No AssetHandle-driven re-upload path exists for generic buffers today —
                // every live ScheduleSwap(BufferHandle,...) call carries a mesh handle.
                ZENGINE_LOG_RENDER_WARN("[RRM] Hot-reload swap for generic buffers is not supported — skipping")
            }
            else
            {
                if (s.OldBuffer.Index >= m_mesh_slot_count || m_mesh_slots[s.OldBuffer.Index].Generation != s.OldBuffer.Generation)
                {
                    continue; // stale handle — asset was released before the swap could apply
                }
                MeshSlot new_data = AppendMeshData(s.NewAsset, frame_index);
                if (new_data.VtxCount == 0)
                {
                    ZENGINE_LOG_RENDER_ERR("[RRM] Hot-reload swap failed to re-upload mesh data — old data left in place")
                    continue;
                }
                // Free the old region before repointing the slot — pool is no longer append-only.
                auto& slot = m_mesh_slots[s.OldBuffer.Index];
                if (slot.Data.Region.VtxByteSize > 0)
                    m_pool.Free(slot.Data.Region);
                slot.Data            = new_data;
                slot.Data.State      = StreamingState::Resident;
                slot.Data.Referenced = false;
            }
        }
    }

    // Contexts + C-style record callbacks for RecordAndSubmit (no std::function — matches the
    // rest of the engine's zero-heap-alloc convention for hot-path callbacks, see ThreadPool's
    // TaskFn).
    struct GlobalBufferCopyCtx
    {
        VkBuffer     SrcBuffer;
        VkBuffer     DstBuffer;
        VkDeviceSize DstOffset;
        size_t       ByteSize;
    };

    static void RecordGlobalBufferCopy(VkCommandBuffer cmd, void* ctx_ptr)
    {
        auto*        ctx = static_cast<GlobalBufferCopyCtx*>(ctx_ptr);
        VkBufferCopy region{.srcOffset = 0, .dstOffset = ctx->DstOffset, .size = ctx->ByteSize};
        vkCmdCopyBuffer(cmd, ctx->SrcBuffer, ctx->DstBuffer, 1, &region);

        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer              = ctx->DstBuffer;
        barrier.offset              = ctx->DstOffset;
        barrier.size                = ctx->ByteSize;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
    }

    struct RingCopyCtx
    {
        VkBuffer     SrcBuffer;
        VkBuffer     DstBuffer;
        uint32_t     RingOffset;
        VkDeviceSize DstOffset;
        size_t       ByteSize;
    };

    static void RecordRingCopy(VkCommandBuffer cmd, void* ctx_ptr)
    {
        auto*        ctx = static_cast<RingCopyCtx*>(ctx_ptr);
        VkBufferCopy region{.srcOffset = ctx->RingOffset, .dstOffset = ctx->DstOffset, .size = ctx->ByteSize};
        vkCmdCopyBuffer(cmd, ctx->SrcBuffer, ctx->DstBuffer, 1, &region);

        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer              = ctx->DstBuffer;
        barrier.offset              = ctx->DstOffset;
        barrier.size                = ctx->ByteSize;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
    }

    struct StagingCopyCtx
    {
        VkBuffer     SrcBuffer;
        VkBuffer     DstBuffer;
        VkDeviceSize DstOffset;
        size_t       ByteSize;
    };

    static void RecordStagingCopy(VkCommandBuffer cmd, void* ctx_ptr)
    {
        auto*        ctx = static_cast<StagingCopyCtx*>(ctx_ptr);
        VkBufferCopy region{.srcOffset = 0, .dstOffset = ctx->DstOffset, .size = ctx->ByteSize};
        vkCmdCopyBuffer(cmd, ctx->SrcBuffer, ctx->DstBuffer, 1, &region);

        // Same fallback destinations as RecordRingCopy (generic SSBOs, ZUI vertex/index
        // buffers), so the same access/stage transition. Previously relied on the caller
        // fully blocking on a fence before returning — harmless then, but once this runs
        // as part of a deferred batch, the cross-submission consumer needs an explicit
        // barrier, not just ordering.
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer              = ctx->DstBuffer;
        barrier.offset              = ctx->DstOffset;
        barrier.size                = ctx->ByteSize;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
    }

    static void RecordAndSubmit(CommandBuffer* cmd, Rendering::Primitives::Fence* fence, VkQueue queue, void (*record_fn)(VkCommandBuffer, void*), void* record_ctx)
    {
        cmd->ResetState();
        vkResetCommandBuffer(cmd->GetHandle(), 0);
        cmd->Begin();
        record_fn(cmd->GetHandle(), record_ctx);
        cmd->End();

        VkCommandBuffer raw = cmd->GetHandle();
        VkSubmitInfo    submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &raw;

        // Wait before reset — fence may be in-flight under MoltenVK async completion.
        fence->Wait(UINT64_MAX);
        fence->Reset();
        vkQueueSubmit(queue, 1, &submit, fence->GetHandle());
        fence->Wait(UINT64_MAX);
        cmd->ResetState();
    }

    // Derive a per-axis geometry streaming budget from the device's device-local VRAM.
    // Uses 15% of the largest device-local heap, clamped to [GLOBAL_VTX_MIN, GLOBAL_VTX_CAPACITY].
    // The same value is used for both vtx and idx axes (split evenly from the total budget).
    static VkDeviceSize DeriveGeometryBudget(const VkPhysicalDeviceMemoryProperties& props)
    {
        VkDeviceSize largest_device_local = 0;
        for (uint32_t i = 0; i < props.memoryHeapCount; ++i)
        {
            if ((props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) && props.memoryHeaps[i].size > largest_device_local)
                largest_device_local = props.memoryHeaps[i].size;
        }

        if (largest_device_local == 0)
            return RenderResourceManager::GLOBAL_VTX_CAPACITY;

        VkDeviceSize half_budget = largest_device_local * 15 / 100 / 2;
        half_budget              = std::max(half_budget, RenderResourceManager::GLOBAL_VTX_MIN);
        half_budget              = std::min(half_budget, RenderResourceManager::GLOBAL_VTX_CAPACITY);
        return half_budget;
    }

    void RenderResourceManager::InitGlobalBuffers()
    {
        VkDeviceSize vtx_capacity = 0;
        VkDeviceSize idx_capacity = 0;

        if (m_device->GeometryStreamingBudget != 0)
        {
            // Explicit project.json override: split evenly, clamp to [min, max].
            VkDeviceSize half = m_device->GeometryStreamingBudget / 2;
            half              = std::max(half, GLOBAL_VTX_MIN);
            half              = std::min(half, GLOBAL_VTX_CAPACITY);
            vtx_capacity      = half;
            idx_capacity      = half;
            ZENGINE_LOG_RENDER_INFO("[RRM] Geometry pool: project override {} MB vtx + {} MB idx", vtx_capacity >> 20, idx_capacity >> 20)
        }
        else
        {
            vtx_capacity = DeriveGeometryBudget(m_device->PhysicalDeviceMemoryProperties);
            idx_capacity = vtx_capacity;
            ZENGINE_LOG_RENDER_INFO("[RRM] Geometry pool: auto-detected {} MB vtx + {} MB idx (15% of largest device-local heap)", vtx_capacity >> 20, idx_capacity >> 20)
        }

        m_pool.VertexBuffer = m_device->GpuMem.AllocateBuffer(vtx_capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, GpuMemoryDomain::DeviceGeometry, "RRM::GlobalVertexBuffer");
        m_pool.IndexBuffer  = m_device->GpuMem.AllocateBuffer(idx_capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, GpuMemoryDomain::DeviceGeometry, "RRM::GlobalIndexBuffer");
        ZENGINE_VALIDATE_ASSERT(m_pool.VertexBuffer, "RRM: global vertex buffer allocation failed")
        ZENGINE_VALIDATE_ASSERT(m_pool.IndexBuffer, "RRM: global index buffer allocation failed")
        m_pool.Initialize(m_device->Arena, vtx_capacity, idx_capacity, MAX_BUFFERS * 2);

        m_builtin_vertex_buf = m_device->GpuMem.AllocateBuffer(BUILTIN_VTX_CAPACITY, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, GpuMemoryDomain::DeviceGeometry, "RRM::BuiltinVertexBuffer");
        m_builtin_index_buf  = m_device->GpuMem.AllocateBuffer(BUILTIN_IDX_CAPACITY, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, GpuMemoryDomain::DeviceGeometry, "RRM::BuiltinIndexBuffer");
        m_builtin_vtx_cursor = 0;
        m_builtin_idx_cursor = 0;
        ZENGINE_VALIDATE_ASSERT(m_builtin_vertex_buf, "RRM: builtin vertex buffer allocation failed")
        ZENGINE_VALIDATE_ASSERT(m_builtin_index_buf, "RRM: builtin index buffer allocation failed")

        m_streaming_mgr.Initialize(m_device, this);
    }

    void RenderResourceManager::RegisterBuiltinGeometry(const void* vtx_data, size_t vtx_bytes, const uint32_t* idx_data, uint32_t idx_count, uint32_t& out_vtx_offset, uint32_t& out_idx_offset)
    {
        const size_t idx_bytes = idx_count * sizeof(uint32_t);
        ZENGINE_VALIDATE_ASSERT(m_builtin_vtx_cursor + vtx_bytes <= BUILTIN_VTX_CAPACITY, "RRM::RegisterBuiltinGeometry: builtin vertex buffer out of space")
        ZENGINE_VALIDATE_ASSERT(m_builtin_idx_cursor + idx_bytes <= BUILTIN_IDX_CAPACITY, "RRM::RegisterBuiltinGeometry: builtin index buffer out of space")

        // Called pre-render-thread (single-threaded init) — the batch this opens stays
        // open until the first real frame's RRM::EndFrame, where any mesh uploads from
        // that frame join it too. Builtin data goes into the pinned builtin buffers, not
        // the streaming global buffers, so a ResetGeometryBuffers never corrupts it.
        EnsureBatchOpen(static_cast<uint8_t>(m_active_frame_index));
        AppendToGlobalBuffer(m_builtin_vertex_buf, vtx_data, vtx_bytes, m_builtin_vtx_cursor, m_active_frame_index);
        AppendToGlobalBuffer(m_builtin_index_buf, idx_data, idx_bytes, m_builtin_idx_cursor, m_active_frame_index);

        out_vtx_offset        = static_cast<uint32_t>(m_builtin_vtx_cursor / (8 * sizeof(float)));
        out_idx_offset        = static_cast<uint32_t>(m_builtin_idx_cursor / sizeof(uint32_t));
        m_builtin_vtx_cursor += vtx_bytes;
        m_builtin_idx_cursor += idx_bytes;
    }

    void RenderResourceManager::ResetGeometryBuffers()
    {
        m_pending_reset.store(true, std::memory_order_release);
    }

    void RenderResourceManager::ResetGeometryBuffersInternal()
    {
        m_pool.Reset();
        for (uint32_t i = 0; i < m_mesh_slot_count; ++i)
            m_mesh_slots[i] = {};
        m_mesh_slot_count = 0;

        std::lock_guard lock(m_uuid_map_mutex);
        for (uint32_t i = 0; i < m_uuid_to_buffer_count; ++i)
            m_uuid_to_buffer[i] = {};
        m_uuid_to_buffer_count = 0;
    }

    void RenderResourceManager::RunCompaction()
    {
        // Snapshot all Resident slots with their AssetHandles before touching the pool.
        // AssetManager::Meshes is always CPU-resident today (no CPU-side eviction), so
        // GetAsset<AssetMesh> inside AppendMeshData is guaranteed to succeed for every
        // Resident slot. If CPU streaming is added later, switch to a GPU self-copy.
        struct CompactEntry
        {
            uint32_t              SlotIdx;
            Managers::AssetHandle Asset;
        };
        CompactEntry entries[MAX_UUID_MAP];
        uint32_t     entry_count = 0;

        {
            std::lock_guard lock(m_uuid_map_mutex);
            for (uint32_t i = 0; i < m_uuid_to_buffer_count; ++i)
            {
                const UUIDBufferPair& pair = m_uuid_to_buffer[i];
                if (!pair.Handle.IsValid())
                    continue;
                uint32_t slot_idx = pair.Handle.Index;
                if (slot_idx >= m_mesh_slot_count)
                    continue;
                const auto& slot = m_mesh_slots[slot_idx];
                if (slot.Generation != pair.Handle.Generation)
                    continue;
                if (slot.Data.State != StreamingState::Resident)
                    continue;
                const AssetRecord* rec = m_registry->FindByUUID(pair.UUID);
                if (!rec)
                    continue;
                entries[entry_count++] = {slot_idx, rec->SlotHandle};
            }
        }

        // Reset the pool: zero cursors, clear free lists. VkBuffer content is irrelevant —
        // every Resident byte will be re-uploaded into the new packed layout below.
        m_pool.Reset();

        // Clear stale regions so no slot holds a dangling offset after the reset.
        for (uint32_t i = 0; i < entry_count; ++i)
            m_mesh_slots[entries[i].SlotIdx].Data.Region = {};

        // Re-upload each mesh into a fresh packed region.  EnsureBatchOpen opens the
        // batch if nothing else has yet — it will be closed by EndFrame as normal.
        EnsureBatchOpen(m_active_frame_index);
        for (uint32_t i = 0; i < entry_count; ++i)
        {
            MeshSlot new_data = AppendMeshData(entries[i].Asset, m_active_frame_index);
            if (new_data.VtxCount > 0)
            {
                m_mesh_slots[entries[i].SlotIdx].Data.Region   = new_data.Region;
                m_mesh_slots[entries[i].SlotIdx].Data.VtxCount = new_data.VtxCount;
                m_mesh_slots[entries[i].SlotIdx].Data.IdxCount = new_data.IdxCount;
            }
        }

        m_streaming_mgr.ClearCompactionRequest();
        ZENGINE_LOG_RENDER_INFO("[RRM] Geometry compaction complete — {} meshes re-packed, fragmentation now {:.1f}%", entry_count, m_pool.FragmentationRatio() * 100.f)
    }

    void RenderResourceManager::RetireBatchStagings()
    {
        uint64_t completed = 0;
        vkGetSemaphoreCounterValue(m_device->LogicalDevice, m_batch_timeline->GetHandle(), &completed);
        for (uint32_t i = 0; i < m_batch_frames.size(); ++i)
        {
            BatchFrameState& frame = m_batch_frames[i];
            if (frame.LastSignal == 0 || frame.LastSignal > completed || frame.StagingCount == 0)
                continue;
            for (uint32_t j = 0; j < frame.StagingCount; ++j)
                m_device->GpuMem.FreeBuffer(frame.StagingBuffers[j]);
            frame.StagingCount = 0;
        }
    }

    void RenderResourceManager::EnsureBatchOpen(uint8_t frame_index)
    {
        if (!m_batch_mode)
            BeginBatchUpload(frame_index);
    }

    void RenderResourceManager::BeginBatchUpload(uint8_t frame_index)
    {
        m_batch_frame_index    = frame_index;
        // Instant buffer, not the regular pool's slot 0 — that slot is shared by the two
        // remaining synchronous callers (UpdateBuffer's ring path, UploadFontAtlas), which
        // submit and block before returning; this buffer's submission is deferred instead.
        m_batch_cmd            = m_upload_cmd_mgr->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE, frame_index, 0, 0, false);

        BatchFrameState& frame = m_batch_frames[frame_index];

        // Wait for this frame index's buffer to be free of its last use before recording
        // into it again. Almost always a no-op — BufferedFrameCount frames have already
        // passed by the time this frame index comes back around.
        if (frame.LastSignal != 0)
            m_batch_timeline->Wait(frame.LastSignal, UINT64_MAX);

        // Guaranteed-safe fallback free, in case RetireBatchStagings' poll hasn't caught up
        // yet — the wait above proves this frame index's previous batch is done either way.
        for (uint32_t i = 0; i < frame.StagingCount; ++i)
            m_device->GpuMem.FreeBuffer(frame.StagingBuffers[i]);
        frame.StagingCount = 0;

        m_batch_cmd->ResetState();
        vkResetCommandBuffer(m_batch_cmd->GetHandle(), 0);
        m_batch_cmd->Begin();
        m_batch_mode = true;
    }

    void RenderResourceManager::EndBatchUpload()
    {
        m_batch_cmd->End();

        // Submission is deferred to SubmitAsyncUploads (AppRenderPipeline::EndFrame) instead
        // of submitted-and-blocked-on here, so a mesh drop never stalls the render thread.
        // Signals m_batch_timeline — a dedicated semaphore with exactly one writer (this
        // function) — rather than DeviceSwapchain::RenderTimeline, which Present() also
        // drives independently; sharing it produced a timeline value Intel's Windows driver
        // treats as non-monotonic.
        uint64_t signal_value                          = ++m_batch_next_value;
        m_batch_frames[m_batch_frame_index].LastSignal = signal_value;

        Hardwares::AsyncUploadJob job;
        job.Buffer      = m_batch_cmd;
        job.Timeline    = m_batch_timeline;
        job.SignalValue = signal_value;
        // Stages that actually consume the global vertex/index buffers, matching
        // RecordGlobalBufferCopy's own barrier — so Present() waits at the right point.
        job.WaitFlag    = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        m_async_uploads.Enqueue(job);

        // Left in m_batch_frames[m_batch_frame_index] for RetireBatchStagings (or the next
        // BeginBatchUpload for this same frame index) to free once m_batch_timeline proves
        // this copy has completed.
        m_batch_mode = false;
        m_batch_cmd  = nullptr;
    }

    void RenderResourceManager::AppendToGlobalBuffer(BufferView& dst_buf, const void* data, size_t byte_size, VkDeviceSize byte_offset, uint32_t frame_index)
    {
        BufferView staging = m_device->GpuMem.AllocateBuffer(static_cast<VkDeviceSize>(byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, GpuMemoryDomain::HostStaging, "RRM::Staging");
        ZENGINE_VALIDATE_ASSERT(staging, "RRM::AppendToGlobalBuffer: staging alloc failed")
        ZENGINE_VALIDATE_ASSERT(vmaCopyMemoryToAllocation(m_device->GpuMem.Allocator, data, staging.Allocation, 0, byte_size) == VK_SUCCESS, "RRM::AppendToGlobalBuffer: staging copy failed")

        GlobalBufferCopyCtx ctx{staging.Handle, dst_buf.Handle, byte_offset, byte_size};

        // Every caller (FlushPendingUploads, FlushPendingSwaps, RegisterBuiltinGeometry)
        // calls EnsureBatchOpen first — there is no longer a synchronous fallback path.
        ZENGINE_VALIDATE_ASSERT(m_batch_mode, "RRM::AppendToGlobalBuffer: called without an open batch — call EnsureBatchOpen first")
        ZENGINE_VALIDATE_ASSERT(static_cast<uint8_t>(frame_index) == m_batch_frame_index, "RRM::AppendToGlobalBuffer: frame_index does not match the currently open batch")
        RecordGlobalBufferCopy(m_batch_cmd->GetHandle(), &ctx);
        BatchFrameState& frame = m_batch_frames[m_batch_frame_index];
        ZENGINE_VALIDATE_ASSERT(frame.StagingCount < MAX_PENDING * 2, "RRM::AppendToGlobalBuffer: batch staging overflow")
        frame.StagingBuffers[frame.StagingCount++] = staging;
    }

    RenderResourceManager::MeshSlot RenderResourceManager::AppendMeshData(AssetHandle asset, uint32_t frame_index)
    {
        AssetMesh* mesh = AssetManager::GetAsset<AssetMesh>(asset);
        if (!mesh || mesh->Vertices.empty())
        {
            return {};
        }

        static constexpr uint32_t FLOATS_PER_DRAW_VERTEX = 8;                                      // x,y,z, nx,ny,nz, u,v
        static constexpr uint32_t DRAW_VERTEX_BYTES      = FLOATS_PER_DRAW_VERTEX * sizeof(float); // 32

        // Every downstream offset assumes Vertices.size() is a whole number of DrawVertex
        // elements — enforced only by importer convention, so assert here rather than let
        // a misaligned cursor corrupt every subsequent mesh's GPU-side read offset.
        ZENGINE_VALIDATE_ASSERT(mesh->Vertices.size() % FLOATS_PER_DRAW_VERTEX == 0, "RRM::AppendMeshData: Vertices.size() is not a whole number of DrawVertex elements")

        size_t         vert_bytes = mesh->Vertices.size() * sizeof(float);
        size_t         idx_bytes  = mesh->Indices.size() * sizeof(uint32_t);
        GeometryRegion region;
        if (!m_pool.Allocate(static_cast<VkDeviceSize>(vert_bytes), static_cast<VkDeviceSize>(idx_bytes), region))
        {
            ZENGINE_LOG_RENDER_ERR("[RRM] AppendMeshData: geometry pool full")
            return {};
        }

        AppendToGlobalBuffer(m_pool.VertexBuffer, mesh->Vertices.data(), vert_bytes, region.VtxByteOffset, frame_index);
        AppendToGlobalBuffer(m_pool.IndexBuffer, mesh->Indices.data(), idx_bytes, region.IdxByteOffset, frame_index);

        uint32_t vtx_elem_offset = static_cast<uint32_t>(region.VtxByteOffset / DRAW_VERTEX_BYTES);
        uint32_t idx_elem_offset = static_cast<uint32_t>(region.IdxByteOffset / sizeof(uint32_t));
        uint32_t vtx_elem_count  = static_cast<uint32_t>(mesh->Vertices.size() / FLOATS_PER_DRAW_VERTEX);
        uint32_t idx_elem_count  = static_cast<uint32_t>(mesh->Indices.size());

        ZENGINE_LOG_RENDER_INFO("[RRM] Uploaded mesh: {} verts ({} bytes), {} indices ({} bytes) — vtx@{} idx@{}", vtx_elem_count, vert_bytes, idx_elem_count, idx_bytes, vtx_elem_offset, idx_elem_offset)

        return {region, vtx_elem_count, idx_elem_count};
    }

    BufferHandle RenderResourceManager::DoUploadMesh(AssetHandle asset, uint32_t frame_index)
    {
        MeshSlot data = AppendMeshData(asset, frame_index);
        if (data.VtxCount == 0)
        {
            return {};
        }

        uint32_t slot                      = AllocMeshSlot();
        m_mesh_slots[slot].Data            = data;
        m_mesh_slots[slot].Data.State      = StreamingState::Resident;
        m_mesh_slots[slot].Data.Referenced = false;
        m_mesh_slots[slot].Data.Pinned     = false;
        return {slot, m_mesh_slots[slot].Generation};
    }

    void RenderResourceManager::FlushPendingUploads(uint32_t frame_index)
    {
        // Compact geometry buffers if a scene reload was requested
        if (m_pending_reset.exchange(false, std::memory_order_acq_rel))
            ResetGeometryBuffersInternal();

        uint32_t      count = 0;
        PendingUpload local[MAX_PENDING];
        {
            std::lock_guard lock(m_pending_mutex);
            count = m_pending_count;
            secure_memcpy(local, sizeof(local), m_pending, count * sizeof(PendingUpload));
            m_pending_count = 0;
        }
        if (count == 0)
            return;

        // Joins this frame's deferred batch (opened here if nothing else has yet) — closed
        // once, by RRM::EndFrame, after everything else that might also join it this frame.
        EnsureBatchOpen(static_cast<uint8_t>(frame_index));
        for (uint32_t i = 0; i < count; ++i)
        {
            BufferHandle h = DoUploadMesh(local[i].Asset, frame_index);
            if (h.IsValid())
            {
                std::lock_guard lock(m_uuid_map_mutex);
                if (m_uuid_to_buffer_count < MAX_UUID_MAP)
                    m_uuid_to_buffer[m_uuid_to_buffer_count++] = {local[i].UUID, h};
            }
            else
            {
                ZENGINE_CORE_ERROR("[RRM] Mesh upload failed for asset handle {}", local[i].Asset)
            }
        }
    }

    void RenderResourceManager::UpdateBuffer(BufferView& dst, const void* data, size_t byte_size, uint32_t dst_offset)
    {
        if (!data || byte_size == 0 || !dst)
            return;

        VkMemoryPropertyFlags mem_flags = 0;
        vmaGetAllocationMemoryProperties(m_device->GpuMem.Allocator, dst.Allocation, &mem_flags);

        if (mem_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        {
            // BAR / HOST_VISIBLE — direct memcpy, no command buffer.
            ZENGINE_VALIDATE_ASSERT(vmaCopyMemoryToAllocation(m_device->GpuMem.Allocator, data, dst.Allocation, dst_offset, byte_size) == VK_SUCCESS, "RRM::UpdateBuffer: host-visible memcpy failed")
            if (!(mem_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                vmaFlushAllocation(m_device->GpuMem.Allocator, dst.Allocation, dst_offset, byte_size);
            return;
        }

        // DEVICE_LOCAL. Two sub-cases with different lifecycles:
        uint32_t ring_offset = 0;
        void*    ring_ptr    = m_device->GpuMem.Ring.Allocate(static_cast<uint32_t>(byte_size), 4, &ring_offset);

        if (ring_ptr)
        {
            // Ring path stays synchronous, deliberately not deferred: GpuAllocator::Ring's
            // retirement (Ring::Drain, driven by VulkanDevice::TickMemory) tracks every
            // chunk in one FIFO compared against RenderTimeline's completed value alone.
            // Stamping a chunk with m_batch_timeline's (future, not-yet-reached) signal
            // value instead would compare it against the wrong counter — the chunk could
            // be reclaimed before the deferred copy on m_batch_timeline ever executes.
            // Fixing that needs Ring to track more than one semaphore, which is out of
            // scope here — so this sub-case keeps the fence-blocking model, just repointed
            // at m_sync_upload_fence.
            secure_memmove(ring_ptr, byte_size, data, byte_size);

            VkQueue        gfx_queue  = m_device->GetQueue(QueueType::GRAPHIC_QUEUE).Handle;
            CommandBuffer* upload_cmd = m_upload_cmd_mgr->GetCommandBuffer(QueueType::GRAPHIC_QUEUE, m_active_frame_index, 0, 0, false);
            RingCopyCtx    ctx{m_device->GpuMem.Ring.Buffer, dst.Handle, ring_offset, dst_offset, byte_size};
            RecordAndSubmit(upload_cmd, m_sync_upload_fence, gfx_queue, RecordRingCopy, &ctx);

            m_device->GpuMem.Ring.Submit(ring_offset, static_cast<uint32_t>(byte_size), m_device->SwapchainPtr->RenderTimelineNextValue);
        }
        else
        {
            // Staging path joins this frame's deferred batch — no GpuAllocator::Ring
            // involvement, so no cross-semaphore retirement hazard.
            EnsureBatchOpen(m_active_frame_index);

            BufferView staging = m_device->CreateBuffer(static_cast<VkDeviceSize>(byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, GpuMemoryDomain::HostStaging);
            ZENGINE_VALIDATE_ASSERT(vmaCopyMemoryToAllocation(m_device->GpuMem.Allocator, data, staging.Allocation, 0, byte_size) == VK_SUCCESS, "RRM::UpdateBuffer: staging copy failed")

            StagingCopyCtx ctx{staging.Handle, dst.Handle, dst_offset, byte_size};
            RecordStagingCopy(m_batch_cmd->GetHandle(), &ctx);

            BatchFrameState& frame = m_batch_frames[m_batch_frame_index];
            ZENGINE_VALIDATE_ASSERT(frame.StagingCount < MAX_PENDING * 2, "RRM::UpdateBuffer: batch staging overflow")
            frame.StagingBuffers[frame.StagingCount++] = staging;
        }
    }

    void RenderResourceManager::ScheduleSwap(BufferHandle old_handle, AssetHandle new_asset)
    {
        if (!old_handle.IsValid())
        {
            return;
        }
        std::lock_guard lock(m_pending_swap_mutex);
        if (m_pending_swap_count >= MAX_PENDING)
        {
            ZENGINE_LOG_RENDER_WARN("[RRM] Pending swap queue full — dropping hot-reload swap")
            return;
        }
        PendingSwap& s = m_pending_swaps[m_pending_swap_count++];
        s.OldBuffer    = old_handle;
        s.NewAsset     = new_asset;
    }

    bool RenderResourceManager::GetMeshOffsets(BufferHandle handle, uint32_t& vtx_offset, uint32_t& idx_offset) const
    {
        if (!handle.IsValid() || handle.Index >= m_mesh_slot_count)
            return false;
        const auto& slot = m_mesh_slots[handle.Index];
        if (slot.Generation != handle.Generation)
            return false;
        vtx_offset = static_cast<uint32_t>(slot.Data.Region.VtxByteOffset / DRAW_VERTEX_BYTES);
        idx_offset = static_cast<uint32_t>(slot.Data.Region.IdxByteOffset / sizeof(uint32_t));
        return true;
    }

    bool RenderResourceManager::RequestMeshLoad(BufferHandle handle, const uuids::uuid& uuid)
    {
        const AssetRecord* rec = m_registry->FindByUUID(uuid);
        if (!rec || rec->SlotHandle == 0)
            return false;
        return m_streaming_mgr.RequestLoad({uuid, rec->SlotHandle, handle, 0});
    }

    bool RenderResourceManager::IsMeshResident(BufferHandle handle) const
    {
        if (!handle.IsValid() || handle.Index >= m_mesh_slot_count)
            return false;
        const auto& slot = m_mesh_slots[handle.Index];
        if (slot.Generation != handle.Generation)
            return false;
        return slot.Data.State == StreamingState::Resident;
    }

    void RenderResourceManager::MarkMeshReferenced(BufferHandle handle)
    {
        if (!handle.IsValid() || handle.Index >= m_mesh_slot_count)
            return;
        auto& slot = m_mesh_slots[handle.Index];
        if (slot.Generation == handle.Generation)
            slot.Data.Referenced = true;
    }

    BufferHandle RenderResourceManager::FindMeshBuffer(const uuids::uuid& uuid) const
    {
        // Render-thread only — m_uuid_to_buffer is written in FlushPendingUploads
        // which also runs on the render thread, so no mutex needed.
        for (uint32_t i = 0; i < m_uuid_to_buffer_count; ++i)
            if (m_uuid_to_buffer[i].UUID == uuid)
                return m_uuid_to_buffer[i].Handle;
        return {};
    }

    void RenderResourceManager::ReleaseMeshGeometry(const uuids::uuid& uuid)
    {
        std::lock_guard lock(m_uuid_map_mutex);

        // Find and invalidate the slot
        for (uint32_t i = 0; i < m_uuid_to_buffer_count; ++i)
        {
            if (m_uuid_to_buffer[i].UUID == uuid)
            {
                BufferHandle h = m_uuid_to_buffer[i].Handle;

                if (h.IsValid() && !(h.Generation & GBUF_GEN_TAG) && h.Index < m_mesh_slot_count)
                {
                    auto& slot = m_mesh_slots[h.Index];
                    if (slot.Data.Region.VtxByteSize > 0)
                        m_pool.Free(slot.Data.Region);
                    slot.Generation = 0;
                }

                // Remove from UUID map (swap with last entry)
                m_uuid_to_buffer[i] = m_uuid_to_buffer[--m_uuid_to_buffer_count];
                ZENGINE_CORE_INFO("[RRM] Released mesh geometry slot for UUID {}", uuids::to_string(uuid))
                return;
            }
        }
    }

    void RenderResourceManager::Release(BufferHandle handle)
    {
        if (!handle.IsValid())
            return;

        if (handle.Generation & GBUF_GEN_TAG)
        {
            // Generic device-local buffer — deferred-free the VmaAllocation.
            if (handle.Index >= m_gbuf_slot_count)
                return;
            auto& slot = m_gbuf_slots[handle.Index];
            if (slot.Generation != handle.Generation)
                return;
            DeferredFreeEntry e;
            e.EntryKind     = DeferredFreeEntry::Kind::Buffer;
            e.TimelineValue = m_device->SwapchainPtr->RenderTimelineNextValue;
            e.Data.Buffer   = slot.Data;
            m_device->DeferFree(e);
            slot.Data       = {};
            slot.Generation = 0;
        }
        else
        {
            // Mesh slot — packed global buffer is append-only; just invalidate the slot.
            if (handle.Index >= m_mesh_slot_count)
                return;
            m_mesh_slots[handle.Index].Generation = 0;
        }
    }

    const Rendering::Textures::Texture* RenderResourceManager::GetTexture(const Rendering::Textures::TextureHandle& handle) const
    {
        return m_device->GlobalTextures.Access(handle);
    }

    void RenderResourceManager::EnqueueDeletion(DeferredFreeEntry entry)
    {
        m_device->DeferFree(entry);
    }

    void RenderResourceManager::EnqueueDeletion(VkShaderModule module)
    {
        if (module == VK_NULL_HANDLE)
            return;
        DeferredFreeEntry e;
        e.EntryKind      = DeferredFreeEntry::Kind::VkHandle;
        e.TimelineValue  = m_device->SwapchainPtr->RenderTimelineNextValue;
        e.Data.Vk.Handle = reinterpret_cast<void*>(module);
        e.Data.Vk.Type   = Rendering::DeviceResourceType::SHADERMODULE;
        e.Data.Vk.Extra  = nullptr;
        m_device->DeferFree(e);
    }

    void RenderResourceManager::EnqueueDeletion(VkPipeline pipeline)
    {
        if (pipeline == VK_NULL_HANDLE)
            return;
        DeferredFreeEntry e;
        e.EntryKind      = DeferredFreeEntry::Kind::VkHandle;
        e.TimelineValue  = m_device->SwapchainPtr->RenderTimelineNextValue;
        e.Data.Vk.Handle = reinterpret_cast<void*>(pipeline);
        e.Data.Vk.Type   = Rendering::DeviceResourceType::PIPELINE;
        e.Data.Vk.Extra  = nullptr;
        m_device->DeferFree(e);
    }

    void RenderResourceManager::EnqueueDeletion(VkBuffer buffer, VmaAllocation allocation)
    {
        if (buffer == VK_NULL_HANDLE)
            return;
        DeferredFreeEntry e;
        e.EntryKind              = DeferredFreeEntry::Kind::Buffer;
        e.TimelineValue          = m_device->SwapchainPtr->RenderTimelineNextValue;
        e.Data.Buffer.Handle     = buffer;
        e.Data.Buffer.Allocation = allocation;
        m_device->DeferFree(e);
    }

    void RenderResourceManager::EnqueueDeletion(VkImage image, VkImageView view, VmaAllocation allocation)
    {
        if (image == VK_NULL_HANDLE)
            return;
        DeferredFreeEntry e;
        e.EntryKind             = DeferredFreeEntry::Kind::Image;
        e.TimelineValue         = m_device->SwapchainPtr->RenderTimelineNextValue;
        e.Data.Image.Handle     = image;
        e.Data.Image.ViewHandle = view;
        e.Data.Image.Allocation = allocation;
        m_device->DeferFree(e);
    }

    uint32_t RenderResourceManager::AllocMeshSlot()
    {
        for (uint32_t i = 0; i < m_mesh_slot_count; ++i)
        {
            if (m_mesh_slots[i].Generation == 0)
            {
                m_mesh_slots[i].Generation = ++m_mesh_slot_gen_counter[i];
                return i;
            }
        }
        ZENGINE_VALIDATE_ASSERT(m_mesh_slot_count < MAX_BUFFERS, "RRM: MAX_BUFFERS exceeded")
        uint32_t idx                 = m_mesh_slot_count++;
        m_mesh_slots[idx].Generation = ++m_mesh_slot_gen_counter[idx];
        return idx;
    }

    // Masks the monotonic counter to 31 bits before OR-ing in GBUF_GEN_TAG (bit 31) so the
    // counter can never collide with the tag, and skips 0 on the (practically unreachable)
    // wraparound since Generation == 0 is the universal free/invalid sentinel.
    uint32_t RenderResourceManager::NextGBufGeneration(uint32_t& counter)
    {
        uint32_t gen = (++counter) & 0x7FFF'FFFFu;
        if (gen == 0)
        {
            gen = (++counter) & 0x7FFF'FFFFu;
        }
        return gen | GBUF_GEN_TAG;
    }

    uint32_t RenderResourceManager::AllocGBufSlot()
    {
        for (uint32_t i = 0; i < m_gbuf_slot_count; ++i)
        {
            if (m_gbuf_slots[i].Generation == 0)
            {
                m_gbuf_slots[i].Generation = NextGBufGeneration(m_gbuf_slot_gen_counter[i]);
                return i;
            }
        }
        ZENGINE_VALIDATE_ASSERT(m_gbuf_slot_count < MAX_GENERIC_BUFS, "RRM: MAX_GENERIC_BUFS exceeded")
        uint32_t idx                 = m_gbuf_slot_count++;
        m_gbuf_slots[idx].Generation = NextGBufGeneration(m_gbuf_slot_gen_counter[idx]);
        return idx;
    }

    BufferHandle RenderResourceManager::UploadBuffer(const void* data, size_t byte_size, VkBufferUsageFlags usage, const char* debug_name)
    {
        if (!data || byte_size == 0)
            return {};

        Core::Memory::BufferView buf = m_device->GpuMem.AllocateBuffer(static_cast<VkDeviceSize>(byte_size), usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, Core::Memory::GpuMemoryDomain::DeviceGeometry, debug_name ? debug_name : "RRM::UploadBuffer");

        if (!buf)
            return {};

        EnsureBatchOpen(m_active_frame_index);
        AppendToGlobalBuffer(buf, data, byte_size, 0, m_active_frame_index);

        uint32_t slot_idx           = AllocGBufSlot();
        m_gbuf_slots[slot_idx].Data = buf;
        return {slot_idx, m_gbuf_slots[slot_idx].Generation};
    }

    const Core::Memory::BufferView* RenderResourceManager::GetBuffer(BufferHandle handle) const
    {
        if (!handle.IsValid() || !(handle.Generation & GBUF_GEN_TAG))
            return nullptr;
        if (handle.Index >= m_gbuf_slot_count)
            return nullptr;
        const auto& slot = m_gbuf_slots[handle.Index];
        return slot.Generation == handle.Generation ? &slot.Data : nullptr;
    }

    void RenderResourceManager::InitTextureTimelines()
    {
        uint32_t total_pool_count = m_device->CommandBufferMgr->TotalPoolCount;
        m_tex_total_cmd_count     = m_device->CommandBufferMgr->MaxBufferPerPool * m_device->CommandBufferMgr->MaxBufferPerPool;

        m_tex_timelines.init(m_device->Arena, total_pool_count, total_pool_count);
        m_tex_next_values.init(m_device->Arena, total_pool_count, total_pool_count);
        m_tex_retire_values.init(m_device->Arena, total_pool_count, total_pool_count);
        m_tex_retire_staging.init(m_device->Arena, total_pool_count, total_pool_count);
        m_tex_deferral_retry.init(m_device->Arena, MAX_DEFERRAL_RETRY);

        for (uint32_t i = 0; i < total_pool_count; ++i)
        {
            m_tex_timelines[i] = ZPushStructCtorArgs(m_device->Arena, Rendering::Primitives::Semaphore, m_device, true);
            m_tex_retire_values[i].init(m_device->Arena, m_tex_total_cmd_count, m_tex_total_cmd_count);
            m_tex_retire_staging[i].init(m_device->Arena, m_tex_total_cmd_count, m_tex_total_cmd_count);
            m_tex_next_values[i].store(1, std::memory_order_release);
        }

        if (m_device->HasSeperateTransfertQueueFamily)
        {
            m_tex_transfer_timelines.init(m_device->Arena, total_pool_count, total_pool_count);
            m_tex_transfer_next_values.init(m_device->Arena, total_pool_count, total_pool_count);
            m_tex_transfer_retire.init(m_device->Arena, total_pool_count, total_pool_count);
            m_tex_transfer_staging.init(m_device->Arena, total_pool_count, total_pool_count);

            for (uint32_t i = 0; i < total_pool_count; ++i)
            {
                m_tex_transfer_timelines[i] = ZPushStructCtorArgs(m_device->Arena, Rendering::Primitives::Semaphore, m_device, true);
                m_tex_transfer_retire[i].init(m_device->Arena, m_tex_total_cmd_count, m_tex_total_cmd_count);
                m_tex_transfer_staging[i].init(m_device->Arena, m_tex_total_cmd_count, m_tex_total_cmd_count);
                m_tex_transfer_next_values[i].store(1, std::memory_order_release);
            }
        }
    }

    void RenderResourceManager::InitUploadSlabs(uint32_t worker_count)
    {
        ZENGINE_VALIDATE_ASSERT(Helpers::ThreadPoolHelper::Pool != nullptr, "RenderResourceManager::InitUploadSlabs: ThreadPool not initialized")

        worker_count        = worker_count < Helpers::ThreadPool::MAX_WORKERS ? worker_count : Helpers::ThreadPool::MAX_WORKERS;
        m_upload_slab_count = worker_count;

        for (uint32_t i = 0; i < worker_count; ++i)
            m_upload_slabs[i].Init(m_device->Arena, UPLOAD_SLAB_BYTES);

        // Register per-worker init callback so each worker sets its thread-local slab pointer.
        // The callback runs before any tasks on each worker — no submit-vs-init race.
        struct Ctx
        {
            Core::Memory::TLSFSlab* slabs;
        };
        auto* ctx  = ZPushStructCtor(m_device->Arena, Ctx);
        ctx->slabs = m_upload_slabs;
        Helpers::ThreadPoolHelper::Pool->RegisterWorkerInit(
            [](void* raw, size_t idx) {
                auto* c = static_cast<Ctx*>(raw);
                Helpers::SetWorkerSlab(&c->slabs[idx]);
            },
            ctx);
    }

    void RenderResourceManager::ShutdownTextureTimelines()
    {
        uint32_t total_pool_count = m_device->CommandBufferMgr->TotalPoolCount;
        for (uint32_t p = 0; p < total_pool_count; ++p)
        {
            for (uint32_t i = 0; i < m_tex_total_cmd_count; ++i)
            {
                auto& sb = m_tex_retire_staging[p][i];
                if (sb.Handle != VK_NULL_HANDLE)
                    m_device->GpuMem.FreeBuffer(sb);

                if (m_device->HasSeperateTransfertQueueFamily)
                {
                    auto& tsb = m_tex_transfer_staging[p][i];
                    if (tsb.Handle != VK_NULL_HANDLE)
                        m_device->GpuMem.FreeBuffer(tsb);
                }
            }

            // Explicitly destroy arena-allocated Semaphore objects — VkSemaphore handles
            // are never freed by the arena page release.
            if (p < m_tex_timelines.size() && m_tex_timelines[p])
                m_tex_timelines[p]->~Semaphore();

            if (m_device->HasSeperateTransfertQueueFamily && p < m_tex_transfer_timelines.size() && m_tex_transfer_timelines[p])
                m_tex_transfer_timelines[p]->~Semaphore();
        }
    }

    Rendering::Textures::TextureHandle RenderResourceManager::UploadTextureBuffer(uint8_t frame_index, uint8_t thread_index, const Rendering::Textures::TextureHandle& handle, unsigned char* data)
    {
        using namespace Rendering::Specifications;
        using namespace Rendering::Primitives;

        if (!handle.Valid() || !data)
            return {};

        uint32_t pool_index     = (frame_index * m_device->CommandBufferMgr->TotalThreadCount) + thread_index;

        auto     texture        = m_device->GlobalTextures.Access(handle);
        auto     img_buf        = m_device->ImageBufferManager.Access(texture->BufferHandle);
        auto     img_buf_aspect = (texture->Specification.Format == ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        auto     buffer_handle  = img_buf->GetHandle();

        if (m_device->HasSeperateTransfertQueueFamily)
        {
            auto&    transfer_retire = m_tex_transfer_retire[pool_index];
            uint32_t i               = 0;
            for (; i < GEOMETRY_UPLOAD_SLOT; ++i)
                if (transfer_retire[i] == 0)
                    break;
            if (i >= GEOMETRY_UPLOAD_SLOT)
            {
                ZENGINE_CORE_WARN("[RRM] UploadTextureBuffer: no free transfer slot — upload deferred")
                return {};
            }

            auto                            transfer_cmd = m_device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::TRANSFER_QUEUE, frame_index, thread_index, i);

            ImageMemoryBarrierSpecification to_transfer  = {};
            to_transfer.ImageHandle                      = buffer_handle;
            to_transfer.OldLayout                        = img_buf->Layout;
            to_transfer.NewLayout                        = ImageLayout::TRANSFER_DST_OPTIMAL;
            to_transfer.ImageAspectMask                  = VkImageAspectFlagBits(img_buf_aspect);
            to_transfer.SourceAccessMask                 = VK_ACCESS_NONE;
            to_transfer.DestinationAccessMask            = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_transfer.SourceStageMask                  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            to_transfer.DestinationStageMask             = VK_PIPELINE_STAGE_TRANSFER_BIT;
            to_transfer.LayerCount                       = texture->Specification.LayerCount;
            to_transfer.SourceQueueFamily                = m_device->TransferFamilyIndex;
            to_transfer.DestinationQueueFamily           = m_device->TransferFamilyIndex;
            transfer_cmd->TransitionImageLayout(ImageMemoryBarrier{to_transfer});
            img_buf->Layout             = to_transfer.NewLayout;

            uint32_t   ring_offset      = 0;
            BufferView transfer_staging = m_device->WriteTextureData(transfer_cmd, handle, data, &ring_offset);
            // Ring chunks retire against RenderTimeline (see VulkanDevice::TickMemory),
            // not m_tex_transfer_timelines — the frame's own submission is on the same
            // queue, after this one, so it's a safe (if slightly conservative) proxy for
            // "the transfer copy has definitely finished reading the ring by then".
            if (ring_offset != std::numeric_limits<uint32_t>::max())
                m_device->GpuMem.Ring.Submit(ring_offset, static_cast<uint32_t>(texture->BufferSize), m_device->SwapchainPtr->RenderTimelineNextValue);

            ImageMemoryBarrierSpecification release = {};
            release.ImageHandle                     = buffer_handle;
            release.OldLayout                       = ImageLayout::TRANSFER_DST_OPTIMAL;
            release.NewLayout                       = (img_buf_aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::SHADER_READ_ONLY_OPTIMAL;
            release.ImageAspectMask                 = VkImageAspectFlagBits(img_buf_aspect);
            release.SourceAccessMask                = VK_ACCESS_TRANSFER_WRITE_BIT;
            release.DestinationAccessMask           = VK_ACCESS_NONE;
            release.SourceStageMask                 = VK_PIPELINE_STAGE_TRANSFER_BIT;
            release.DestinationStageMask            = (img_buf_aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            release.LayerCount                      = texture->Specification.LayerCount;
            release.SourceQueueFamily               = m_device->TransferFamilyIndex;
            release.DestinationQueueFamily          = m_device->GraphicFamilyIndex;
            transfer_cmd->TransitionImageLayout(ImageMemoryBarrier{release});
            img_buf->Layout = release.NewLayout;
            transfer_cmd->End();

            uint64_t transfer_val = m_tex_transfer_next_values[pool_index].fetch_add(1, std::memory_order_acq_rel);
            transfer_retire[i]    = transfer_val;
            if (transfer_staging)
                m_tex_transfer_staging[pool_index][i] = transfer_staging;

            m_async_uploads.Enqueue({transfer_cmd, m_tex_transfer_timelines[pool_index], nullptr, VK_PIPELINE_STAGE_TRANSFER_BIT, transfer_val, UINT64_MAX});

            uint32_t acquire_slot  = 0;
            auto&    retire_values = m_tex_retire_values[pool_index];
            for (; acquire_slot < GEOMETRY_UPLOAD_SLOT; ++acquire_slot)
                if (retire_values[acquire_slot] == 0)
                    break;
            if (acquire_slot >= GEOMETRY_UPLOAD_SLOT)
            {
                // Unlike the two early returns above, the transfer-queue copy has already
                // been recorded and enqueued by this point (img_buf->Layout mutated) — this
                // call cannot be safely retried from scratch, so it's treated as done rather
                // than deferred (the texture just stays transfer-owned until a future upload
                // call for the same handle happens to find a free acquire slot).
                ZENGINE_CORE_WARN("[RRM] UploadTextureBuffer: no free acquire slot — texture left transfer-owned")
                return handle;
            }

            auto                            acquire_cmd  = m_device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE, frame_index, thread_index, acquire_slot);
            ImageMemoryBarrierSpecification acquire_spec = release;
            acquire_spec.SourceAccessMask                = VK_ACCESS_NONE;
            acquire_spec.DestinationAccessMask           = VK_ACCESS_SHADER_READ_BIT;
            acquire_spec.SourceQueueFamily               = m_device->TransferFamilyIndex;
            acquire_spec.DestinationQueueFamily          = m_device->GraphicFamilyIndex;
            acquire_cmd->TransitionImageLayout(ImageMemoryBarrier{acquire_spec});
            acquire_cmd->End();

            uint64_t graphics_val       = m_tex_next_values[pool_index].fetch_add(1, std::memory_order_acq_rel);
            retire_values[acquire_slot] = graphics_val;
            m_async_uploads.Enqueue({acquire_cmd, m_tex_timelines[pool_index], m_tex_transfer_timelines[pool_index], (uint32_t) release.DestinationStageMask, graphics_val, transfer_val});
        }
        else
        {
            auto&    retire_values = m_tex_retire_values[pool_index];
            uint32_t i             = 0;
            for (; i < GEOMETRY_UPLOAD_SLOT; ++i)
                if (retire_values[i] == 0)
                    break;
            if (i >= GEOMETRY_UPLOAD_SLOT)
            {
                ZENGINE_CORE_WARN("[RRM] UploadTextureBuffer: no free graphics slot — upload deferred")
                return {};
            }

            auto                            cmd         = m_device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE, frame_index, thread_index, i);

            ImageMemoryBarrierSpecification to_transfer = {};
            to_transfer.ImageHandle                     = buffer_handle;
            to_transfer.OldLayout                       = img_buf->Layout;
            to_transfer.NewLayout                       = ImageLayout::TRANSFER_DST_OPTIMAL;
            to_transfer.ImageAspectMask                 = VkImageAspectFlagBits(img_buf_aspect);
            to_transfer.SourceAccessMask                = VK_ACCESS_NONE;
            to_transfer.DestinationAccessMask           = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_transfer.SourceStageMask                 = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            to_transfer.DestinationStageMask            = VK_PIPELINE_STAGE_TRANSFER_BIT;
            to_transfer.LayerCount                      = texture->Specification.LayerCount;
            to_transfer.SourceQueueFamily               = m_device->GraphicFamilyIndex;
            to_transfer.DestinationQueueFamily          = m_device->GraphicFamilyIndex;
            cmd->TransitionImageLayout(ImageMemoryBarrier{to_transfer});
            img_buf->Layout        = to_transfer.NewLayout;

            uint32_t   ring_offset = 0;
            BufferView staging     = m_device->WriteTextureData(cmd, handle, data, &ring_offset);
            // See the separate-transfer-queue branch above for why RenderTimelineNextValue
            // (not signal_value/m_tex_timelines) is the correct retirement marker here.
            if (ring_offset != std::numeric_limits<uint32_t>::max())
                m_device->GpuMem.Ring.Submit(ring_offset, static_cast<uint32_t>(texture->BufferSize), m_device->SwapchainPtr->RenderTimelineNextValue);

            ImageMemoryBarrierSpecification to_final = {};
            to_final.ImageHandle                     = buffer_handle;
            to_final.OldLayout                       = ImageLayout::TRANSFER_DST_OPTIMAL;
            to_final.NewLayout                       = (img_buf_aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::SHADER_READ_ONLY_OPTIMAL;
            to_final.ImageAspectMask                 = VkImageAspectFlagBits(img_buf_aspect);
            to_final.SourceAccessMask                = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_final.DestinationAccessMask           = VK_ACCESS_SHADER_READ_BIT;
            to_final.SourceStageMask                 = VK_PIPELINE_STAGE_TRANSFER_BIT;
            to_final.DestinationStageMask            = (img_buf_aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            to_final.LayerCount                      = texture->Specification.LayerCount;
            to_final.SourceQueueFamily               = m_device->GraphicFamilyIndex;
            to_final.DestinationQueueFamily          = m_device->GraphicFamilyIndex;
            cmd->TransitionImageLayout(ImageMemoryBarrier{to_final});
            cmd->End();

            uint64_t signal_value = m_tex_next_values[pool_index].fetch_add(1, std::memory_order_acq_rel);
            retire_values[i]      = signal_value;
            if (staging)
                m_tex_retire_staging[pool_index][i] = staging;
            m_async_uploads.Enqueue({cmd, m_tex_timelines[pool_index], nullptr, (uint32_t) to_final.DestinationStageMask, signal_value, UINT64_MAX});
            img_buf->Layout = to_final.NewLayout;
        }
        return handle;
    }

    Rendering::Textures::TextureHandle RenderResourceManager::UploadFontAtlas(unsigned char* pixels, uint32_t width, uint32_t height)
    {
        using namespace Rendering::Specifications;
        using namespace Rendering::Primitives;

        if (!pixels || width == 0 || height == 0)
            return {};

        TextureSpecification spec                     = {};
        spec.Width                                    = width;
        spec.Height                                   = height;
        spec.Format                                   = ImageFormat::R8G8B8A8_UNORM;
        spec.PerformTransition                        = false;

        auto                            handle        = m_device->CreateTexture(spec);
        auto                            texture       = m_device->GlobalTextures.Access(handle);
        auto                            img_buf       = m_device->ImageBufferManager.Access(texture->BufferHandle);
        auto                            buffer_handle = img_buf->GetHandle();

        ImageMemoryBarrierSpecification to_transfer   = {};
        to_transfer.ImageHandle                       = buffer_handle;
        to_transfer.OldLayout                         = img_buf->Layout;
        to_transfer.NewLayout                         = ImageLayout::TRANSFER_DST_OPTIMAL;
        to_transfer.ImageAspectMask                   = VK_IMAGE_ASPECT_COLOR_BIT;
        to_transfer.SourceAccessMask                  = VK_ACCESS_NONE;
        to_transfer.DestinationAccessMask             = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_transfer.SourceStageMask                   = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        to_transfer.DestinationStageMask              = VK_PIPELINE_STAGE_TRANSFER_BIT;
        to_transfer.LayerCount                        = 1;
        to_transfer.SourceQueueFamily                 = m_device->GraphicFamilyIndex;
        to_transfer.DestinationQueueFamily            = m_device->GraphicFamilyIndex;

        ImageMemoryBarrierSpecification to_final      = {};
        to_final.ImageHandle                          = buffer_handle;
        to_final.OldLayout                            = ImageLayout::TRANSFER_DST_OPTIMAL;
        to_final.NewLayout                            = ImageLayout::SHADER_READ_ONLY_OPTIMAL;
        to_final.ImageAspectMask                      = VK_IMAGE_ASPECT_COLOR_BIT;
        to_final.SourceAccessMask                     = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_final.DestinationAccessMask                = VK_ACCESS_SHADER_READ_BIT;
        to_final.SourceStageMask                      = VK_PIPELINE_STAGE_TRANSFER_BIT;
        to_final.DestinationStageMask                 = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        to_final.LayerCount                           = 1;
        to_final.SourceQueueFamily                    = m_device->GraphicFamilyIndex;
        to_final.DestinationQueueFamily               = m_device->GraphicFamilyIndex;

        auto*                         cmd             = m_upload_cmd_mgr->GetCommandBuffer(QueueType::GRAPHIC_QUEUE, m_active_frame_index, 0, 0, false);
        Rendering::Primitives::Fence* fence           = m_sync_upload_fence;
        fence->Wait(UINT64_MAX);
        fence->Reset();
        cmd->ResetState();
        vkResetCommandBuffer(cmd->GetHandle(), 0);
        cmd->Begin();
        cmd->TransitionImageLayout(ImageMemoryBarrier{to_transfer});
        img_buf->Layout        = to_transfer.NewLayout;
        uint32_t   ring_offset = 0;
        BufferView staging     = m_device->WriteTextureData(cmd, handle, pixels, &ring_offset);
        cmd->TransitionImageLayout(ImageMemoryBarrier{to_final});
        img_buf->Layout = to_final.NewLayout;
        cmd->End();

        VkQueue         gfx_queue = m_device->GetQueue(QueueType::GRAPHIC_QUEUE).Handle;
        VkCommandBuffer raw       = cmd->GetHandle();
        VkSubmitInfo    submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &raw;

        vkQueueSubmit(gfx_queue, 1, &submit, fence->GetHandle());
        fence->Wait(UINT64_MAX);
        cmd->ResetState();

        // Gated on the render timeline's own progress rather than marked safe at value 0 —
        // the fence wait above is not trustworthy proof of completion once any command
        // buffer on the queue has already timed out (issue #764 follow-up investigation).
        if (ring_offset != std::numeric_limits<uint32_t>::max())
            m_device->GpuMem.Ring.Submit(ring_offset, static_cast<uint32_t>(texture->BufferSize), m_device->SwapchainPtr->RenderTimelineNextValue);

        if (staging)
        {
            DeferredFreeEntry e;
            e.EntryKind   = DeferredFreeEntry::Kind::Buffer;
            e.Data.Buffer = staging;
            m_device->DeferFree(e);
        }

        return handle;
    }

    void RenderResourceManager::EnqueueTextureDeferral(TextureDeferral&& deferral)
    {
        m_tex_deferral_queue.Emplace(std::forward<TextureDeferral>(deferral));
    }

    void RenderResourceManager::CompleteDeferrals(uint8_t frame_index)
    {
        // Deferrals that found no free upload slot this pass are collected here and
        // requeued after the loop — retried next frame instead of freeing pixel data
        // that was never actually copied to the GPU, and instead of spinning in place
        // waiting for a slot that won't free up within this same call.
        m_tex_deferral_retry.clear();
        while (!m_tex_deferral_queue.Empty())
        {
            TextureDeferral d = {};
            m_tex_deferral_queue.Pop(d);
            auto result = UploadTextureBuffer(frame_index, 0, d.TexHandle, d.Pixels);
            if (!result.Valid())
            {
                m_tex_deferral_retry.push(std::move(d));
                continue;
            }
            // Free slab-owned pixels after upload. Nullptr = borrowed pointer, skip.
            if (d.Slab && d.Pixels)
                d.Slab->Free(d.Pixels);
        }
        for (auto& d : m_tex_deferral_retry)
            m_tex_deferral_queue.Emplace(std::move(d));
    }

    void RenderResourceManager::SubmitAsyncUploads()
    {
        m_async_uploads.SubmitAll();
    }

    void RenderResourceManager::RetireTextureSlots(uint8_t frame_index, uint8_t thread_index)
    {
        uint32_t pool_index     = (frame_index * m_device->CommandBufferMgr->TotalThreadCount) + thread_index;
        uint64_t graphics_value = 0;
        vkGetSemaphoreCounterValue(m_device->LogicalDevice, m_tex_timelines[pool_index]->GetHandle(), &graphics_value);

        auto& retire_values = m_tex_retire_values[pool_index];
        for (uint32_t i = 0; i < GEOMETRY_UPLOAD_SLOT; ++i)
        {
            auto retire_val = retire_values[i];
            if (retire_val != 0 && graphics_value >= retire_val)
            {
                auto cmd = m_device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE, frame_index, thread_index, i, false);
                cmd->ResetState();
                vkResetCommandBuffer(cmd->GetHandle(), 0);
                retire_values[i] = 0;

                auto& sb         = m_tex_retire_staging[pool_index][i];
                if (sb.Handle != VK_NULL_HANDLE)
                    m_device->GpuMem.FreeBuffer(sb);
            }
        }

        if (m_device->HasSeperateTransfertQueueFamily)
        {
            uint64_t transfer_value = 0;
            vkGetSemaphoreCounterValue(m_device->LogicalDevice, m_tex_transfer_timelines[pool_index]->GetHandle(), &transfer_value);
            auto& transfer_retire = m_tex_transfer_retire[pool_index];
            for (uint32_t i = 0; i < GEOMETRY_UPLOAD_SLOT; ++i)
            {
                auto tv = transfer_retire[i];
                if (tv != 0 && transfer_value >= tv)
                {
                    auto cmd = m_device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::TRANSFER_QUEUE, frame_index, thread_index, i, false);
                    cmd->ResetState();
                    vkResetCommandBuffer(cmd->GetHandle(), 0);
                    transfer_retire[i] = 0;

                    auto& tsb          = m_tex_transfer_staging[pool_index][i];
                    if (tsb.Handle != VK_NULL_HANDLE)
                        m_device->GpuMem.FreeBuffer(tsb);
                }
            }
        }
    }

    void RenderResourceManager::ClearAsyncUploads()
    {
        m_async_uploads.Clear();
        m_device->AsyncGPUOperations.Clear();
    }

    void RenderResourceManager::ResetTextureTimelines()
    {
        auto total_thread_count = m_device->CommandBufferMgr->TotalThreadCount;
        auto frame_count        = m_device->SwapchainPtr->BufferredFrameCount;

        for (uint32_t f = 0; f < frame_count; ++f)
        {
            for (uint32_t t = 0; t < total_thread_count; ++t)
            {
                RetireTextureSlots(static_cast<uint8_t>(f), static_cast<uint8_t>(t));

                uint32_t pool_index = (f * total_thread_count) + t;
                uint64_t gv         = 0;
                vkGetSemaphoreCounterValue(m_device->LogicalDevice, m_tex_timelines[pool_index]->GetHandle(), &gv);
                m_tex_next_values[pool_index].store(gv + 1, std::memory_order_release);

                if (m_device->HasSeperateTransfertQueueFamily)
                {
                    uint64_t tv = 0;
                    vkGetSemaphoreCounterValue(m_device->LogicalDevice, m_tex_transfer_timelines[pool_index]->GetHandle(), &tv);
                    m_tex_transfer_next_values[pool_index].store(tv + 1, std::memory_order_release);
                }
            }
        }
    }

    Rendering::Textures::TextureHandle RenderResourceManager::IngestTexture(const uuids::uuid& uuid, const char* absolute_path, Rendering::Textures::TextureHandle existing)
    {
        ZENGINE_LOG_RENDER_INFO("[RRM] {} texture {} from {}", existing.Valid() ? "Reloading" : "Ingesting", uuids::to_string(uuid), absolute_path)
        return SubmitTextureFile(absolute_path, existing);
    }

    void RenderResourceManager::ScheduleTextureReload(const uuids::uuid& uuid)
    {
        std::lock_guard lock(m_pending_texture_reload_mutex);
        for (uint32_t i = 0; i < m_pending_texture_reload_count; ++i)
            if (m_pending_texture_reloads[i] == uuid)
                return; // already pending — dedupe
        if (m_pending_texture_reload_count >= MAX_PENDING)
        {
            ZENGINE_LOG_RENDER_WARN("[RRM] Pending texture reload queue full — dropping reload for {}", uuids::to_string(uuid))
            return;
        }
        m_pending_texture_reloads[m_pending_texture_reload_count++] = uuid;
    }

    void RenderResourceManager::FlushPendingTextureReloads()
    {
        uint32_t    count = 0;
        uuids::uuid local[MAX_PENDING];
        {
            std::lock_guard lock(m_pending_texture_reload_mutex);
            count = m_pending_texture_reload_count;
            secure_memcpy(local, sizeof(local), m_pending_texture_reloads, count * sizeof(local[0]));
            m_pending_texture_reload_count = 0;
        }

        for (uint32_t i = 0; i < count; ++i)
        {
            Rendering::Textures::TextureHandle existing = AssetManager::FindTextureHandle(local[i]);
            if (!existing.Valid())
                continue;

            // IngestMutex guards the read: AssetManager::Textures (unlike Meshes/Materials)
            // is arena-backed, so a concurrent IngestTexture on the import thread can
            // reallocate its backing storage mid-read without this lock.
            char full_path[MAX_FILE_PATH_COUNT] = {};
            {
                std::lock_guard lock(AssetManager::Instance()->IngestMutex);
                AssetTexture*   tex = AssetManager::GetAsset<AssetTexture>(local[i]);
                if (!tex || tex->Path.empty())
                    continue;
                snprintf(full_path, sizeof(full_path), "%s%c%s", AssetManager::Instance()->CurrentWorkingSpacePath, PLATFORM_OS_BACKSLASH, tex->Path.c_str());
            }
            IngestTexture(local[i], full_path, existing);
        }
    }

    void RenderResourceManager::ReleaseTexture(const uuids::uuid& uuid)
    {
        // Captured before AssetManager::ReleaseTexture's deferred patch runs — that patch
        // erases the UUID→handle map entry, so the handle must be read now or it's lost.
        Rendering::Textures::TextureHandle handle = AssetManager::FindTextureHandle(uuid);

        AssetManager::ReleaseTexture(uuid);

        if (!handle.Valid())
            return;

        std::lock_guard lock(m_pending_texture_release_mutex);
        if (m_pending_texture_release_count >= MAX_PENDING)
        {
            ZENGINE_LOG_RENDER_ERR("[RRM] Pending texture release queue full — texture handle (index {}) leaked", handle.Index)
            return;
        }
        m_pending_texture_releases[m_pending_texture_release_count++] = handle;
    }

    void RenderResourceManager::FlushPendingTextureReleases()
    {
        uint32_t                           count = 0;
        Rendering::Textures::TextureHandle local[MAX_PENDING];
        {
            std::lock_guard lock(m_pending_texture_release_mutex);
            count = m_pending_texture_release_count;
            secure_memcpy(local, sizeof(local), m_pending_texture_releases, count * sizeof(local[0]));
            m_pending_texture_release_count = 0;
        }

        for (uint32_t i = 0; i < count; ++i)
            m_device->DestroyTexture(local[i]);
    }

    Rendering::Textures::TextureHandle RenderResourceManager::SubmitTextureFile(const char* filename, Rendering::Textures::TextureHandle existing)
    {
        using namespace Rendering::Specifications;

        std::unique_lock<std::mutex> l(m_pending_mutex);

        auto                         abs_filename = std::filesystem::absolute(filename).string();
        auto                         file_ext     = std::filesystem::path(abs_filename).extension().string();

        TextureSpecification         spec{};

        if (file_ext == ".zenvmap")
        {
            Importers::AssetCodec::EnvironmentMapFileHeader env_header{};
            if (!Importers::AssetCodec::ReadEnvironmentMapFileHeader(abs_filename.c_str(), env_header))
            {
                ZENGINE_CORE_ERROR("Failed to read .zenvmap header: {}", abs_filename)
                return {};
            }
            spec.IsCubemap  = true;
            spec.LayerCount = static_cast<uint32_t>(env_header.LayerCount);
            spec.Format     = ImageFormat::R32G32B32A32_SFLOAT;
            spec.Width      = static_cast<uint32_t>(env_header.FaceWidth);
            spec.Height     = static_cast<uint32_t>(env_header.FaceHeight);
        }
        else
        {
            int w, h, ch;
            if (!stbi_info(abs_filename.c_str(), &w, &h, &ch))
                return {};

            const std::set<std::string_view> known_cubemap_ext = {".hdr", ".exr"};
            spec.Width                                         = static_cast<uint32_t>(w);
            spec.Height                                        = static_cast<uint32_t>(h);
            spec.Format                                        = ImageFormat::R8G8B8A8_SRGB;

            if (known_cubemap_ext.contains(file_ext))
            {
                int face_size   = w / 4;
                spec.IsCubemap  = true;
                spec.LayerCount = 6;
                spec.Format     = ImageFormat::R32G32B32A32_SFLOAT;
                spec.Width      = static_cast<uint32_t>(face_size);
                spec.Height     = static_cast<uint32_t>(face_size);
            }
        }

        spec.BytePerPixel = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(spec.Format)];

        Rendering::Textures::TextureHandle tex_handle;
        if (existing.Valid())
        {
            // Reimport — reconstruct in place only if dimensions/format actually changed;
            // same handle, same bindless index either way.
            auto* texture = m_device->GlobalTextures.Access(existing);
            if (texture && (texture->Width != spec.Width || texture->Height != spec.Height || texture->Specification.Format != spec.Format))
                m_device->ReconstructTexture(existing, spec);
            tex_handle = existing;
        }
        else
        {
            tex_handle = m_device->CreateTexture(spec);
        }

        // Capture everything by value for the thread pool lambda.
        std::string                        captured_filename = abs_filename;
        std::string                        captured_ext      = file_ext;
        TextureSpecification               captured_spec     = spec;
        Rendering::Textures::TextureHandle captured_handle   = tex_handle;

        Helpers::ThreadPoolHelper::Submit([this, captured_filename, captured_ext, captured_spec, captured_handle]() mutable {
            std::vector<uint8_t> buffer;

            if (captured_spec.IsCubemap)
            {
                if (captured_ext == ".zenvmap")
                {
                    Rendering::Buffers::Bitmap cubemap{};
                    if (!Importers::AssetCodec::DeserializeEnvironmentMapFile(captured_filename.c_str(), cubemap))
                    {
                        ZENGINE_CORE_ERROR("Failed to deserialize .zenvmap: {}", captured_filename)
                        return;
                    }
                    size_t bytes = cubemap.BufferSize;
                    buffer.resize(bytes);
                    Helpers::secure_memmove(buffer.data(), bytes, cubemap.Buffer, bytes);
                }
                else
                {
                    int          w, h, ch;
                    const float* image_data = stbi_loadf(captured_filename.c_str(), &w, &h, &ch, 4);
                    if (!image_data)
                    {
                        ZENGINE_CORE_ERROR("Failed to load texture: {}", captured_filename) return;
                    }

                    Core::Memory::TLSFSlab* slab            = Helpers::GetWorkerSlab();
                    size_t                  float_buf_bytes = 0;
                    float*                  output_buf      = nullptr;
                    if (ch == STBI_rgb)
                    {
                        size_t total    = (size_t) (w * h);
                        float_buf_bytes = total * 4 * sizeof(float);
                        output_buf      = slab ? static_cast<float*>(slab->Alloc(float_buf_bytes)) : new float[total * 4];
                        stbir_resize_float(image_data, w, h, 0, output_buf, w, h, 0, 4);
                        for (size_t i = 0; i < total; ++i)
                            output_buf[i * 4 + 3] = 255.f;
                    }
                    else
                    {
                        float_buf_bytes = (size_t) (w * h * ch) * sizeof(float);
                        output_buf      = slab ? static_cast<float*>(slab->Alloc(float_buf_bytes)) : new float[w * h * ch];
                        Helpers::secure_memcpy(output_buf, float_buf_bytes, image_data, float_buf_bytes);
                    }
                    stbi_image_free((void*) image_data);

                    Rendering::Buffers::Bitmap in = Rendering::Buffers::Bitmap::FromData(w, h, 1, 4, Rendering::Buffers::BitmapFormat::Float, Rendering::Buffers::BitmapType::Texture2D, output_buf, slab);
                    if (slab)
                        slab->Free(output_buf);
                    else
                        delete[] output_buf;

                    Rendering::Buffers::Bitmap vertical_cross = Rendering::Buffers::BitmapConvert::EquirectToCross(in, slab);
                    Rendering::Buffers::Bitmap cubemap        = Rendering::Buffers::BitmapConvert::CrossToCubemap(vertical_cross, slab);

                    size_t                     bytes          = cubemap.BufferSize;
                    buffer.resize(bytes);
                    Helpers::secure_memmove(buffer.data(), bytes, cubemap.Buffer, bytes);
                }
            }
            else
            {
                stbi_set_flip_vertically_on_load(1);
                int      w, h, ch;
                stbi_uc* image_data = stbi_load(captured_filename.c_str(), &w, &h, &ch, STBI_rgb_alpha);
                if (!image_data)
                {
                    ZENGINE_CORE_ERROR("Failed to load texture: {}", captured_filename) return;
                }

                if (ch <= STBI_rgb)
                {
                    size_t total = w * h;
                    buffer.resize(total * 4);
                    stbir_resize_uint8(image_data, w, h, 0, buffer.data(), w, h, 0, 4);
                    for (size_t i = 0; i < total; ++i)
                        buffer[i * 4 + 3] = 255;
                }
                else
                {
                    size_t bytes = (size_t) (w * h * ch);
                    buffer.resize(bytes);
                    Helpers::secure_memmove(buffer.data(), bytes, image_data, bytes);
                }
                stbi_image_free(image_data);
            }

            // Copy final pixels into a TLSFSlab allocation so the local buffer
            // vector can be destroyed without freeing the pixel data.
            Core::Memory::TLSFSlab* slab   = Helpers::GetWorkerSlab();
            size_t                  bytes  = buffer.size();
            uint8_t*                pixels = nullptr;
            if (slab && bytes > 0)
            {
                pixels = static_cast<uint8_t*>(slab->Alloc(bytes));
                Helpers::secure_memmove(pixels, bytes, buffer.data(), bytes);
            }
            TextureDeferral deferral;
            deferral.Pixels    = pixels;
            deferral.ByteSize  = bytes;
            deferral.Slab      = slab;
            deferral.TexHandle = captured_handle;
            EnqueueTextureDeferral(std::move(deferral));
            m_device->RequestDescriptorUpdate(captured_handle);
        });

        return tex_handle;
    }

    Rendering::Textures::TextureHandle RenderResourceManager::GetOrCreateFallbackTexture()
    {
        static constexpr const char* kFallbackPath = "ZodiacEngine/Settings/FallbackTexture.png";

        if (!std::filesystem::exists(kFallbackPath))
        {
            // 4×4 (255, 20, 147, 255) fallback color for missing textures
            static constexpr int     W = 4, H = 4;
            static constexpr uint8_t R = 255, G = 20, B = 147, A = 255;
            uint8_t                  pixels[W * H * 4];
            for (int i = 0; i < W * H; ++i)
            {
                pixels[i * 4 + 0] = R;
                pixels[i * 4 + 1] = G;
                pixels[i * 4 + 2] = B;
                pixels[i * 4 + 3] = A;
            }
            stbi_write_png(kFallbackPath, W, H, 4, pixels, W * 4);
        }

        return SubmitTextureFile(kFallbackPath);
    }

} // namespace ZEngine::Rendering
