#include <pch.h>
#include <Rendering/Renderers/Contracts/RendererDataContract.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Helpers/ThreadPool.h>
#include <Textures/Texture2D.h>

#define STB_IMAGE_IMPLEMENTATION
#ifdef __GNUC__
#define STBI_NO_SIMD
#endif
#include <stb/stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/stb_image_resize.h>
#include <stb/stb_image_write.h>




using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Rendering::Renderers::Contracts;
using namespace ZEngine::Helpers;

namespace ZEngine::Rendering::Renderers
{
    RendererInformation                 GraphicRenderer::s_renderer_information      = {};
    Ref<Rendering::Swapchain>           GraphicRenderer::s_swapchain                 = nullptr;
    Ref<Buffers::UniformBufferSet>      GraphicRenderer::s_UBCamera                  = {};
    Ref<Textures::TextureHandleManager> GraphicRenderer::GlobalTextures              = {};
    Pools::CommandPool*                 GraphicRenderer::s_command_pool              = nullptr;
    Buffers::CommandBuffer*             GraphicRenderer::s_current_command_buffer    = nullptr;
    Buffers::CommandBuffer*             GraphicRenderer::s_current_command_buffer_ui = nullptr;
    Ref<SceneRenderer>                  GraphicRenderer::s_scene_renderer            = CreateRef<SceneRenderer>();
    Ref<ImGUIRenderer>                  GraphicRenderer::s_imgui_renderer            = CreateRef<ImGUIRenderer>();
    Scope<RenderGraph>                  GraphicRenderer::s_render_graph              = CreateScope<RenderGraph>();
    Ref<AsyncResourceLoader>            GraphicRenderer::s_resource_loader           = CreateRef<AsyncResourceLoader>();
    Helpers::ThreadSafeQueue<UpdateTextureRequest> GraphicRenderer::s_update_texture_request{};
    Helpers::Ref<Primitives::Fence>                GraphicRenderer::s_transfer_fence;
    Helpers::Ref<Primitives::Semaphore>            GraphicRenderer::s_transfer_semaphore;

    void GraphicRenderer::Initialize(const Helpers::Ref<ZEngine::Windows::CoreWindow>& window)
    {
        Hardwares::VulkanDevice::Initialize(window);

        s_transfer_fence     = CreateRef<Primitives::Fence>(true);
        s_transfer_semaphore = CreateRef<Primitives::Semaphore>();

        s_swapchain                                = CreateRef<Rendering::Swapchain>();
        s_renderer_information.FrameCount          = s_swapchain->GetImageCount();
        s_renderer_information.CurrentFrameIndex   = s_swapchain->GetCurrentFrameIndex();
        s_renderer_information.SwapchainIdentifier = s_swapchain->GetIdentifier();

        s_command_pool = Hardwares::VulkanDevice::GetCommandPool(QueueType::GRAPHIC_QUEUE);
        /*
         * Shared Buffers
         */
        const auto& device_properties = Hardwares::VulkanDevice::GetPhysicalDeviceProperties();
        s_UBCamera                    = CreateRef<Buffers::UniformBufferSet>(s_renderer_information.FrameCount);
        GlobalTextures                = CreateRef<Textures::TextureHandleManager>(device_properties.limits.maxDescriptorSetSampledImages - 20);

        auto builder = s_render_graph->GetBuilder();
        builder->AttachBuffer("scene_camera", s_UBCamera);
        /*
         * Sub Renderer Initialization
         */
        s_resource_loader->Initialize();
        s_resource_loader->Start();
        
        s_scene_renderer->Initialize(s_render_graph.get());
        s_imgui_renderer->Initialize(s_render_graph.get());

        s_render_graph->Setup();
        s_render_graph->Compile();
    }

    void GraphicRenderer::Deinitialize()
    {
        s_render_graph->Dispose();

        s_scene_renderer->Deinitialize();
        s_imgui_renderer->Deinitialize();

        s_UBCamera->Dispose();
        GlobalTextures->Dispose();

        s_swapchain.reset();

        s_transfer_fence.reset();
        s_transfer_semaphore.reset();

        s_resource_loader->Shutdown();
        s_resource_loader.reset();

        Hardwares::VulkanDevice::Deinitialize();
        Hardwares::VulkanDevice::Dispose();
    }

    void GraphicRenderer::SetViewportSize(uint32_t width, uint32_t height)
    {
        s_render_graph->Resize(width, height);
    }

    void GraphicRenderer::Update()
    {
        GetRendererInformation();

        auto pool = Hardwares::VulkanDevice::GetCommandPool(QueueType::TRANSFER_QUEUE);

        if (s_transfer_fence->IsSignaled())
        {
            UpdateTextureRequest tr;
            if (s_update_texture_request.Pop(tr))
            {
                auto     buffer       = tr.Texture->GetBuffer();
                auto     spec         = tr.Texture->GetSpecification();
                uint32_t image_aspect = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

                Specifications::ImageMemoryBarrierSpecification barrier_spec_1 = {};
                barrier_spec_1.ImageHandle                                     = tr.Texture->GetBuffer().Handle;
                barrier_spec_1.OldLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
                barrier_spec_1.NewLayout             = VkImageAspectFlagBits(image_aspect) == VK_IMAGE_ASPECT_DEPTH_BIT ? Specifications::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                                                                                        : Specifications::ImageLayout::SHADER_READ_ONLY_OPTIMAL;
                barrier_spec_1.ImageAspectMask       = VkImageAspectFlagBits(image_aspect);
                barrier_spec_1.SourceAccessMask      = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier_spec_1.DestinationAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barrier_spec_1.SourceStageMask       = VK_PIPELINE_STAGE_TRANSFER_BIT;
                barrier_spec_1.DestinationStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                barrier_spec_1.LayerCount            = spec.LayerCount;
                Primitives::ImageMemoryBarrier barrier_1{barrier_spec_1};

                auto command_buffer = pool->GetCommmandBuffer();
                command_buffer->SetSignalFence(s_transfer_fence);
                command_buffer->SetSignalSemaphore(s_transfer_semaphore);
                command_buffer->Begin();
                command_buffer->TransitionImageLayout(barrier_1);
                command_buffer->End();
                command_buffer->Submit(false, VK_PIPELINE_STAGE_TRANSFER_BIT);

                GlobalTextures->Update(tr.Handle, std::move(tr.Texture));
            }
        }
    }

    void GraphicRenderer::DrawScene(const Ref<Rendering::Cameras::Camera>& camera, const Ref<Rendering::Scenes::SceneRawData>& scene_data)
    {
        uint32_t frame_index = s_renderer_information.CurrentFrameIndex;

        auto& scene_camera    = *s_UBCamera;
        auto  ubo_camera_data = UBOCameraLayout{
             .View         = camera->GetViewMatrix(),
             .RotScaleView = glm::mat4(glm::mat3(camera->GetViewMatrix())),
             .Projection   = camera->GetPerspectiveMatrix(),
             .Position     = glm::vec4(camera->GetPosition(), 1.0f)};

        scene_camera[frame_index].SetData(&ubo_camera_data, sizeof(UBOCameraLayout));

        s_current_command_buffer = s_command_pool->GetCommmandBuffer();

        s_render_graph->Execute(frame_index, s_current_command_buffer, scene_data.get());
        s_current_command_buffer->Submit();
    }

    void GraphicRenderer::BeginImguiFrame()
    {
        s_current_command_buffer_ui = s_command_pool->GetCommmandBuffer();
        s_imgui_renderer->BeginFrame(s_current_command_buffer_ui);
    }

    void GraphicRenderer::DrawUIFrame()
    {
        s_imgui_renderer->Draw(s_current_command_buffer_ui, s_renderer_information.CurrentFrameIndex);
    }

    void GraphicRenderer::EndImguiFrame()
    {
        s_imgui_renderer->EndFrame(s_current_command_buffer_ui, s_renderer_information.CurrentFrameIndex);
        s_current_command_buffer_ui->Submit();
    }

    VkDescriptorSet GraphicRenderer::GetImguiFrameOutput()
    {
        auto& frame_color_res = s_render_graph->GetResource("lighting_render_target");
        return s_imgui_renderer->UpdateFrameOutput(frame_color_res.ResourceInfo.TextureHandle->GetBuffer());
    }

    void GraphicRenderer::BindGlobalTextures(RenderPasses::RenderPass* pass)
    {
        static int cached_used_slot_count = -1;

        if (cached_used_slot_count != GlobalTextures->GetUsedSlotCount())
        {
            cached_used_slot_count = GlobalTextures->GetUsedSlotCount();
            // pass->SetInput("TextureArray", GlobalTextures);
            pass->MarkDirty();
        }
    }

    void GraphicRenderer::Present()
    {
        s_swapchain->Present();
    }

    void GraphicRenderer::ResizeSwapchain()
    {
        s_swapchain->Resize();
    }

    Helpers::Ref<Rendering::Swapchain> GraphicRenderer::GetSwapchain()
    {
        return s_swapchain;
    }

    Textures::TextureHandle GraphicRenderer::AddTexture(std::string_view filename)
    {
        Textures::TextureHandle handle = GlobalTextures->Create();
        s_resource_loader->CreateTextureFileRequest(filename, handle);
        return handle;
    }

    void GraphicRenderer::AddTextureToUpdate(UpdateTextureRequest&& req)
    {
        s_update_texture_request.Emplace(std::forward<UpdateTextureRequest>(req));
    }

    const RendererInformation& GraphicRenderer::GetRendererInformation()
    {
        /*
         * Ensure Frame information are up to date
         */
        s_renderer_information.CurrentFrameIndex = s_swapchain->GetCurrentFrameIndex();
        return s_renderer_information;
    }


    // AsyncResourceLoader
    //
    void AsyncResourceLoader::Initialize()
    {
        m_command_pool       = CreateRef<Pools::CommandPool>(QueueType::TRANSFER_QUEUE);
        m_transfer_fence     = CreateRef<Primitives::Fence>(true);
        m_transfer_semaphore = CreateRef<Primitives::Semaphore>();
    }

    void AsyncResourceLoader::Start() 
    {
        Helpers::ThreadPoolHelper::Submit([this] {
            Run();
        });
    }

    void AsyncResourceLoader::Run()
    {
        Helpers::Scope<uint8_t[]> temp_buffer = nullptr;

        while (true)
        {
            std::unique_lock l(m_mut);

            if (m_running.load() == false)
            {
                break;
            }

            // Processing upload requests
            if (m_upload_requests.Size())
            {
                if (m_transfer_fence->IsSignaled())
                {
                    TextureUploadRequest upload_request;
                    if (m_upload_requests.Pop(upload_request))
                    {
                        auto buffer_size =
                            upload_request.TextureSpec.Width * upload_request.TextureSpec.Height * upload_request.TextureSpec.BytePerPixel * upload_request.TextureSpec.LayerCount;
                        auto                  device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
                        Hardwares::BufferView staging_buffer =
                            Hardwares::VulkanDevice::CreateBuffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
                        Hardwares::VulkanDevice::MapAndCopyToMemory(staging_buffer, buffer_size, upload_request.TextureSpec.Data);

                        /* Create VkImage */
                        uint32_t storage_bit   = upload_request.TextureSpec.IsUsageStorage ? VK_IMAGE_USAGE_STORAGE_BIT : 0;
                        uint32_t transfert_bit = upload_request.TextureSpec.IsUsageTransfert ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0;
                        uint32_t sampled_bit   = upload_request.TextureSpec.IsUsageSampled ? VK_IMAGE_USAGE_SAMPLED_BIT : 0;
                        uint32_t image_aspect =
                            (upload_request.TextureSpec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                        uint32_t image_usage_attachment = (upload_request.TextureSpec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE)
                                                              ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                                              : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

                        VkFormat                                   image_format = (upload_request.TextureSpec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE)
                                                                                      ? Hardwares::VulkanDevice::FindDepthFormat()
                                                                                      : Specifications::ImageFormatMap[static_cast<uint32_t>(upload_request.TextureSpec.Format)];
                        Specifications::Image2DBufferSpecification buffer_spec;
                        buffer_spec.Width  = upload_request.TextureSpec.Width;
                        buffer_spec.Height = upload_request.TextureSpec.Height;
                        buffer_spec.BufferUsageType =
                            upload_request.TextureSpec.IsCubemap ? Specifications::ImageBufferUsageType::CUBEMAP : Specifications::ImageBufferUsageType::SINGLE_2D_IMAGE;
                        buffer_spec.ImageFormat     = image_format;
                        buffer_spec.ImageUsage      = VkImageUsageFlagBits(image_usage_attachment | transfert_bit | sampled_bit | storage_bit);
                        buffer_spec.ImageAspectFlag = VkImageAspectFlagBits(image_aspect);
                        buffer_spec.LayerCount      = upload_request.TextureSpec.LayerCount;

                        Ref<Buffers::Image2DBuffer> image_2d_buffer = CreateRef<Buffers::Image2DBuffer>(std::move(buffer_spec));

                        auto command_buffer = m_command_pool->GetCommmandBuffer();
                        command_buffer->SetSignalFence(m_transfer_fence);
                        command_buffer->SetSignalSemaphore(m_transfer_semaphore);
                        command_buffer->Begin();
                        {
                            auto                                            image_handle   = image_2d_buffer->GetHandle();
                            auto&                                           image_buffer   = image_2d_buffer->GetBuffer();
                            Specifications::ImageMemoryBarrierSpecification barrier_spec_0 = {};
                            barrier_spec_0.ImageHandle                                     = image_handle;
                            barrier_spec_0.OldLayout                                       = Specifications::ImageLayout::UNDEFINED;
                            barrier_spec_0.NewLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
                            barrier_spec_0.ImageAspectMask                                 = VkImageAspectFlagBits(image_aspect);
                            barrier_spec_0.SourceAccessMask                                = 0;
                            barrier_spec_0.DestinationAccessMask                           = VK_ACCESS_TRANSFER_WRITE_BIT;
                            barrier_spec_0.SourceStageMask                                 = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                            barrier_spec_0.DestinationStageMask                            = VK_PIPELINE_STAGE_TRANSFER_BIT;
                            barrier_spec_0.LayerCount                                      = upload_request.TextureSpec.LayerCount;
                            Primitives::ImageMemoryBarrier barrier_0{barrier_spec_0};

                            command_buffer->TransitionImageLayout(barrier_0);
                            command_buffer->CopyBufferToImage(
                                staging_buffer,
                                image_buffer,
                                upload_request.TextureSpec.Width,
                                upload_request.TextureSpec.Height,
                                upload_request.TextureSpec.LayerCount,
                                barrier_0.GetHandle().newLayout);
                        }
                        command_buffer->End();
                        command_buffer->Submit(false, VK_PIPELINE_STAGE_TRANSFER_BIT);

                        UpdateTextureRequest tr = {.Handle = upload_request.Handle, .Texture = CreateRef<Textures::Texture2D>(upload_request.TextureSpec, image_2d_buffer)};
                        GraphicRenderer::AddTextureToUpdate(std::move(tr));
                         
                        /* Cleanup resource */
                        temp_buffer.reset();
                        Hardwares::VulkanDevice::EnqueueBufferForDeletion(staging_buffer);
                    }
                }
            }

            // Processing file requests
            TextureFileRequest file_request;
            if (m_file_requests.Pop(file_request))
            {
                int      width = 0, height = 0, channel = 0;
                stbi_uc* image_data = stbi_load(file_request.Filename.data(), &width, &height, &channel, STBI_rgb_alpha);

                if (!image_data)
                {
                    ZENGINE_CORE_ERROR("Failed to load texture file : {0}", file_request.Filename.data())
                    continue;
                }

                channel     = (channel == STBI_rgb) ? STBI_rgb_alpha : channel;
                temp_buffer = CreateScope<uint8_t[]>(width * height * channel);
                stbir_resize_uint8(image_data, width, height, 0, temp_buffer.get(), width, height, 0, channel);
                stbi_image_free(image_data);

                Specifications::TextureSpecification spec = {
                    .Width = (uint32_t) width, .Height = (uint32_t) height, .Format = Specifications::ImageFormat::R8G8B8A8_SRGB, .Data = temp_buffer.get()};
                spec.BytePerPixel                         = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(spec.Format)];

                m_upload_requests.Emplace({.Handle = file_request.Handle, .TextureSpec = std::move(spec)});
            }
        }
    }
    
    void AsyncResourceLoader::Shutdown()
    {
        {
            std::lock_guard lock(m_mut);
            m_running = false;
        }
    }

    void AsyncResourceLoader::CreateTextureFileRequest(std::string_view file, const Textures::TextureHandle& handle)
    {
        m_file_requests.Emplace({.Filename = file.data(), .Handle = handle});
    }
} // namespace ZEngine::Rendering::Renderers
