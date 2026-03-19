#include <AsyncResourceLoader.h>
#include <Helpers/ThreadPool.h>
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
        Device          = device;
        TextureTimeline = ZPushStructCtorArgs(Device->Arena, Rendering::Primitives::Semaphore, Device, true);
        BufferTimeline  = ZPushStructCtorArgs(Device->Arena, Rendering::Primitives::Semaphore, Device, true);
        RetireValues.init(Device->Arena, Device->CommandBufferMgr->TotalPoolCount, Device->CommandBufferMgr->TotalPoolCount);

        auto max_buffers = Device->CommandBufferMgr->MaxBufferPerPool * Device->CommandBufferMgr->MaxBufferPerPool;
        for (uint32_t i = 0; i < Device->CommandBufferMgr->TotalPoolCount; ++i)
        {
            RetireValues[i].init(Device->Arena, max_buffers, max_buffers);
        }
    }

    void AsyncResourceLoader::Submit(UploadType type, uint8_t frame_index, uint8_t thread_index, const UploadRequest& request)
    {
        switch (type)
        {
            case UploadType::TEXTURE_BUFFER:
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
                case UNKNOWN:
                    break;
            }

            uint32_t pool_index = (frame_index * Device->CommandBufferMgr->TotalThreadCount) + thread_index;
            uint32_t i          = 0;
            uint64_t counter    = 0;
            vkGetSemaphoreCounterValue(Device->LogicalDevice, BufferTimeline->GetHandle(), &counter);

            auto& retire_values = RetireValues[pool_index];
            for (; i < retire_values.size(); ++i)
            {
                if (counter >= retire_values[i])
                {
                    break;
                }
            }

            BufferTimeline->SetState(SemaphoreState::Idle);
            auto                  command_buffer = Device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE, frame_index, thread_index, i);

            VkBufferMemoryBarrier bufMemBarrier  = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            bufMemBarrier.srcAccessMask          = VK_ACCESS_HOST_WRITE_BIT;
            bufMemBarrier.dstAccessMask          = dst_access_mask;
            bufMemBarrier.srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
            bufMemBarrier.dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
            bufMemBarrier.buffer                 = buffer_view->Handle;
            bufMemBarrier.offset                 = 0;
            bufMemBarrier.size                   = VK_WHOLE_SIZE;

            // It's important to insert a buffer memory barrier here to ensure writing to the buffer has finished.
            vkCmdPipelineBarrier(command_buffer->GetHandle(), VK_PIPELINE_STAGE_HOST_BIT, dst_pipeline_stage, 0, 0, nullptr, 1, &bufMemBarrier, 0, nullptr);

            command_buffer->End();
            uint64_t signal_value = NextValue.fetch_add(1);
            Device->QueueSubmit(command_buffer, BufferTimeline, signal_value);
            retire_values[i] = signal_value;
            Device->EnqueueAsyncGPUOperation({signal_value, BufferTimeline});
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

        uint32_t pool_index = (frame_index * Device->CommandBufferMgr->TotalThreadCount) + thread_index;
        uint32_t i          = 0;
        uint64_t counter    = 0;
        vkGetSemaphoreCounterValue(Device->LogicalDevice, BufferTimeline->GetHandle(), &counter);

        auto& retire_values = RetireValues[pool_index];
        for (; i < retire_values.size(); ++i)
        {
            if (counter >= retire_values[i])
            {
                break;
            }
        }

        auto       command_buffer = Device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE, frame_index, thread_index, i);

        BufferView staging_buffer = Device->CreateBuffer(static_cast<VkDeviceSize>(byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

        ZENGINE_VALIDATE_ASSERT(vmaCopyMemoryToAllocation(Device->VmaAllocatorValue, data, staging_buffer.Allocation, offset, byte_size) == VK_SUCCESS, "Failed to perform memory copy operation")

        auto dst_pipeline_stage = Device->CopyBuffer(command_buffer, staging_buffer, *destination, byte_size, 0u, offset);

        command_buffer->End();
        uint64_t signal_value = NextValue.fetch_add(1);
        Device->QueueSubmit(command_buffer, BufferTimeline, signal_value, dst_pipeline_stage);
        retire_values[i] = signal_value;

        Device->EnqueueAsyncGPUOperation({signal_value, BufferTimeline});
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
            uint32_t pool_index = (frame_index * Device->CommandBufferMgr->TotalThreadCount) + thread_index;
            uint32_t i          = 0;
            uint64_t counter    = 0;
            vkGetSemaphoreCounterValue(Device->LogicalDevice, BufferTimeline->GetHandle(), &counter);

            auto& retire_values = RetireValues[pool_index];
            for (; i < retire_values.size(); ++i)
            {
                if (counter >= retire_values[i])
                {
                    break;
                }
            }

            BufferTimeline->SetState(SemaphoreState::Idle);
            auto              command_buffer  = Device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE, frame_index, thread_index, i);

            BufferView        staging_buffer  = Device->CreateBuffer(static_cast<VkDeviceSize>(byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

            VmaAllocationInfo allocation_info = {};
            vmaGetAllocationInfo(Device->VmaAllocatorValue, staging_buffer.Allocation, &allocation_info);

            if (allocation_info.pMappedData)
            {
                ZENGINE_VALIDATE_ASSERT(Helpers::secure_memset(allocation_info.pMappedData, clear_value, allocation_info.size, byte_size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
                ZENGINE_VALIDATE_ASSERT(vmaFlushAllocation(Device->VmaAllocatorValue, staging_buffer.Allocation, 0, byte_size) == VK_SUCCESS, "Failed to flush allocation")

                auto dst_pipeline_stage = Device->CopyBuffer(command_buffer, staging_buffer, *buffer_view, byte_size, 0u, offset);

                command_buffer->End();
                uint64_t signal_value = NextValue.fetch_add(1);
                Device->QueueSubmit(command_buffer, BufferTimeline, signal_value, dst_pipeline_stage);
                retire_values[i] = signal_value;

                Device->EnqueueAsyncGPUOperation({signal_value, BufferTimeline});
            }

            /* Cleanup resource */
            Device->EnqueueBufferForDeletion(staging_buffer);
        }
    }

    void AsyncResourceLoader::UploadTextureBuffer(uint8_t frame_index, uint8_t thread_index, const Rendering::Textures::TextureHandle& handle, unsigned char* data)
    {
        if (!handle.Valid() || !data)
        {
            return;
        }

        uint32_t pool_index = (frame_index * Device->CommandBufferMgr->TotalThreadCount) + thread_index;
        uint32_t i          = 0;
        uint64_t counter    = 0;
        vkGetSemaphoreCounterValue(Device->LogicalDevice, TextureTimeline->GetHandle(), &counter);

        auto& retire_values = RetireValues[pool_index];
        for (; i < retire_values.size(); ++i)
        {
            if (counter >= retire_values[i])
            {
                break;
            }
        }

        auto texture        = Device->GlobalTextures.Access(handle);
        auto img_buf        = Device->Image2DBufferManager.Access(texture->BufferHandle);
        auto img_buf_aspect = (texture->Specification.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        auto buffer_handle  = img_buf->GetHandle();

        TextureTimeline->SetState(SemaphoreState::Idle);
        auto                                            command_buffer = Device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::TRANSFER_QUEUE, frame_index, thread_index, i);
        Specifications::ImageMemoryBarrierSpecification barrier_spec_0 = {};
        barrier_spec_0.ImageHandle                                     = buffer_handle;
        barrier_spec_0.OldLayout                                       = img_buf->Layout;
        barrier_spec_0.NewLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
        barrier_spec_0.ImageAspectMask                                 = VkImageAspectFlagBits(img_buf_aspect);
        barrier_spec_0.SourceAccessMask                                = VK_ACCESS_NONE;
        barrier_spec_0.DestinationAccessMask                           = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier_spec_0.SourceStageMask                                 = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        barrier_spec_0.DestinationStageMask                            = VK_PIPELINE_STAGE_TRANSFER_BIT;
        barrier_spec_0.LayerCount                                      = texture->Specification.LayerCount;
        barrier_spec_0.SourceQueueFamily                               = Device->TransferFamilyIndex;
        barrier_spec_0.DestinationQueueFamily                          = Device->TransferFamilyIndex;
        Primitives::ImageMemoryBarrier barrier_0{barrier_spec_0};
        command_buffer->TransitionImageLayout(barrier_0);

        img_buf->Layout = barrier_spec_0.NewLayout;

        Device->WriteTextureData(command_buffer, handle, data);

        if (Device->HasSeperateTransfertQueueFamily)
        {
            Specifications::ImageMemoryBarrierSpecification barrier_spec = {};
            barrier_spec.ImageHandle                                     = buffer_handle;
            barrier_spec.OldLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
            barrier_spec.NewLayout                                       = VkImageAspectFlagBits(img_buf_aspect) == VK_IMAGE_ASPECT_DEPTH_BIT ? Specifications::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : Specifications::ImageLayout::SHADER_READ_ONLY_OPTIMAL;
            barrier_spec.ImageAspectMask                                 = VkImageAspectFlagBits(img_buf_aspect);
            barrier_spec.SourceAccessMask                                = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier_spec.DestinationAccessMask                           = VK_ACCESS_NONE;
            barrier_spec.SourceStageMask                                 = VK_PIPELINE_STAGE_TRANSFER_BIT;
            barrier_spec.DestinationStageMask                            = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            barrier_spec.LayerCount                                      = texture->Specification.LayerCount;
            barrier_spec.SourceQueueFamily                               = Device->TransferFamilyIndex;
            barrier_spec.DestinationQueueFamily                          = Device->GraphicFamilyIndex;
            Primitives::ImageMemoryBarrier barrier{barrier_spec};
            command_buffer->TransitionImageLayout(barrier);
            img_buf->Layout = barrier_spec.NewLayout;
        }

        if (Device->HasSeperateTransfertQueueFamily)
        {
            // If the device has seperate transfer queue family, we need to submit the command buffer to transfer queue and wait for the operation to finish before we can use the texture in graphic queue.
            // Otherwise, we can directly submit the command buffer without waiting since the ownership of the resource is not transferred between queues.
            command_buffer->End();
            uint64_t signal_value = NextValue.fetch_add(1);

            Device->QueueSubmit(command_buffer, TextureTimeline, signal_value);
            retire_values[i] = signal_value;
            Device->EnqueueAsyncGPUOperation({signal_value, TextureTimeline});

            TextureTimeline->SetState(SemaphoreState::Idle);
            command_buffer = Device->CommandBufferMgr->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE, frame_index, thread_index, i);
        }

        VkAccessFlags                                   access_flag    = Device->HasSeperateTransfertQueueFamily ? VK_ACCESS_NONE : VK_ACCESS_TRANSFER_WRITE_BIT;
        VkPipelineStageFlagBits                         src_stage      = Device->HasSeperateTransfertQueueFamily ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT;

        Specifications::ImageMemoryBarrierSpecification barrier_spec_1 = {};
        barrier_spec_1.ImageHandle                                     = buffer_handle;
        barrier_spec_1.OldLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
        barrier_spec_1.NewLayout                                       = (img_buf_aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? Specifications::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : Specifications::ImageLayout::SHADER_READ_ONLY_OPTIMAL;
        barrier_spec_1.ImageAspectMask                                 = VkImageAspectFlagBits(img_buf_aspect);
        barrier_spec_1.SourceAccessMask                                = access_flag;
        barrier_spec_1.DestinationAccessMask                           = VK_ACCESS_SHADER_READ_BIT;
        barrier_spec_1.SourceStageMask                                 = src_stage;
        barrier_spec_1.DestinationStageMask                            = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        barrier_spec_1.LayerCount                                      = texture->Specification.LayerCount;
        barrier_spec_1.SourceQueueFamily                               = Device->TransferFamilyIndex;
        barrier_spec_1.DestinationQueueFamily                          = Device->GraphicFamilyIndex;
        Primitives::ImageMemoryBarrier barrier_1{barrier_spec_1};
        command_buffer->TransitionImageLayout(barrier_1);

        img_buf->Layout = barrier_spec_1.NewLayout;

        command_buffer->End();

        uint64_t signal_value = NextValue.fetch_add(1);
        Device->QueueSubmit(command_buffer, TextureTimeline, signal_value);
        retire_values[i] = signal_value;
        Device->EnqueueAsyncGPUOperation({signal_value, TextureTimeline});
    }

    Textures::TextureHandle AsyncResourceLoader::Submit(uint8_t frame_index, uint8_t thread_index, const UploadRequest& request)
    {
        std::unique_lock l(m_mutex);

        auto             abs_filename = std::filesystem::absolute(request.TextureUpload.Filename).string();

        int              w, h, ch;
        if (!stbi_info(abs_filename.c_str(), &w, &h, &ch))
        {
            return {};
        }

        const std::set<std::string_view>     known_cubmap_file_ext = {".hdr", ".exr"};
        auto                                 file_ext              = std::filesystem::path(request.TextureUpload.Filename).extension().string();

        Specifications::TextureSpecification spec{.Width = (uint32_t) w, .Height = (uint32_t) h, .Format = Specifications::ImageFormat::R8G8B8A8_SRGB};

        if (known_cubmap_file_ext.contains(file_ext))
        {
            int face_size   = w / 4;

            spec.IsCubemap  = true;
            spec.LayerCount = 6;
            spec.Format     = Specifications::ImageFormat::R32G32B32A32_SFLOAT;

            spec.Width      = face_size;
            spec.Height     = face_size;
        }

        TextureFileRequest tex_file_req       = {};
        tex_file_req.Filename                 = request.TextureUpload.Filename;
        tex_file_req.TextureSpec              = spec;
        tex_file_req.TextureSpec.BytePerPixel = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(spec.Format)];
        tex_file_req.Handle                   = Device->CreateTexture(tex_file_req.TextureSpec);
        tex_file_req.FrameIdx                 = frame_index;
        tex_file_req.ThreadIdx                = thread_index;

        m_file_requests.Enqueue(tex_file_req);

        if (!m_pump_running.load(std::memory_order_acquire))
        {
            ThreadPoolHelper::Submit([this] { Run(); });
            m_pump_running.store(true, std::memory_order_release);
        }

        return tex_file_req.Handle;
    }

    void AsyncResourceLoader::Run()
    {
        while (m_cancellation_token.load(std::memory_order_acquire) == false)
        {
            if (m_file_requests.Empty() && m_upload_requests.Empty())
            {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                continue;
            }

            // Processing upload requests
            if (m_upload_requests.Size())
            {
                TextureUploadRequest upload_request;
                if (m_upload_requests.Pop(upload_request))
                {
                    UploadTextureBuffer(upload_request.FrameIdx, upload_request.ThreadIdx, upload_request.Handle, upload_request.Buffer.data());
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
                                int offset = i * 4; // RGBA format (4 channels)

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

                        // spec.Width                     = cubemap.Width;
                        // spec.Height                    = cubemap.Height;
                        size_t          buffer_size    = cubemap.Buffer.size();
                        size_t          buffer_byte    = buffer_size * sizeof(uint8_t);
                        upload_req.Buffer.resize(buffer_size);
                        Helpers::secure_memmove(upload_req.Buffer.data(), buffer_byte, cubemap.Buffer.data(), buffer_byte);
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
        TextureTimeline->~Semaphore();
        BufferTimeline->~Semaphore();
    }
} // namespace ZEngine::Hardwares
