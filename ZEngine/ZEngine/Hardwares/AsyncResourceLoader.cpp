#include <AsyncResourceLoader.h>
#include <Helpers/ThreadPool.h>
#include <Importers/EnvironmentMapImporter.h>
#include <Rendering/Buffers/Bitmap.h>
#include <VulkanDevice.h>

#define STB_IMAGE_IMPLEMENTATION
#ifdef __GNUC__
#define STBI_NO_SIMD
#endif
#include <stb/stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/deprecated/stb_image_resize.h>
#include <stb/stb_image_write.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Rendering;
using namespace ZEngine::Rendering::Primitives;
using namespace ZEngine::Helpers;

namespace ZEngine::Hardwares
{

    void AsyncResourceLoader::Initialize(VulkanDevice* device)
    {
        Device                  = device;
        TotalCommandBufferCount = Device->CommandBufferMgr->MaxBufferPerPool * Device->CommandBufferMgr->MaxBufferPerPool;

        Timelines.init(Device->Arena, Device->CommandBufferMgr->TotalPoolCount, Device->CommandBufferMgr->TotalPoolCount);
        NextValues.init(Device->Arena, Device->CommandBufferMgr->TotalPoolCount, Device->CommandBufferMgr->TotalPoolCount);
        RetireValues.init(Device->Arena, Device->CommandBufferMgr->TotalPoolCount, Device->CommandBufferMgr->TotalPoolCount);

        for (uint32_t i = 0; i < Device->CommandBufferMgr->TotalPoolCount; ++i)
        {
            Timelines[i] = ZPushStructCtorArgs(Device->Arena, Rendering::Primitives::Semaphore, Device, true);
            RetireValues[i].init(Device->Arena, TotalCommandBufferCount, TotalCommandBufferCount);
            NextValues[i].store(1, std::memory_order_release);
        }

        if (Device->HasSeperateTransfertQueueFamily)
        {
            TransferTimelines.init(Device->Arena, Device->CommandBufferMgr->TotalPoolCount, Device->CommandBufferMgr->TotalPoolCount);
            TransferNextValues.init(Device->Arena, Device->CommandBufferMgr->TotalPoolCount, Device->CommandBufferMgr->TotalPoolCount);
            TransferRetireValues.init(Device->Arena, Device->CommandBufferMgr->TotalPoolCount, Device->CommandBufferMgr->TotalPoolCount);

            for (uint32_t i = 0; i < Device->CommandBufferMgr->TotalPoolCount; ++i)
            {
                TransferTimelines[i] = ZPushStructCtorArgs(Device->Arena, Rendering::Primitives::Semaphore, Device, true);
                TransferRetireValues[i].init(Device->Arena, TotalCommandBufferCount, TotalCommandBufferCount);
                TransferNextValues[i].store(1, std::memory_order_release);
            }
        }
    }

    void AsyncResourceLoader::Submit(UploadType type, uint8_t frame_index, uint8_t thread_index, const UploadRequest& request)
    {
        switch (type)
        {
            case UploadType::TEXTURE_BUFFER:
            case UploadType::TEXTURE_BUFFER_LARGE:
                UploadTextureBuffer(frame_index, thread_index, request.TextureUpload.TexHandle, request.TextureUpload.Data);
                break;

            case UploadType::BUFFER:
                UploadBuffer(frame_index, thread_index, request.BufferUpload.Buffer, request.BufferUpload.Data, request.BufferUpload.Offset, request.BufferUpload.ByteSize);
                break;

            case UploadType::STAGING_BUFFER:
                UploadFromStagingBuffer(frame_index, thread_index, request.BufferUpload.Buffer, request.BufferUpload.Data, request.BufferUpload.Offset, request.BufferUpload.ByteSize);
                break;

            case UploadType::BUFFER_CLEAR:
                ClearBuffer(frame_index, thread_index, request.BufferUpload.Buffer, request.BufferUpload.Offset, request.BufferUpload.ByteSize, request.BufferUpload.ClearValue);
                break;

            default:
                break;
        }
    }

    void AsyncResourceLoader::CompleteDeferrals()
    {
        while (!DeferralUploadQueue.Empty())
        {
            DeferralUpload deferral = {};
            DeferralUploadQueue.Pop(deferral);
            if (deferral.Type == UploadType::TEXTURE_BUFFER_LARGE)
            {
                auto& buf = std::get<std::vector<uint8_t>>(deferral.Buffer);
                Submit(
                    deferral.Type,
                    deferral.FrameIdx,
                    deferral.ThreadIdx,
                    UploadRequest{
                    .TextureUpload = {.Data = buf.data(), .TexHandle = deferral.TexHandle}
                });
            }
            else if (deferral.Type == UploadType::TEXTURE_BUFFER)
            {
                auto& buf = std::get<unsigned char*>(deferral.Buffer);
                Submit(
                    deferral.Type,
                    deferral.FrameIdx,
                    deferral.ThreadIdx,
                    UploadRequest{
                    .TextureUpload = {.Data = buf, .TexHandle = deferral.TexHandle}
                });
            }
        }
    }

    void AsyncResourceLoader::SubmitDeferral(DeferralUpload&& deferral)
    {
        DeferralUploadQueue.Emplace(std::forward<DeferralUpload>(deferral));
    }

    void AsyncResourceLoader::UploadBuffer(uint8_t frame_index, uint8_t thread_index, BufferView* const buffer_view, const void* data, uint32_t offset, size_t byte_size)
    {
        if (!buffer_view || !(*buffer_view) || !data || byte_size == 0)
        {
            return;
        }

        VkMemoryPropertyFlags mem_prop_flags;
        vmaGetAllocationMemoryProperties(Device->VmaAllocatorValue, buffer_view->Allocation, &mem_prop_flags);

        if (mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        {
            ZENGINE_VALIDATE_ASSERT(vmaCopyMemoryToAllocation(Device->VmaAllocatorValue, data, buffer_view->Allocation, offset, byte_size) == VK_SUCCESS, "Failed to perform memory copy operation")

            // flushing the allocation so the GPU can see it
            if (!(mem_prop_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            {
                vmaFlushAllocation(Device->VmaAllocatorValue, buffer_view->Allocation, offset, byte_size);
            }

            VkAccessFlags        dst_access_mask    = VK_ACCESS_NONE;
            VkPipelineStageFlags dst_pipeline_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            switch (buffer_view->Type)
            {
                case BufferType::VERTEX:
                    dst_access_mask    = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
                    dst_pipeline_stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
                    break;

                case BufferType::INDEX:
                    dst_access_mask    = VK_ACCESS_INDEX_READ_BIT;
                    dst_pipeline_stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
                    break;

                case BufferType::UNIFORM:
                    dst_access_mask    = VK_ACCESS_UNIFORM_READ_BIT;
                    dst_pipeline_stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
                    break;

                case BufferType::STORAGE:
                    dst_access_mask    = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                    dst_pipeline_stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                    break;

                case BufferType::INDIRECT:
                    dst_access_mask    = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
                    dst_pipeline_stage = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
                    break;
                default:
                    dst_pipeline_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                    break;
            }

            uint32_t i             = 0;
            uint32_t pool_index    = (frame_index * Device->CommandBufferMgr->TotalThreadCount) + thread_index;
            auto&    retire_values = RetireValues[pool_index];

            for (; i < retire_values.size(); ++i)
            {
                if (retire_values[i] == 0)
                {
                    break;
                }
            }

            auto                  command_buffer = Device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE, frame_index, thread_index, i);

            VkBufferMemoryBarrier bufMemBarrier  = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            bufMemBarrier.srcAccessMask          = VK_ACCESS_HOST_WRITE_BIT;
            bufMemBarrier.dstAccessMask          = dst_access_mask;
            bufMemBarrier.srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
            bufMemBarrier.dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
            bufMemBarrier.buffer                 = buffer_view->Handle;
            bufMemBarrier.offset                 = offset;
            bufMemBarrier.size                   = byte_size;

            // It's important to insert a buffer memory barrier here to ensure writing to the buffer has finished.
            vkCmdPipelineBarrier(command_buffer->GetHandle(), VK_PIPELINE_STAGE_HOST_BIT, dst_pipeline_stage, 0, 0, nullptr, 1, &bufMemBarrier, 0, nullptr);

            command_buffer->End();

            uint64_t signal_value = NextValues[pool_index].fetch_add(1, std::memory_order_acq_rel);
            retire_values[i]      = signal_value;
            AsyncTimelineJobQueue.Enqueue({command_buffer, Timelines[pool_index], nullptr, dst_pipeline_stage, signal_value});
        }
        else
        {
            UploadFromStagingBuffer(frame_index, thread_index, buffer_view, data, offset, byte_size);
        }
    }

    void AsyncResourceLoader::UploadFromStagingBuffer(uint8_t frame_index, uint8_t thread_index, BufferView* const destination, const void* data, uint32_t offset, size_t byte_size)
    {
        if (!destination || !(*destination) || !data || byte_size == 0)
        {
            return;
        }

        uint32_t i             = 0;
        uint32_t pool_index    = (frame_index * Device->CommandBufferMgr->TotalThreadCount) + thread_index;
        auto&    retire_values = RetireValues[pool_index];

        for (; i < retire_values.size(); ++i)
        {
            if (retire_values[i] == 0)
            {
                break;
            }
        }

        auto       command_buffer = Device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE, frame_index, thread_index, i);

        BufferView staging_buffer = Device->CreateBuffer(static_cast<VkDeviceSize>(byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

        ZENGINE_VALIDATE_ASSERT(vmaCopyMemoryToAllocation(Device->VmaAllocatorValue, data, staging_buffer.Allocation, offset, byte_size) == VK_SUCCESS, "Failed to perform memory copy operation")

        auto dst_pipeline_stage = Device->CopyBuffer(command_buffer, staging_buffer, *destination, byte_size, 0u, offset);

        command_buffer->End();

        uint64_t signal_value = NextValues[pool_index].fetch_add(1, std::memory_order_acq_rel);
        retire_values[i]      = signal_value;
        AsyncTimelineJobQueue.Enqueue({command_buffer, Timelines[pool_index], nullptr, dst_pipeline_stage, signal_value});
        Device->EnqueueBufferForDeletion(staging_buffer);
    }

    void AsyncResourceLoader::ClearBuffer(uint8_t frame_index, uint8_t thread_index, BufferView* const buffer_view, uint32_t offset, size_t byte_size, uint32_t clear_value)
    {
        if (!buffer_view || byte_size == 0)
        {
            return;
        }

        VkMemoryPropertyFlags mem_prop_flags;
        vmaGetAllocationMemoryProperties(Device->VmaAllocatorValue, buffer_view->Allocation, &mem_prop_flags);

        if (mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        {
            VmaAllocationInfo allocation_info = {};
            vmaGetAllocationInfo(Device->VmaAllocatorValue, buffer_view->Allocation, &allocation_info);
            if (allocation_info.pMappedData)
            {
                auto mapped_buf = reinterpret_cast<uint8_t*>(allocation_info.pMappedData);
                ZENGINE_VALIDATE_ASSERT(Helpers::secure_memset((mapped_buf + offset), clear_value, allocation_info.size, byte_size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
            }
        }
        else
        {
            BufferView        staging_buffer  = Device->CreateBuffer(static_cast<VkDeviceSize>(byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

            VmaAllocationInfo allocation_info = {};
            vmaGetAllocationInfo(Device->VmaAllocatorValue, staging_buffer.Allocation, &allocation_info);

            if (allocation_info.pMappedData)
            {

                uint32_t i             = 0;
                uint32_t pool_index    = (frame_index * Device->CommandBufferMgr->TotalThreadCount) + thread_index;
                auto&    retire_values = RetireValues[pool_index];

                for (; i < retire_values.size(); ++i)
                {
                    if (retire_values[i] == 0)
                    {
                        break;
                    }
                }

                auto command_buffer = Device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE, frame_index, thread_index, i);
                ZENGINE_VALIDATE_ASSERT(Helpers::secure_memset(allocation_info.pMappedData, clear_value, allocation_info.size, byte_size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
                ZENGINE_VALIDATE_ASSERT(vmaFlushAllocation(Device->VmaAllocatorValue, staging_buffer.Allocation, 0, byte_size) == VK_SUCCESS, "Failed to flush allocation")

                auto dst_pipeline_stage = Device->CopyBuffer(command_buffer, staging_buffer, *buffer_view, byte_size, 0u, offset);

                command_buffer->End();
                uint64_t signal_value = NextValues[pool_index].fetch_add(1, std::memory_order_acq_rel);
                retire_values[i]      = signal_value;
                AsyncTimelineJobQueue.Enqueue({command_buffer, Timelines[pool_index], nullptr, dst_pipeline_stage, signal_value});
            }

            /* Cleanup resource */
            Device->EnqueueBufferForDeletion(staging_buffer);
        }
    }

    void AsyncResourceLoader::UploadTextureBuffer(uint8_t frame_index, uint8_t thread_index, const Rendering::Textures::TextureHandle& handle, unsigned char* data)
    {
        if (!handle.Valid() || !data)
            return;

        uint32_t pool_index     = (frame_index * Device->CommandBufferMgr->TotalThreadCount) + thread_index;

        auto     texture        = Device->GlobalTextures.Access(handle);
        auto     img_buf        = Device->Image2DBufferManager.Access(texture->BufferHandle);
        auto     img_buf_aspect = (texture->Specification.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        auto     buffer_handle  = img_buf->GetHandle();

        if (Device->HasSeperateTransfertQueueFamily)
        {
            auto&    transfer_retire_values = TransferRetireValues[pool_index];

            uint32_t i                      = 0;
            for (; i < transfer_retire_values.size(); ++i)
            {
                if (transfer_retire_values[i] == 0)
                    break;
            }

            auto                                            transfer_cmd = Device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::TRANSFER_QUEUE, frame_index, thread_index, i);

            // 2. Transition to TRANSFER_DST_OPTIMAL
            Specifications::ImageMemoryBarrierSpecification to_transfer  = {};
            to_transfer.ImageHandle                                      = buffer_handle;
            to_transfer.OldLayout                                        = img_buf->Layout;
            to_transfer.NewLayout                                        = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
            to_transfer.ImageAspectMask                                  = VkImageAspectFlagBits(img_buf_aspect);
            to_transfer.SourceAccessMask                                 = VK_ACCESS_NONE;
            to_transfer.DestinationAccessMask                            = VK_ACCESS_TRANSFER_WRITE_BIT;
            to_transfer.SourceStageMask                                  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            to_transfer.DestinationStageMask                             = VK_PIPELINE_STAGE_TRANSFER_BIT;
            to_transfer.LayerCount                                       = texture->Specification.LayerCount;
            to_transfer.SourceQueueFamily                                = Device->TransferFamilyIndex;
            to_transfer.DestinationQueueFamily                           = Device->TransferFamilyIndex;
            transfer_cmd->TransitionImageLayout(Primitives::ImageMemoryBarrier{to_transfer});

            img_buf->Layout = to_transfer.NewLayout;

            // 3. Copy data to image
            Device->WriteTextureData(transfer_cmd, handle, data);

            // Release barrier: transfer → graphics ownership
            Specifications::ImageMemoryBarrierSpecification release = {};
            release.ImageHandle                                     = buffer_handle;
            release.OldLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
            release.NewLayout                                       = (img_buf_aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? Specifications::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : Specifications::ImageLayout::SHADER_READ_ONLY_OPTIMAL;
            release.ImageAspectMask                                 = VkImageAspectFlagBits(img_buf_aspect);
            release.SourceAccessMask                                = VK_ACCESS_TRANSFER_WRITE_BIT;
            release.DestinationAccessMask                           = VK_ACCESS_NONE; // Must be 0 for release
            release.SourceStageMask                                 = VK_PIPELINE_STAGE_TRANSFER_BIT;
            release.DestinationStageMask                            = (img_buf_aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            release.LayerCount                                      = texture->Specification.LayerCount;
            release.SourceQueueFamily                               = Device->TransferFamilyIndex;
            release.DestinationQueueFamily                          = Device->GraphicFamilyIndex;

            transfer_cmd->TransitionImageLayout(ImageMemoryBarrier{release});
            img_buf->Layout = release.NewLayout;
            transfer_cmd->End();

            uint64_t transfer_val               = TransferNextValues[pool_index].fetch_add(1, std::memory_order_acq_rel);
            TransferRetireValues[pool_index][i] = transfer_val;

            AsyncTimelineJobQueue.Enqueue({
                transfer_cmd,
                TransferTimelines[pool_index],
                nullptr,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                transfer_val,
                UINT64_MAX // no wait value
            });

            uint32_t acquire_slot  = 0;
            auto&    retire_values = RetireValues[pool_index];
            for (; acquire_slot < retire_values.size(); ++acquire_slot)
            {
                if (retire_values[acquire_slot] == 0)
                {
                    break;
                }
            }

            auto                            acquire_cmd  = Device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE, frame_index, thread_index, acquire_slot);

            // Acquire barrier: graphics takes ownership
            ImageMemoryBarrierSpecification acquire_spec = release;        // same image/layout params
            acquire_spec.SourceAccessMask                = VK_ACCESS_NONE; // Must be 0 for acquire
            acquire_spec.DestinationAccessMask           = VK_ACCESS_SHADER_READ_BIT;
            acquire_spec.SourceQueueFamily               = Device->TransferFamilyIndex;
            acquire_spec.DestinationQueueFamily          = Device->GraphicFamilyIndex;

            acquire_cmd->TransitionImageLayout(ImageMemoryBarrier{acquire_spec});
            acquire_cmd->End();

            uint64_t graphics_val       = NextValues[pool_index].fetch_add(1, std::memory_order_acq_rel);
            retire_values[acquire_slot] = graphics_val;

            AsyncTimelineJobQueue.Enqueue({acquire_cmd, Timelines[pool_index], TransferTimelines[pool_index], (uint32_t) release.DestinationStageMask, graphics_val, transfer_val});

            img_buf->Layout = release.NewLayout;
        }
        else
        {
            auto&    retire_values = RetireValues[pool_index];

            uint32_t i             = 0;
            for (; i < retire_values.size(); ++i)
            {
                if (retire_values[i] == 0)
                    break;
            }
            auto                            cmd         = Device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE, frame_index, thread_index, i);

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
            to_transfer.SourceQueueFamily               = Device->GraphicFamilyIndex;
            to_transfer.DestinationQueueFamily          = Device->GraphicFamilyIndex;

            cmd->TransitionImageLayout(ImageMemoryBarrier{to_transfer});
            img_buf->Layout = to_transfer.NewLayout;

            Device->WriteTextureData(cmd, handle, data);

            // Single Queue: No ownership transfer needed. Just a normal barrier.
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
            to_final.SourceQueueFamily               = Device->GraphicFamilyIndex;
            to_final.DestinationQueueFamily          = Device->GraphicFamilyIndex;

            cmd->TransitionImageLayout(ImageMemoryBarrier{to_final});
            cmd->End();

            uint64_t signal_value = NextValues[pool_index].fetch_add(1, std::memory_order_acq_rel);
            retire_values[i]      = signal_value;
            AsyncTimelineJobQueue.Enqueue({cmd, Timelines[pool_index], nullptr, (uint32_t) to_final.DestinationStageMask, signal_value, UINT64_MAX});
            img_buf->Layout = to_final.NewLayout;
        }
    }

    Textures::TextureHandle AsyncResourceLoader::Submit(uint8_t frame_index, uint8_t thread_index, const UploadRequest& request)
    {
        std::unique_lock                     l(m_mutex);

        auto                                 abs_filename = std::filesystem::absolute(request.TextureUpload.Filename).string();
        auto                                 file_ext     = std::filesystem::path(abs_filename).extension().string();

        Specifications::TextureSpecification spec{};

        if (file_ext == ".zenvmap")
        {
            Importers::EnvironmentMapFileHeader env_header{};
            if (!Importers::EnvironmentMapImporter::ReadHeader(abs_filename.c_str(), env_header))
            {
                ZENGINE_CORE_ERROR("Failed to read .zenvmap header: {}", abs_filename)
                return {};
            }

            spec.IsCubemap  = true;
            spec.LayerCount = static_cast<uint32_t>(env_header.LayerCount);
            spec.Format     = Specifications::ImageFormat::R32G32B32A32_SFLOAT;
            spec.Width      = static_cast<uint32_t>(env_header.FaceWidth);
            spec.Height     = static_cast<uint32_t>(env_header.FaceHeight);
        }
        else
        {
            int w, h, ch;
            if (!stbi_info(abs_filename.c_str(), &w, &h, &ch))
            {
                return {};
            }

            const std::set<std::string_view> known_cubmap_file_ext = {".hdr", ".exr"};
            spec.Width                                             = static_cast<uint32_t>(w);
            spec.Height                                            = static_cast<uint32_t>(h);
            spec.Format                                            = Specifications::ImageFormat::R8G8B8A8_SRGB;

            if (known_cubmap_file_ext.contains(file_ext))
            {
                int face_size   = w / 4;

                spec.IsCubemap  = true;
                spec.LayerCount = 6;
                spec.Format     = Specifications::ImageFormat::R32G32B32A32_SFLOAT;

                spec.Width      = static_cast<uint32_t>(face_size);
                spec.Height     = static_cast<uint32_t>(face_size);
            }
        }

        TextureFileRequest tex_file_req       = {};
        tex_file_req.Filename                 = request.TextureUpload.Filename;
        tex_file_req.TextureSpec              = spec;
        tex_file_req.TextureSpec.BytePerPixel = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(spec.Format)];
        tex_file_req.Handle                   = Device->CreateTexture(tex_file_req.TextureSpec);
        tex_file_req.FrameIdx                 = frame_index;
        tex_file_req.ThreadIdx                = thread_index;

        m_file_requests.Enqueue(tex_file_req);

        ThreadPoolHelper::Submit([this] { Run(); });

        return tex_file_req.Handle;
    }

    void AsyncResourceLoader::ClearAsyncJobs()
    {
        AsyncTimelineJobQueue.Clear();
        Device->AsyncGPUOperations.Clear();
    }

    void AsyncResourceLoader::SubmitAsyncJobs()
    {
        while (!AsyncTimelineJobQueue.Empty())
        {
            TimelineJob job;
            if (AsyncTimelineJobQueue.Pop(job))
            {
                Device->QueueSubmit(job.Buffer, job.Timeline, job.WaitFlag, job.SignalValue, job.WaitValue, job.WaitTimeline);
                Device->EnqueueAsyncGPUOperation({job.WaitFlag, job.SignalValue, job.Timeline});
            }
        }
    }

    void AsyncResourceLoader::ResetCommandBuffers(uint8_t frame_index, uint8_t thread_index)
    {
        uint32_t pool_index     = (frame_index * Device->CommandBufferMgr->TotalThreadCount) + thread_index;

        uint64_t graphics_value = 0;
        auto&    retire_values  = RetireValues[pool_index];
        vkGetSemaphoreCounterValue(Device->LogicalDevice, Timelines[pool_index]->GetHandle(), &graphics_value);

        for (int i = 0; i < retire_values.size(); ++i)
        {
            auto retire_val = retire_values[i];
            if (retire_val != 0 && graphics_value >= retire_val)
            {
                auto command_buffer = Device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE, frame_index, thread_index, i, false);
                command_buffer->ResetState();
                vkResetCommandBuffer(command_buffer->GetHandle(), 0);
                retire_values[i] = 0;
            }
        }

        if (Device->HasSeperateTransfertQueueFamily)
        {
            uint64_t transfer_value  = 0;
            auto&    transfer_retire = TransferRetireValues[pool_index];
            vkGetSemaphoreCounterValue(Device->LogicalDevice, TransferTimelines[pool_index]->GetHandle(), &transfer_value);

            for (int i = 0; i < transfer_retire.size(); ++i)
            {
                auto transfer_retire_val = transfer_retire[i];
                if (transfer_retire_val != 0 && transfer_value >= transfer_retire_val)
                {
                    auto transfer_cmd = Device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::TRANSFER_QUEUE, frame_index, thread_index, i, false);
                    transfer_cmd->ResetState();
                    vkResetCommandBuffer(transfer_cmd->GetHandle(), 0);
                    transfer_retire[i] = 0;
                }
            }
        }
    }

    void AsyncResourceLoader::Run()
    {
        while (m_cancellation_token.load(std::memory_order_acquire) == false)
        {
            if (m_file_requests.Empty() && m_upload_requests.Empty())
            {
                break;
            }

            // Processing upload requests
            if (m_upload_requests.Size())
            {
                TextureUploadRequest upload_request;
                if (m_upload_requests.Pop(upload_request))
                {
                    DeferralUpload deferral = {
                        .Type      = AsyncResourceLoader::UploadType::TEXTURE_BUFFER_LARGE,
                        .FrameIdx  = upload_request.FrameIdx,
                        .ThreadIdx = upload_request.ThreadIdx,
                        .Buffer    = std::move(upload_request.Buffer),
                        .TexHandle = upload_request.Handle,
                    };
                    SubmitDeferral(std::move(deferral));
                    Device->TextureHandleToUpdates.Enqueue(upload_request.Handle);
                }
            }

            // Processing file requests
            if (m_file_requests.Size())
            {
                TextureFileRequest file_request;
                if (m_file_requests.Pop(file_request))
                {
                    TextureUploadRequest upload_req = {};

                    int                  width = 0, height = 0, channel = 0;
                    stbi_set_flip_vertically_on_load(1);

                    if (file_request.TextureSpec.IsCubemap)
                    {
                        auto cubemap_ext = std::filesystem::path(file_request.Filename.data()).extension().string();

                        if (cubemap_ext == ".zenvmap")
                        {
                            Buffers::Bitmap cubemap{};
                            if (!Importers::EnvironmentMapImporter::Deserialize(file_request.Filename.data(), cubemap))
                            {
                                ZENGINE_CORE_ERROR("Failed to deserialize .zenvmap: {}", file_request.Filename.data())
                                continue;
                            }

                            size_t buffer_byte = cubemap.Buffer.size() * sizeof(uint8_t);
                            upload_req.Buffer.resize(cubemap.Buffer.size());
                            Helpers::secure_memmove(upload_req.Buffer.data(), buffer_byte, cubemap.Buffer.data(), buffer_byte);
                        }
                        else
                        {
                            const float* image_data = stbi_loadf(file_request.Filename.data(), &width, &height, &channel, 4);
                            if (!image_data)
                            {
                                ZENGINE_CORE_ERROR("Failed to load texture file : {0}", file_request.Filename.data())
                                continue;
                            }

                            bool               perform_convert_rgb_to_rgba = (channel == STBI_rgb);

                            std::vector<float> output_buffer               = {};
                            if (perform_convert_rgb_to_rgba)
                            {
                                size_t total_pixel = width * height;
                                size_t buffer_size = total_pixel * 4;
                                output_buffer.resize(buffer_size);
                                stbir_resize_float(image_data, width, height, 0, output_buffer.data(), width, height, 0, 4);

                                for (int i = 0; i < total_pixel; ++i)
                                {
                                    int offset = i * 4;

                                    if (channel == 1)
                                    {
                                        output_buffer[offset + 3] = 255;
                                    }
                                    else if (channel == 2)
                                    {
                                        output_buffer[offset + 3] = image_data[i * 2 + 1];
                                    }
                                    else if (channel == 3)
                                    {
                                        output_buffer[offset + 3] = 255;
                                    }
                                }
                            }
                            else
                            {
                                size_t total_pixel = width * height;
                                size_t buffer_size = total_pixel * channel;
                                output_buffer.resize(buffer_size);
                                Helpers::secure_memset(output_buffer.data(), 0.f, buffer_size, buffer_size);
                            }

                            stbi_image_free((void*) image_data);

                            Buffers::Bitmap in             = {width, height, 4, Buffers::BitmapFormat::FLOAT, output_buffer.data()};
                            Buffers::Bitmap vertical_cross = Buffers::Bitmap::EquirectangularMapToVerticalCross(in);
                            Buffers::Bitmap cubemap        = Buffers::Bitmap::VerticalCrossToCubemap(vertical_cross);

                            size_t          buffer_size    = cubemap.Buffer.size();
                            size_t          buffer_byte    = buffer_size * sizeof(uint8_t);
                            upload_req.Buffer.resize(buffer_size);
                            Helpers::secure_memmove(upload_req.Buffer.data(), buffer_byte, cubemap.Buffer.data(), buffer_byte);
                        }
                    }
                    else
                    {

                        stbi_uc* image_data = stbi_load(file_request.Filename.data(), &width, &height, &channel, STBI_rgb_alpha);
                        if (!image_data)
                        {
                            ZENGINE_CORE_ERROR("Failed to load texture file : {0}", file_request.Filename.data())
                            continue;
                        }

                        bool perform_convert_rgb_to_rgba = (channel <= STBI_rgb);

                        if (perform_convert_rgb_to_rgba)
                        {
                            size_t total_pixel = width * height;
                            size_t buffer_size = total_pixel * 4;
                            upload_req.Buffer.resize(buffer_size);
                            stbir_resize_uint8(image_data, width, height, 0, upload_req.Buffer.data(), width, height, 0, 4);

                            for (int i = 0; i < total_pixel; ++i)
                            {
                                int offset = i * 4; // RGBA format (4 channels)

                                if (channel == 1)
                                {
                                    upload_req.Buffer[offset + 3] = 255;
                                }
                                else if (channel == 2)
                                {
                                    upload_req.Buffer[offset + 3] = image_data[i * 2 + 1];
                                }
                                else if (channel == 3)
                                {
                                    upload_req.Buffer[offset + 3] = 255;
                                }
                            }
                        }
                        else
                        {
                            size_t total_pixel = width * height;
                            size_t buffer_size = total_pixel * channel;
                            upload_req.Buffer.resize(buffer_size, 0);
                            Helpers::secure_memmove(upload_req.Buffer.data(), buffer_size, image_data, buffer_size);
                        }

                        stbi_image_free(image_data);
                    }

                    upload_req.BufferSize  = (upload_req.Buffer.size() * sizeof(uint8_t));
                    upload_req.Handle      = file_request.Handle;
                    upload_req.TextureSpec = file_request.TextureSpec;
                    upload_req.FrameIdx    = file_request.FrameIdx;
                    upload_req.ThreadIdx   = file_request.ThreadIdx;

                    m_upload_requests.Emplace(std::move(upload_req));
                }
            }
        }
    }

    void AsyncResourceLoader::Shutdown()
    {
        m_cancellation_token.store(true, std::memory_order_release);
        // We are safe to call destructor to clean up semaphore resources
        // Timeline->~Semaphore();
    }

    void AsyncResourceLoader::Reset()
    {
        auto total_thread_count = Device->CommandBufferMgr->TotalThreadCount;
        auto frame_count        = Device->SwapchainPtr->BufferredFrameCount;

        for (uint32_t f = 0; f < frame_count; ++f)
        {
            for (uint32_t t = 0; t < total_thread_count; ++t)
            {
                ResetCommandBuffers(f, t);

                uint32_t pool_index   = (f * Device->CommandBufferMgr->TotalThreadCount) + t;

                uint64_t grahic_value = 0;
                vkGetSemaphoreCounterValue(Device->LogicalDevice, Timelines[pool_index]->GetHandle(), &grahic_value);
                NextValues[pool_index].store(grahic_value + 1, std::memory_order_release);

                if (Device->HasSeperateTransfertQueueFamily)
                {
                    uint64_t transfer_value = 0;
                    vkGetSemaphoreCounterValue(Device->LogicalDevice, TransferTimelines[pool_index]->GetHandle(), &transfer_value);
                    TransferNextValues[pool_index].store(transfer_value + 1, std::memory_order_release);
                }
            }
        }
    }
} // namespace ZEngine::Hardwares
