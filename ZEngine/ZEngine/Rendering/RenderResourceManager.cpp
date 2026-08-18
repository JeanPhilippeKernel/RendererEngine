#define STB_IMAGE_IMPLEMENTATION
#ifdef __GNUC__
#define STBI_NO_SIMD
#endif
#include <stb/stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/ThreadPool.h>
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

        registry->SetOnReadyCallback(this, [](void* ctx, const uuids::uuid& uuid, AssetHandle handle) {
            auto*              rrm = static_cast<RenderResourceManager*>(ctx);

            const AssetRecord* rec = rrm->m_registry->FindByUUID(uuid);
            if (!rec)
                return;

            UploadKind kind;
            if (rec->Type == AssetType::MESH)
                kind = UploadKind::Mesh;
            else if (rec->Type == AssetType::TEXTURE)
                kind = UploadKind::Texture;
            else
                return;

            // Deduplicate: hold both locks together so two concurrent callbacks
            // for the same UUID can't both pass the check before either pushes.
            std::lock_guard map_lock(rrm->m_uuid_map_mutex);
            std::lock_guard pend_lock(rrm->m_pending_mutex);

            if (kind == UploadKind::Mesh)
            {
                for (uint32_t i = 0; i < rrm->m_uuid_to_buffer_count; ++i)
                    if (rrm->m_uuid_to_buffer[i].UUID == uuid)
                        return;
                for (uint32_t i = 0; i < rrm->m_pending_count; ++i)
                    if (rrm->m_pending[i].Kind == UploadKind::Mesh && rrm->m_pending[i].UUID == uuid)
                        return;
            }
            else
            {
                for (uint32_t i = 0; i < rrm->m_uuid_to_image_count; ++i)
                    if (rrm->m_uuid_to_image[i].UUID == uuid)
                        return;
                for (uint32_t i = 0; i < rrm->m_pending_count; ++i)
                    if (rrm->m_pending[i].Kind == UploadKind::Texture && rrm->m_pending[i].UUID == uuid)
                        return;
            }

            if (rrm->m_pending_count >= MAX_PENDING)
            {
                ZENGINE_CORE_WARN("[RRM] Pending upload queue full — dropping asset")
                return;
            }

            rrm->m_pending[rrm->m_pending_count++] = {kind, handle, uuid};
        });

        registry->SetOnStaleCallback(this, [](void* ctx, const uuids::uuid& uuid) {
            auto*           rrm = static_cast<RenderResourceManager*>(ctx);

            // Look up current GPU handle for this UUID and schedule a swap.
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
            for (uint32_t i = 0; i < rrm->m_uuid_to_image_count; ++i)
            {
                if (rrm->m_uuid_to_image[i].UUID == uuid)
                {
                    AssetHandle new_asset = 0;
                    {
                        const AssetRecord* rec = rrm->m_registry->FindByUUID(uuid);
                        if (rec)
                            new_asset = rec->SlotHandle;
                    }
                    rrm->ScheduleSwap(rrm->m_uuid_to_image[i].Handle, new_asset);
                    return;
                }
            }
        });
    }

    void RenderResourceManager::InitUploadPool()
    {
        // Create fences pre-signaled so vkWaitForFences before the first submit returns immediately.
        VkFenceCreateInfo fence_ci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT};

        m_upload_pool = ZPushStructCtorArgs(m_device->Arena, Rendering::Pools::CommandPool, m_device, QueueType::GRAPHIC_QUEUE);
        m_upload_cmd  = ZPushStructCtorArgs(m_device->Arena, CommandBuffer, m_device, m_upload_pool->Handle, QueueType::GRAPHIC_QUEUE, true);
        vkCreateFence(m_device->LogicalDevice, &fence_ci, nullptr, &m_upload_fence);

        if (m_device->HasSeperateTransfertQueueFamily)
        {
            m_transfer_pool = ZPushStructCtorArgs(m_device->Arena, Rendering::Pools::CommandPool, m_device, QueueType::TRANSFER_QUEUE);
            m_transfer_cmd  = ZPushStructCtorArgs(m_device->Arena, CommandBuffer, m_device, m_transfer_pool->Handle, QueueType::TRANSFER_QUEUE, true);
            vkCreateFence(m_device->LogicalDevice, &fence_ci, nullptr, &m_transfer_fence);
        }
    }

    void RenderResourceManager::Shutdown()
    {
        if (!m_device)
            return;

        m_device->QueueWaitAll();
        ShutdownTextureTimelines();

        auto destroy_fence = [&](VkFence& fence) {
            if (fence != VK_NULL_HANDLE)
            {
                vkDestroyFence(m_device->LogicalDevice, fence, nullptr);
                fence = VK_NULL_HANDLE;
            }
        };
        destroy_fence(m_upload_fence);
        destroy_fence(m_transfer_fence);
        // m_upload_pool/cmd and m_transfer_pool/cmd are arena-allocated — destroyed with the arena.

        // Free all live buffer slots
        if (m_global_vertex_buf)
            m_device->GpuMem.FreeBuffer(m_global_vertex_buf);
        if (m_global_index_buf)
            m_device->GpuMem.FreeBuffer(m_global_index_buf);

        for (uint32_t i = 0; i < m_image_slot_count; ++i)
        {
            if (m_image_slots[i].Generation != 0 && m_image_slots[i].Data)
                m_device->GpuMem.FreeImage(m_image_slots[i].Data, m_device->LogicalDevice);
        }

        for (uint32_t i = 0; i < m_gbuf_slot_count; ++i)
        {
            if (m_gbuf_slots[i].Generation != 0 && m_gbuf_slots[i].Data)
                m_device->GpuMem.FreeBuffer(m_gbuf_slots[i].Data);
        }

        m_device   = nullptr;
        m_registry = nullptr;
    }

    void RenderResourceManager::BeginFrame(uint32_t frame_index)
    {
        FlushPendingUploads(frame_index);
    }

    void RenderResourceManager::EndFrame(uint32_t frame_index)
    {
        std::lock_guard lock(m_pending_mutex);

        uint32_t        write = 0;
        for (uint32_t i = 0; i < m_swap_count; ++i)
        {
            SwapEntry& e = m_swaps[i];
            if (frame_index < e.SwapSafeFrame)
            {
                m_swaps[write++] = e;
                continue;
            }

            if (e.Kind == SwapKind::Image)
            {
                BufferImage* slot = GetImageMutable(e.OldImage);
                if (slot)
                {
                    DeferredFreeEntry entry;
                    entry.EntryKind     = DeferredFreeEntry::Kind::Image;
                    entry.TimelineValue = m_device->SwapchainPtr->RenderTimelineNextValue;
                    entry.Data.Image    = *slot;
                    m_device->DeferFree(entry);
                    *slot = e.NewImage;
                }
            }
        }
        m_swap_count    = write;
        m_current_frame = frame_index + 1;
    }

    static void RecordAndSubmit(VkDevice device, CommandBuffer* cmd, VkFence fence, VkQueue queue, std::function<void(VkCommandBuffer)> record_fn, VkSemaphore wait_semaphore = VK_NULL_HANDLE, uint64_t wait_value = 0)
    {
        cmd->ResetState();
        vkResetCommandBuffer(cmd->GetHandle(), 0);
        cmd->Begin();
        record_fn(cmd->GetHandle());
        cmd->End();

        VkTimelineSemaphoreSubmitInfo timeline_wait{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
        VkPipelineStageFlags          wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

        VkCommandBuffer               raw        = cmd->GetHandle();
        VkSubmitInfo                  submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &raw;

        if (wait_semaphore != VK_NULL_HANDLE && wait_value > 0)
        {
            timeline_wait.waitSemaphoreValueCount   = 1;
            timeline_wait.pWaitSemaphoreValues      = &wait_value;
            timeline_wait.signalSemaphoreValueCount = 0;
            submit.pNext                            = &timeline_wait;
            submit.waitSemaphoreCount               = 1;
            submit.pWaitSemaphores                  = &wait_semaphore;
            submit.pWaitDstStageMask                = &wait_stage;
        }

        // Wait before reset — fence may be in-flight under MoltenVK async completion.
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &fence);
        vkQueueSubmit(queue, 1, &submit, fence);
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        cmd->ResetState();
    }

    void RenderResourceManager::InitGlobalBuffers()
    {
        m_global_vertex_buf = m_device->GpuMem.AllocateBuffer(GLOBAL_VTX_CAPACITY, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, GpuMemoryDomain::DeviceGeometry, "RRM::GlobalVertexBuffer");

        m_global_index_buf  = m_device->GpuMem.AllocateBuffer(GLOBAL_IDX_CAPACITY, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, GpuMemoryDomain::DeviceGeometry, "RRM::GlobalIndexBuffer");

        m_vtx_cursor        = 0;
        m_idx_cursor        = 0;

        ZENGINE_VALIDATE_ASSERT(m_global_vertex_buf, "RRM: global vertex buffer allocation failed")
        ZENGINE_VALIDATE_ASSERT(m_global_index_buf, "RRM: global index buffer allocation failed")
    }

    void RenderResourceManager::RegisterBuiltinGeometry(const void* vtx_data, size_t vtx_bytes, const uint32_t* idx_data, uint32_t idx_count, uint32_t& out_vtx_offset, uint32_t& out_idx_offset)
    {
        const size_t idx_bytes = idx_count * sizeof(uint32_t);
        ZENGINE_VALIDATE_ASSERT(m_vtx_cursor + vtx_bytes <= GLOBAL_VTX_CAPACITY, "RRM::RegisterBuiltinGeometry: global vertex buffer out of space")
        ZENGINE_VALIDATE_ASSERT(m_idx_cursor + idx_bytes <= GLOBAL_IDX_CAPACITY, "RRM::RegisterBuiltinGeometry: global index buffer out of space")

        AppendToGlobalBuffer(m_global_vertex_buf, vtx_data, vtx_bytes, m_vtx_cursor, 0);
        AppendToGlobalBuffer(m_global_index_buf, idx_data, idx_bytes, m_idx_cursor, 0);

        out_vtx_offset  = static_cast<uint32_t>(m_vtx_cursor / (8 * sizeof(float)));
        out_idx_offset  = static_cast<uint32_t>(m_idx_cursor / sizeof(uint32_t));
        m_vtx_cursor   += vtx_bytes;
        m_idx_cursor   += idx_bytes;
    }

    void RenderResourceManager::AppendToGlobalBuffer(BufferView& global_buf, const void* data, size_t byte_size, VkDeviceSize byte_offset, uint32_t frame_index)
    {
        BufferView staging = m_device->GpuMem.AllocateBuffer(static_cast<VkDeviceSize>(byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, GpuMemoryDomain::HostStaging, "RRM::Staging");
        ZENGINE_VALIDATE_ASSERT(staging, "RRM::AppendToGlobalBuffer: staging alloc failed")

        ZENGINE_VALIDATE_ASSERT(vmaCopyMemoryToAllocation(m_device->GpuMem.Allocator, data, staging.Allocation, 0, byte_size) == VK_SUCCESS, "RRM::AppendToGlobalBuffer: staging copy failed")

        VkSemaphore render_timeline = m_device->SwapchainPtr ? m_device->SwapchainPtr->RenderTimeline->GetHandle() : VK_NULL_HANDLE;
        uint64_t    render_value    = m_device->SwapchainPtr ? m_device->SwapchainPtr->RenderTimelineNextValue : 0;
        VkQueue     gfx_queue       = m_device->GetQueue(QueueType::GRAPHIC_QUEUE).Handle;

        RecordAndSubmit(
            m_device->LogicalDevice,
            m_upload_cmd,
            m_upload_fence,
            gfx_queue,
            [&](VkCommandBuffer cmd) {
                VkBufferCopy region{.srcOffset = 0, .dstOffset = byte_offset, .size = byte_size};
                vkCmdCopyBuffer(cmd, staging.Handle, global_buf.Handle, 1, &region);

                VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
                barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.buffer              = global_buf.Handle;
                barrier.offset              = byte_offset;
                barrier.size                = byte_size;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
            },
            render_timeline,
            render_value);

        m_device->GpuMem.FreeBuffer(staging);
    }

    BufferHandle RenderResourceManager::DoUploadMesh(AssetHandle asset, uint32_t frame_index)
    {
        AssetMesh* mesh = AssetManager::GetAsset<AssetMesh>(asset);
        if (!mesh || mesh->Vertices.empty())
            return {};

        size_t vert_bytes = mesh->Vertices.size() * sizeof(float);
        size_t idx_bytes  = mesh->Indices.size() * sizeof(uint32_t);

        if (m_vtx_cursor + vert_bytes > GLOBAL_VTX_CAPACITY || m_idx_cursor + idx_bytes > GLOBAL_IDX_CAPACITY)
        {
            ZENGINE_CORE_ERROR("[RRM] DoUploadMesh: global buffer capacity exceeded")
            return {};
        }

        AppendToGlobalBuffer(m_global_vertex_buf, mesh->Vertices.data(), vert_bytes, m_vtx_cursor, frame_index);
        AppendToGlobalBuffer(m_global_index_buf, mesh->Indices.data(), idx_bytes, m_idx_cursor, frame_index);

        static constexpr uint32_t FLOATS_PER_DRAW_VERTEX  = 8;                                      // x,y,z, nx,ny,nz, u,v
        static constexpr uint32_t DRAW_VERTEX_BYTES       = FLOATS_PER_DRAW_VERTEX * sizeof(float); // 32

        uint32_t                  vtx_elem_offset         = static_cast<uint32_t>(m_vtx_cursor / DRAW_VERTEX_BYTES); // DrawVertex[] index
        uint32_t                  idx_elem_offset         = static_cast<uint32_t>(m_idx_cursor / sizeof(uint32_t));  // uint32[] index
        uint32_t                  vtx_elem_count          = static_cast<uint32_t>(mesh->Vertices.size() / FLOATS_PER_DRAW_VERTEX);
        uint32_t                  idx_elem_count          = static_cast<uint32_t>(mesh->Indices.size());

        m_vtx_cursor                                     += vert_bytes;
        m_idx_cursor                                     += idx_bytes;

        uint32_t slot                                     = AllocMeshSlot();
        m_mesh_slots[slot].Data                           = {vtx_elem_offset, idx_elem_offset, vtx_elem_count, idx_elem_count};

        ZENGINE_CORE_INFO("[RRM] Uploaded mesh: {} verts ({} bytes), {} indices ({} bytes) — vtx@{} idx@{}", vtx_elem_count, vert_bytes, idx_elem_count, idx_bytes, vtx_elem_offset, idx_elem_offset)

        return {slot, m_mesh_slots[slot].Generation};
    }

    ImageHandle RenderResourceManager::DoUploadTexture(AssetHandle asset)
    {
        AssetTexture* tex = AssetManager::GetAsset<AssetTexture>(asset);
        if (!tex || tex->Path.empty())
            return {};

        if (!tex->Handle.Valid())
            return {};

        // Texture is already on GPU (via SubmitTextureFile/UploadTextureBuffer); register its slot.
        uint32_t slot_idx                  = AllocImageSlot();
        // We don't own a BufferImage for this path yet — store a sentinel.
        m_image_slots[slot_idx].Generation = slot_idx + 1;

        return {slot_idx, m_image_slots[slot_idx].Generation};
    }

    void RenderResourceManager::FlushPendingUploads(uint32_t frame_index)
    {
        uint32_t      count = 0;
        PendingUpload local[MAX_PENDING];
        {
            std::lock_guard lock(m_pending_mutex);
            count = m_pending_count;
            secure_memcpy(local, sizeof(local), m_pending, count * sizeof(PendingUpload));
            m_pending_count = 0;
        }

        for (uint32_t i = 0; i < count; ++i)
        {
            const PendingUpload& p = local[i];
            if (p.Kind == UploadKind::Mesh)
            {
                BufferHandle h = DoUploadMesh(p.Asset, frame_index);
                if (h.IsValid())
                {
                    std::lock_guard lock(m_uuid_map_mutex);
                    if (m_uuid_to_buffer_count < MAX_UUID_MAP)
                        m_uuid_to_buffer[m_uuid_to_buffer_count++] = {p.UUID, h};
                }
                else
                {
                    ZENGINE_CORE_ERROR("[RRM] Mesh upload failed for asset handle {}", p.Asset)
                }
            }
            else
            {
                ImageHandle h = DoUploadTexture(p.Asset);
                if (h.IsValid())
                {
                    std::lock_guard lock(m_uuid_map_mutex);
                    if (m_uuid_to_image_count < MAX_UUID_MAP)
                        m_uuid_to_image[m_uuid_to_image_count++] = {p.UUID, h};
                }
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

        // DEVICE_LOCAL — Ring staging → dedicated command buffer → vkQueueSubmit → fence wait.
        uint32_t       ring_offset = 0;
        void*          ring_ptr    = m_device->GpuMem.Ring.Allocate(static_cast<uint32_t>(byte_size), 4, &ring_offset);

        VkQueue        gfx_queue   = m_device->GetQueue(QueueType::GRAPHIC_QUEUE).Handle;
        CommandBuffer* upload_cmd  = m_upload_cmd;

        if (ring_ptr)
        {
            secure_memmove(ring_ptr, byte_size, data, byte_size);

            VkBuffer src_buf = m_device->GpuMem.Ring.Buffer;
            RecordAndSubmit(m_device->LogicalDevice, upload_cmd, m_upload_fence, gfx_queue, [&](VkCommandBuffer cmd) {
                VkBufferCopy region{.srcOffset = ring_offset, .dstOffset = dst_offset, .size = byte_size};
                vkCmdCopyBuffer(cmd, src_buf, dst.Handle, 1, &region);

                VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
                barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.buffer              = dst.Handle;
                barrier.offset              = dst_offset;
                barrier.size                = byte_size;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
            });

            m_device->GpuMem.Ring.Submit(ring_offset, static_cast<uint32_t>(byte_size), 0);
        }
        else
        {
            BufferView staging = m_device->CreateBuffer(static_cast<VkDeviceSize>(byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, GpuMemoryDomain::HostStaging);
            ZENGINE_VALIDATE_ASSERT(vmaCopyMemoryToAllocation(m_device->GpuMem.Allocator, data, staging.Allocation, 0, byte_size) == VK_SUCCESS, "RRM::UpdateBuffer: staging copy failed")

            VkBuffer stg_buf = staging.Handle;
            RecordAndSubmit(m_device->LogicalDevice, upload_cmd, m_upload_fence, gfx_queue, [&](VkCommandBuffer cmd) {
                VkBufferCopy region{.srcOffset = 0, .dstOffset = dst_offset, .size = byte_size};
                vkCmdCopyBuffer(cmd, stg_buf, dst.Handle, 1, &region);
            });

            DeferredFreeEntry e;
            e.EntryKind     = DeferredFreeEntry::Kind::Buffer;
            e.TimelineValue = m_device->SwapchainPtr->RenderTimelineNextValue;
            e.Data.Buffer   = staging;
            m_device->DeferFree(e);
        }
    }

    BufferHandle RenderResourceManager::UploadMesh(AssetHandle asset)
    {
        return DoUploadMesh(asset, m_current_frame);
    }

    ImageHandle RenderResourceManager::UploadTexture(AssetHandle asset)
    {
        return DoUploadTexture(asset);
    }

    void RenderResourceManager::ScheduleSwap(BufferHandle old_handle, AssetHandle new_asset)
    {
        // Hot-reload buffer swap not yet implemented.
        (void) old_handle;
        (void) new_asset;
    }

    void RenderResourceManager::ScheduleSwap(ImageHandle old_handle, AssetHandle new_asset)
    {
        // Hot-reload image swap not yet implemented.
        (void) old_handle;
        (void) new_asset;
    }

    bool RenderResourceManager::GetMeshOffsets(BufferHandle handle, uint32_t& vtx_offset, uint32_t& idx_offset) const
    {
        if (!handle.IsValid() || handle.Index >= m_mesh_slot_count)
            return false;
        const auto& slot = m_mesh_slots[handle.Index];
        if (slot.Generation != handle.Generation)
            return false;
        vtx_offset = slot.Data.VtxOffset;
        idx_offset = slot.Data.IdxOffset;
        return true;
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

    void RenderResourceManager::Release(ImageHandle handle)
    {
        BufferImage* slot = GetImageMutable(handle);
        if (!slot)
            return;

        DeferredFreeEntry e;
        e.EntryKind     = DeferredFreeEntry::Kind::Image;
        e.TimelineValue = m_device->SwapchainPtr->RenderTimelineNextValue;
        e.Data.Image    = *slot;
        m_device->DeferFree(e);

        *slot                                  = {};
        m_image_slots[handle.Index].Generation = 0;
    }

    const BufferImage* RenderResourceManager::GetImage(ImageHandle handle) const
    {
        if (!handle.IsValid() || handle.Index >= m_image_slot_count)
            return nullptr;
        const auto& slot = m_image_slots[handle.Index];
        return slot.Generation == handle.Generation ? &slot.Data : nullptr;
    }

    BufferImage* RenderResourceManager::GetImageMutable(ImageHandle handle)
    {
        if (!handle.IsValid() || handle.Index >= m_image_slot_count)
            return nullptr;
        auto& slot = m_image_slots[handle.Index];
        return slot.Generation == handle.Generation ? &slot.Data : nullptr;
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
                m_mesh_slots[i].Generation = i + 1;
                return i;
            }
        }
        ZENGINE_VALIDATE_ASSERT(m_mesh_slot_count < MAX_BUFFERS, "RRM: MAX_BUFFERS exceeded")
        uint32_t idx                 = m_mesh_slot_count++;
        m_mesh_slots[idx].Generation = idx + 1;
        return idx;
    }

    uint32_t RenderResourceManager::AllocImageSlot()
    {
        for (uint32_t i = 0; i < m_image_slot_count; ++i)
        {
            if (m_image_slots[i].Generation == 0)
            {
                m_image_slots[i].Generation = i + 1;
                return i;
            }
        }
        ZENGINE_VALIDATE_ASSERT(m_image_slot_count < MAX_IMAGES, "RRM: MAX_IMAGES exceeded")
        uint32_t idx                  = m_image_slot_count++;
        m_image_slots[idx].Generation = idx + 1;
        return idx;
    }

    uint32_t RenderResourceManager::AllocGBufSlot()
    {
        for (uint32_t i = 0; i < m_gbuf_slot_count; ++i)
        {
            if (m_gbuf_slots[i].Generation == 0)
            {
                m_gbuf_slots[i].Generation = (i + 1) | GBUF_GEN_TAG;
                return i;
            }
        }
        ZENGINE_VALIDATE_ASSERT(m_gbuf_slot_count < MAX_GENERIC_BUFS, "RRM: MAX_GENERIC_BUFS exceeded")
        uint32_t idx                 = m_gbuf_slot_count++;
        m_gbuf_slots[idx].Generation = (idx + 1) | GBUF_GEN_TAG;
        return idx;
    }

    BufferHandle RenderResourceManager::UploadBuffer(const void* data, size_t byte_size, VkBufferUsageFlags usage, const char* debug_name)
    {
        if (!data || byte_size == 0)
            return {};

        Core::Memory::BufferView buf = m_device->GpuMem.AllocateBuffer(static_cast<VkDeviceSize>(byte_size), usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, Core::Memory::GpuMemoryDomain::DeviceGeometry, debug_name ? debug_name : "RRM::UploadBuffer");

        if (!buf)
            return {};

        AppendToGlobalBuffer(buf, data, byte_size, 0, 0);

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
        auto     img_buf        = m_device->Image2DBufferManager.Access(texture->BufferHandle);
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
                return handle;
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
            img_buf->Layout                                  = to_transfer.NewLayout;

            BufferView                      transfer_staging = m_device->WriteTextureData(transfer_cmd, handle, data);

            ImageMemoryBarrierSpecification release          = {};
            release.ImageHandle                              = buffer_handle;
            release.OldLayout                                = ImageLayout::TRANSFER_DST_OPTIMAL;
            release.NewLayout                                = (img_buf_aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : ImageLayout::SHADER_READ_ONLY_OPTIMAL;
            release.ImageAspectMask                          = VkImageAspectFlagBits(img_buf_aspect);
            release.SourceAccessMask                         = VK_ACCESS_TRANSFER_WRITE_BIT;
            release.DestinationAccessMask                    = VK_ACCESS_NONE;
            release.SourceStageMask                          = VK_PIPELINE_STAGE_TRANSFER_BIT;
            release.DestinationStageMask                     = (img_buf_aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            release.LayerCount                               = texture->Specification.LayerCount;
            release.SourceQueueFamily                        = m_device->TransferFamilyIndex;
            release.DestinationQueueFamily                   = m_device->GraphicFamilyIndex;
            transfer_cmd->TransitionImageLayout(ImageMemoryBarrier{release});
            img_buf->Layout = release.NewLayout;
            transfer_cmd->End();

            uint64_t transfer_val = m_tex_transfer_next_values[pool_index].fetch_add(1, std::memory_order_acq_rel);
            transfer_retire[i]    = transfer_val;
            if (transfer_staging)
                m_tex_transfer_staging[pool_index][i] = transfer_staging;

            m_tex_job_queue.Enqueue({transfer_cmd, m_tex_transfer_timelines[pool_index], nullptr, VK_PIPELINE_STAGE_TRANSFER_BIT, transfer_val, UINT64_MAX});

            uint32_t acquire_slot  = 0;
            auto&    retire_values = m_tex_retire_values[pool_index];
            for (; acquire_slot < GEOMETRY_UPLOAD_SLOT; ++acquire_slot)
                if (retire_values[acquire_slot] == 0)
                    break;
            if (acquire_slot >= GEOMETRY_UPLOAD_SLOT)
                return handle;

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
            m_tex_job_queue.Enqueue({acquire_cmd, m_tex_timelines[pool_index], m_tex_transfer_timelines[pool_index], (uint32_t) release.DestinationStageMask, graphics_val, transfer_val});
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
                return handle;
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
            img_buf->Layout                          = to_transfer.NewLayout;

            BufferView                      staging  = m_device->WriteTextureData(cmd, handle, data);

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
            m_tex_job_queue.Enqueue({cmd, m_tex_timelines[pool_index], nullptr, (uint32_t) to_final.DestinationStageMask, signal_value, UINT64_MAX});
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
        auto                            img_buf       = m_device->Image2DBufferManager.Access(texture->BufferHandle);
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

        auto* cmd                                     = m_upload_cmd;
        cmd->ResetState();
        vkResetCommandBuffer(cmd->GetHandle(), 0);
        cmd->Begin();
        cmd->TransitionImageLayout(ImageMemoryBarrier{to_transfer});
        img_buf->Layout    = to_transfer.NewLayout;
        BufferView staging = m_device->WriteTextureData(cmd, handle, pixels);
        cmd->TransitionImageLayout(ImageMemoryBarrier{to_final});
        img_buf->Layout = to_final.NewLayout;
        cmd->End();

        VkQueue         gfx_queue = m_device->GetQueue(QueueType::GRAPHIC_QUEUE).Handle;
        VkCommandBuffer raw       = cmd->GetHandle();
        VkSubmitInfo    submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &raw;

        vkWaitForFences(m_device->LogicalDevice, 1, &m_upload_fence, VK_TRUE, UINT64_MAX);
        vkResetFences(m_device->LogicalDevice, 1, &m_upload_fence);
        vkQueueSubmit(gfx_queue, 1, &submit, m_upload_fence);
        vkWaitForFences(m_device->LogicalDevice, 1, &m_upload_fence, VK_TRUE, UINT64_MAX);
        cmd->ResetState();

        if (staging)
            m_device->GpuMem.FreeBuffer(staging);

        return handle;
    }

    void RenderResourceManager::EnqueueTextureDeferral(TextureDeferral&& deferral)
    {
        m_tex_deferral_queue.Emplace(std::forward<TextureDeferral>(deferral));
    }

    void RenderResourceManager::CompleteDeferrals()
    {
        while (!m_tex_deferral_queue.Empty())
        {
            TextureDeferral d = {};
            m_tex_deferral_queue.Pop(d);
            if (d.IsLarge)
            {
                auto& buf = std::get<std::vector<uint8_t>>(d.Buffer);
                UploadTextureBuffer(d.FrameIdx, d.ThreadIdx, d.TexHandle, buf.data());
            }
            else
            {
                auto& buf = std::get<unsigned char*>(d.Buffer);
                UploadTextureBuffer(d.FrameIdx, d.ThreadIdx, d.TexHandle, buf);
            }
        }
    }

    void RenderResourceManager::SubmitTextureJobs()
    {
        while (!m_tex_job_queue.Empty())
        {
            TextureTimelineJob job;
            if (m_tex_job_queue.Pop(job))
            {
                m_device->QueueSubmit(job.Buffer, job.Timeline, job.WaitFlag, job.SignalValue, job.WaitValue, job.WaitTimeline);
                m_device->EnqueueAsyncGPUOperation({job.WaitFlag, job.SignalValue, job.Timeline});
            }
        }
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

    void RenderResourceManager::ClearTextureJobs()
    {
        m_tex_job_queue.Clear();
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

    Rendering::Textures::TextureHandle RenderResourceManager::SubmitTextureFile(uint8_t frame_index, uint8_t thread_index, const char* filename)
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

        spec.BytePerPixel                                    = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(spec.Format)];
        auto                               tex_handle        = m_device->CreateTexture(spec);

        // Capture everything by value for the thread pool lambda.
        std::string                        captured_filename = abs_filename;
        std::string                        captured_ext      = file_ext;
        TextureSpecification               captured_spec     = spec;
        uint8_t                            fi = frame_index, ti = thread_index;
        Rendering::Textures::TextureHandle captured_handle = tex_handle;

        Helpers::ThreadPoolHelper::Submit([this, captured_filename, captured_ext, captured_spec, fi, ti, captured_handle]() mutable {
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
                    size_t bytes = cubemap.Buffer.size();
                    buffer.resize(bytes);
                    Helpers::secure_memmove(buffer.data(), bytes, cubemap.Buffer.data(), bytes);
                }
                else
                {
                    int          w, h, ch;
                    const float* image_data = stbi_loadf(captured_filename.c_str(), &w, &h, &ch, 4);
                    if (!image_data)
                    {
                        ZENGINE_CORE_ERROR("Failed to load texture: {}", captured_filename) return;
                    }

                    std::vector<float> output_buf;
                    if (ch == STBI_rgb)
                    {
                        size_t total = w * h;
                        output_buf.resize(total * 4);
                        stbir_resize_float(image_data, w, h, 0, output_buf.data(), w, h, 0, 4);
                        for (size_t i = 0; i < total; ++i)
                            output_buf[i * 4 + 3] = 255.f;
                    }
                    else
                    {
                        output_buf.resize((size_t) (w * h * ch));
                    }
                    stbi_image_free((void*) image_data);

                    Rendering::Buffers::Bitmap in             = {w, h, 4, Rendering::Buffers::BitmapFormat::FLOAT, output_buf.data()};
                    Rendering::Buffers::Bitmap vertical_cross = Rendering::Buffers::Bitmap::EquirectangularMapToVerticalCross(in);
                    Rendering::Buffers::Bitmap cubemap        = Rendering::Buffers::Bitmap::VerticalCrossToCubemap(vertical_cross);

                    size_t                     bytes          = cubemap.Buffer.size();
                    buffer.resize(bytes);
                    Helpers::secure_memmove(buffer.data(), bytes, cubemap.Buffer.data(), bytes);
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

            TextureDeferral deferral;
            deferral.FrameIdx  = fi;
            deferral.ThreadIdx = ti;
            deferral.TexHandle = captured_handle;
            deferral.Buffer    = std::move(buffer);
            deferral.IsLarge   = true;
            EnqueueTextureDeferral(std::move(deferral));
            m_device->TextureHandleToUpdates.Enqueue(captured_handle);
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

        return SubmitTextureFile(0, 0, kFallbackPath);
    }

} // namespace ZEngine::Rendering
