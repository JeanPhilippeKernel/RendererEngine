#include <pch.h>
#include <Helpers/ThreadPool.h>
#include <ImGUIRenderer.h>
#include <Rendering/Renderers/Contracts/RendererDataContract.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <SceneRenderer.h>

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
    GraphicRenderer::GraphicRenderer() {}

    GraphicRenderer::~GraphicRenderer() {}

    void GraphicRenderer::Initialize(Hardwares::VulkanDevice* device)
    {
        Device         = device;
        GlobalTextures = CreateRef<Textures::TextureHandleManager>(600);

        RenderGraph       = CreateScope<Renderers::RenderGraph>(this);
        m_resource_loader = CreateRef<AsyncResourceLoader>();
        SceneRenderer     = CreateRef<Renderers::SceneRenderer>();
        ImguiRenderer     = CreateRef<ImGUIRenderer>();
        /*
         * Shared Buffers
         */
        m_UBCamera = CreateUniformBufferSet();

        auto builder = RenderGraph->GetBuilder();
        builder->AttachBuffer("scene_camera", m_UBCamera);
        /*
         * Sub Renderer Initialization
         */
        m_resource_loader->Initialize(this);
        m_resource_loader->Start();

        SceneRenderer->Initialize(this);
        ImguiRenderer->Initialize(this);

        RenderGraph->Setup();
        RenderGraph->Compile();
    }

    void GraphicRenderer::Deinitialize()
    {
        RenderGraph->Dispose();

        SceneRenderer->Deinitialize();
        ImguiRenderer->Deinitialize();

        m_UBCamera->Dispose();
        GlobalTextures->Dispose();

        m_resource_loader->Shutdown();
        m_resource_loader.reset();
    }

    void GraphicRenderer::SetViewportSize(uint32_t width, uint32_t height)
    {
        RenderGraph->Resize(width, height);
    }

    void GraphicRenderer::Update() {}

    void GraphicRenderer::DrawScene(
        Rendering::Buffers::CommandBuffer* const    command_buffer,
        const Ref<Rendering::Cameras::Camera>&      camera,
        const Ref<Rendering::Scenes::SceneRawData>& scene_data)
    {
        uint32_t frame_index = Device->CurrentFrameIndex;

        auto& scene_camera    = *m_UBCamera;
        auto  ubo_camera_data = UBOCameraLayout{
             .View         = camera->GetViewMatrix(),
             .RotScaleView = glm::mat4(glm::mat3(camera->GetViewMatrix())),
             .Projection   = camera->GetPerspectiveMatrix(),
             .Position     = glm::vec4(camera->GetPosition(), 1.0f)};

        scene_camera[frame_index].SetData(&ubo_camera_data, sizeof(UBOCameraLayout));

        RenderGraph->Execute(frame_index, command_buffer, scene_data.get());
    }

    VkDescriptorSet GraphicRenderer::GetImguiFrameOutput()
    {
        auto& frame_color_res = RenderGraph->GetResource("lighting_render_target");
        return ImguiRenderer->UpdateFrameOutput(frame_color_res.ResourceInfo.TextureHandle);
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

    Helpers::Ref<Buffers::VertexBufferSet> GraphicRenderer::CreateVertexBufferSet()
    {
        return CreateRef<Buffers::VertexBufferSet>(Device, Device->SwapchainImageCount);
    }

    Helpers::Ref<Buffers::StorageBufferSet> GraphicRenderer::CreateStorageBufferSet()
    {
        return CreateRef<Buffers::StorageBufferSet>(Device, Device->SwapchainImageCount);
    }

    Helpers::Ref<Buffers::IndirectBufferSet> GraphicRenderer::CreateIndirectBufferSet()
    {
        return CreateRef<Buffers::IndirectBufferSet>(Device, Device->SwapchainImageCount);
    }

    Helpers::Ref<Buffers::IndexBufferSet> GraphicRenderer::CreateIndexBufferSet()
    {
        return CreateRef<Buffers::IndexBufferSet>(Device, Device->SwapchainImageCount);
    }

    Helpers::Ref<Buffers::UniformBufferSet> GraphicRenderer::CreateUniformBufferSet()
    {
        return CreateRef<Buffers::UniformBufferSet>(Device, Device->SwapchainImageCount);
    }

    Helpers::Ref<RenderPasses::RenderPass> GraphicRenderer::CreateRenderPass(const Specifications::RenderPassSpecification& spec)
    {
        return CreateRef<RenderPasses::RenderPass>(Device, spec);
    }

    Helpers::Ref<Textures::Texture> GraphicRenderer::CreateTexture(const Specifications::TextureSpecification& spec)
    {
        auto                buffer_size    = spec.Width * spec.Height * spec.BytePerPixel * spec.LayerCount;
        Buffers::BufferView staging_buffer = Device->CreateBuffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        Device->MapAndCopyToMemory(staging_buffer, buffer_size, spec.Data);

        uint32_t storage_bit   = spec.IsUsageStorage ? VK_IMAGE_USAGE_STORAGE_BIT : 0;
        uint32_t transfert_bit = spec.IsUsageTransfert ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0;
        uint32_t sampled_bit   = spec.IsUsageSampled ? VK_IMAGE_USAGE_SAMPLED_BIT : 0;
        uint32_t image_aspect  = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        uint32_t image_usage_attachment =
            (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        VkFormat image_format =
            (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? Device->FindDepthFormat() : Specifications::ImageFormatMap[VALUE_FROM_SPEC_MAP(spec.Format)];

        Specifications::Image2DBufferSpecification buffer_spec = {
            .Width           = spec.Width,
            .Height          = spec.Height,
            .BufferUsageType = spec.IsCubemap ? Specifications::ImageBufferUsageType::CUBEMAP : Specifications::ImageBufferUsageType::SINGLE_2D_IMAGE,
            .ImageFormat     = image_format,
            .ImageAspectFlag = VkImageAspectFlagBits(image_aspect),
            .LayerCount      = spec.LayerCount};

        buffer_spec.ImageUsage                      = VkImageUsageFlagBits(image_usage_attachment | transfert_bit | sampled_bit | storage_bit);
        Ref<Buffers::Image2DBuffer> image_2d_buffer = CreateRef<Buffers::Image2DBuffer>(Device, std::move(buffer_spec));

        if (spec.PerformTransition)
        {
            auto command_buffer = Device->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE);
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
                barrier_spec_0.LayerCount                                      = spec.LayerCount;
                Primitives::ImageMemoryBarrier barrier_0{barrier_spec_0};

                command_buffer->TransitionImageLayout(barrier_0);
                command_buffer->CopyBufferToImage(staging_buffer, image_buffer, spec.Width, spec.Height, spec.LayerCount, barrier_0.GetHandle().newLayout);

                Specifications::ImageMemoryBarrierSpecification barrier_spec_1 = {};
                barrier_spec_1.ImageHandle                                     = image_handle;
                barrier_spec_1.OldLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
                barrier_spec_1.NewLayout        = VkImageAspectFlagBits(image_aspect) == VK_IMAGE_ASPECT_DEPTH_BIT ? Specifications::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                                                                                   : Specifications::ImageLayout::SHADER_READ_ONLY_OPTIMAL;
                barrier_spec_1.ImageAspectMask  = VkImageAspectFlagBits(image_aspect);
                barrier_spec_1.SourceAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier_spec_1.DestinationAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barrier_spec_1.SourceStageMask       = VK_PIPELINE_STAGE_TRANSFER_BIT;
                barrier_spec_1.DestinationStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                barrier_spec_1.LayerCount            = spec.LayerCount;
                Primitives::ImageMemoryBarrier barrier_1{barrier_spec_1};
                command_buffer->TransitionImageLayout(barrier_1);
            }
            Device->EnqueueInstantCommandBuffer(command_buffer);
        }

        Device->EnqueueBufferForDeletion(staging_buffer);

        return CreateRef<Textures::Texture>(spec, std::move(image_2d_buffer));
    }

    Helpers::Ref<Textures::Texture> GraphicRenderer::CreateTexture(uint32_t width, uint32_t height)
    {
        unsigned char image_data[] = {255, 255, 255, 255, '\0'};

        Specifications::TextureSpecification spec = {
            .Width        = width,
            .Height       = height,
            .BytePerPixel = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(Specifications::ImageFormat::R8G8B8A8_SRGB)],
            .Format       = Specifications::ImageFormat::R8G8B8A8_SRGB,
            .Data         = image_data,
        };

        return CreateTexture(spec);
    }

    Helpers::Ref<Textures::Texture> GraphicRenderer::CreateTexture(uint32_t width, uint32_t height, float r, float g, float b, float a)
    {
        unsigned char image_data[] = {0, 0, 0, 0, '\0'};
        image_data[0]              = static_cast<unsigned char>(std::clamp(r, .0f, 255.0f));
        image_data[1]              = static_cast<unsigned char>(std::clamp(g, .0f, 255.0f));
        image_data[2]              = static_cast<unsigned char>(std::clamp(b, .0f, 255.0f));
        image_data[3]              = static_cast<unsigned char>(std::clamp(a, .0f, 255.0f));

        Specifications::TextureSpecification spec = {
            .Width        = width,
            .Height       = height,
            .BytePerPixel = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(Specifications::ImageFormat::R8G8B8A8_SRGB)],
            .Format       = Specifications::ImageFormat::R8G8B8A8_SRGB,
            .Data         = image_data};
        return CreateTexture(spec);
    }

    Textures::TextureHandle GraphicRenderer::LoadTextureFile(std::string_view filename)
    {
        Textures::TextureHandle handle = GlobalTextures->Create();
        m_resource_loader->EnqueueTextureRequest(filename, handle);
        return handle;
    }

    // AsyncResourceLoader
    //
    void AsyncResourceLoader::Initialize(GraphicRenderer* renderer)
    {
        Renderer = renderer;
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
            m_file_requests.Wait(m_cancellation_token);

            if (m_cancellation_token.load() == true)
            {
                break;
            }

            // Processing update requests
            if (m_update_texture_request.Size())
            {
                UpdateTextureRequest tr;
                if (m_update_texture_request.Pop(tr))
                {
                    auto     image_handle = tr.Texture->ImageBuffer->GetHandle();
                    auto&    spec         = tr.Texture->Specification;
                    uint32_t image_aspect = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                    Specifications::ImageMemoryBarrierSpecification barrier_spec = {};
                    barrier_spec.ImageHandle                                     = image_handle;
                    barrier_spec.OldLayout                                       = Specifications::ImageLayout::TRANSFER_SRC_OPTIMAL;
                    barrier_spec.NewLayout        = VkImageAspectFlagBits(image_aspect) == VK_IMAGE_ASPECT_DEPTH_BIT ? Specifications::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                                                                                     : Specifications::ImageLayout::SHADER_READ_ONLY_OPTIMAL;
                    barrier_spec.ImageAspectMask  = VkImageAspectFlagBits(image_aspect);
                    barrier_spec.SourceAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    barrier_spec.DestinationAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    barrier_spec.SourceStageMask       = VK_PIPELINE_STAGE_TRANSFER_BIT;
                    barrier_spec.DestinationStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    barrier_spec.LayerCount            = spec.LayerCount;
                    Primitives::ImageMemoryBarrier barrier{barrier_spec};

                    auto command_buffer = Renderer->Device->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE);
                    {
                        command_buffer->TransitionImageLayout(barrier);
                    }
                    Renderer->Device->EnqueueInstantCommandBuffer(command_buffer);

                    Renderer->GlobalTextures->Update(tr.Handle, std::move(tr.Texture));
                }
            }

            // Processing upload requests
            if (m_upload_requests.Size())
            {
                TextureUploadRequest upload_request;
                if (m_upload_requests.Pop(upload_request))
                {
                    auto buffer_size =
                        upload_request.TextureSpec.Width * upload_request.TextureSpec.Height * upload_request.TextureSpec.BytePerPixel * upload_request.TextureSpec.LayerCount;
                    Buffers::BufferView staging_buffer =
                        Renderer->Device->CreateBuffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
                    Renderer->Device->MapAndCopyToMemory(staging_buffer, buffer_size, upload_request.TextureSpec.Data);

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
                                                                                  ? Renderer->Device->FindDepthFormat()
                                                                                  : Specifications::ImageFormatMap[static_cast<uint32_t>(upload_request.TextureSpec.Format)];
                    Specifications::Image2DBufferSpecification buffer_spec  = {
                         .Width  = upload_request.TextureSpec.Width,
                         .Height = upload_request.TextureSpec.Height,
                         .BufferUsageType =
                            upload_request.TextureSpec.IsCubemap ? Specifications::ImageBufferUsageType::CUBEMAP : Specifications::ImageBufferUsageType::SINGLE_2D_IMAGE,
                         .ImageFormat     = image_format,
                         .ImageAspectFlag = VkImageAspectFlagBits(image_aspect),
                         .LayerCount      = upload_request.TextureSpec.LayerCount};
                    buffer_spec.ImageUsage = VkImageUsageFlagBits(image_usage_attachment | transfert_bit | sampled_bit | storage_bit);

                    Ref<Buffers::Image2DBuffer> image_2d_buffer = CreateRef<Buffers::Image2DBuffer>(Renderer->Device, std::move(buffer_spec));

                    auto command_buffer = Renderer->Device->GetInstantCommandBuffer(QueueType::TRANSFER_QUEUE);
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

                        Specifications::ImageMemoryBarrierSpecification barrier_spec_1 = {};
                        barrier_spec_1.ImageHandle                                     = image_handle;
                        barrier_spec_1.OldLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
                        barrier_spec_1.NewLayout                                       = Specifications::ImageLayout::TRANSFER_SRC_OPTIMAL;
                        barrier_spec_1.ImageAspectMask                                 = VkImageAspectFlagBits(image_aspect);
                        barrier_spec_1.SourceAccessMask                                = VK_ACCESS_TRANSFER_WRITE_BIT;
                        barrier_spec_1.DestinationAccessMask                           = VK_ACCESS_TRANSFER_READ_BIT;
                        barrier_spec_1.SourceStageMask                                 = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                        barrier_spec_1.DestinationStageMask                            = VK_PIPELINE_STAGE_TRANSFER_BIT;
                        barrier_spec_1.LayerCount                                      = upload_request.TextureSpec.LayerCount;
                        barrier_spec_1.SourceQueueFamily                               = Renderer->Device->TransferFamilyIndex;
                        barrier_spec_1.DestinationQueueFamily                          = Renderer->Device->GraphicFamilyIndex;
                        Primitives::ImageMemoryBarrier barrier_1{barrier_spec_1};
                        command_buffer->TransitionImageLayout(barrier_1);
                    }
                    Renderer->Device->EnqueueInstantCommandBuffer(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT);

                    UpdateTextureRequest tr = {
                        .Handle = upload_request.Handle, .Texture = CreateRef<Textures::Texture>(std::move(upload_request.TextureSpec), std::move(image_2d_buffer))};

                    m_update_texture_request.Emplace(std::move(tr));

                    /* Cleanup resource */
                    temp_buffer.reset();
                    Renderer->Device->EnqueueBufferForDeletion(staging_buffer);
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
                    .Width        = (uint32_t) width,
                    .Height       = (uint32_t) height,
                    .BytePerPixel = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(Specifications::ImageFormat::R8G8B8A8_SRGB)],
                    .Format       = Specifications::ImageFormat::R8G8B8A8_SRGB,
                    .Data         = temp_buffer.get(),
                };

                m_upload_requests.Emplace({.Handle = file_request.Handle, .TextureSpec = std::move(spec)});
            }
        }
    }

    void AsyncResourceLoader::Shutdown()
    {
        m_cancellation_token = true;
    }

    void AsyncResourceLoader::EnqueueTextureRequest(std::string_view file, const Textures::TextureHandle& handle)
    {
        m_file_requests.Emplace({.Filename = file.data(), .Handle = handle});
    }
} // namespace ZEngine::Rendering::Renderers
