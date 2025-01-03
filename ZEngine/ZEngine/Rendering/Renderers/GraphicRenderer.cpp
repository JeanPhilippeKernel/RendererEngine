#include <pch.h>
#include <Helpers/ThreadPool.h>
#include <ImGUIRenderer.h>
#include <RendererPasses.h>
#include <Rendering/Buffers/Bitmap.h>
#include <Rendering/Renderers/Contracts/RendererDataContract.h>
#include <Rendering/Renderers/GraphicRenderer.h>

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
        Device            = device;
        RenderGraph       = CreateScope<Renderers::RenderGraph>(this);
        m_resource_loader = CreateRef<AsyncResourceLoader>();
        ImguiRenderer     = CreateRef<ImGUIRenderer>();
        /*
         * Renderer Passes
         */
        auto initial_pass        = CreateRef<InitialPass>();
        auto scene_depth_prepass = CreateRef<DepthPrePass>();
        auto skybox_pass         = CreateRef<SkyboxPass>();
        auto grid_pass           = CreateRef<GridPass>();
        auto gbuffer_pass        = CreateRef<GbufferPass>();
        auto lighting_pass       = CreateRef<LightingPass>();
        /*
         * Shared Buffers
         */
        m_scene_camera_buffer_handle = CreateUniformBufferSet();
        FrameColorRenderTarget = Device->GlobalTextures->Add(CreateTexture({.PerformTransition = false, .Width = 1280, .Height = 780, .Format = ImageFormat::R8G8B8A8_UNORM}));
        FrameDepthRenderTarget =
            Device->GlobalTextures->Add(CreateTexture({.PerformTransition = false, .Width = 1280, .Height = 780, .Format = ImageFormat::DEPTH_STENCIL_FROM_DEVICE}));
        /*
         * Subsystems initialization
         */
        m_resource_loader->Initialize(this);
        ImguiRenderer->Initialize(this);
        scene_depth_prepass->Initialize(this);
        lighting_pass->Initialize(this);
        /*
         * Render Graph definition
         */
        auto builder = RenderGraph->GetBuilder();
        builder->AttachBuffer("scene_camera", m_scene_camera_buffer_handle);
        builder->AttachRenderTarget(FrameDepthRenderTargetName, FrameDepthRenderTarget);
        builder->AttachRenderTarget(FrameColorRenderTargetName, FrameColorRenderTarget);
        builder->CreateBufferSet("g_scene_vertex_buffer");
        builder->CreateBufferSet("g_scene_index_buffer");
        builder->CreateBufferSet("g_scene_draw_buffer");
        builder->CreateBufferSet("g_scene_transform_buffer");
        builder->CreateBufferSet("g_scene_material_buffer");
        builder->CreateBufferSet("g_scene_directional_light_buffer");
        builder->CreateBufferSet("g_scene_point_light_buffer");
        builder->CreateBufferSet("g_scene_spot_light_buffer");
        builder->CreateBufferSet("g_scene_indirect_buffer", BufferSetCreationType::INDIRECT);

        RenderGraph->AddCallbackPass("Initial Pass", initial_pass);
        RenderGraph->AddCallbackPass("Depth Pre-Pass", scene_depth_prepass);
        RenderGraph->AddCallbackPass("Skybox Pass", skybox_pass);
        RenderGraph->AddCallbackPass("Grid Pass", grid_pass);
        RenderGraph->AddCallbackPass("G-Buffer Pass", gbuffer_pass);
        RenderGraph->AddCallbackPass("Lighting Pass", lighting_pass);

        RenderGraph->Setup();
        RenderGraph->Compile();
    }

    void GraphicRenderer::Deinitialize()
    {
        RenderGraph->Dispose();
        Device->GlobalTextures->Remove(FrameColorRenderTarget);
        Device->GlobalTextures->Remove(FrameDepthRenderTarget);

        ImguiRenderer->Deinitialize();

        VertexBufferSetManager.Dispose();
        StorageBufferSetManager.Dispose();
        IndirectBufferSetManager.Dispose();
        IndexBufferSetManager.Dispose();
        UniformBufferSetManager.Dispose();

        m_resource_loader->Shutdown();
        m_resource_loader.reset();
    }

    void GraphicRenderer::Update() {}

    void GraphicRenderer::DrawScene(
        Rendering::Buffers::CommandBuffer* const    command_buffer,
        const Ref<Rendering::Cameras::Camera>&      camera,
        const Ref<Rendering::Scenes::SceneRawData>& scene_data)
    {
        uint32_t frame_index = Device->CurrentFrameIndex;

        auto& scene_camera    = UniformBufferSetManager.Access(m_scene_camera_buffer_handle);
        auto  ubo_camera_data = UBOCameraLayout{.View = camera->GetViewMatrix(), .Projection = camera->GetPerspectiveMatrix(), .Position = glm::vec4(camera->GetPosition(), 1.0f)};

        scene_camera->At(frame_index).SetData(&ubo_camera_data, sizeof(UBOCameraLayout));

        RenderGraph->Execute(frame_index, command_buffer, scene_data.get());
    }

    void GraphicRenderer::WriteDescriptorSets(std::span<Hardwares::WriteDescriptorSetRequest> requests)
    {
        std::vector<VkWriteDescriptorSet> write_descriptor_set_collection = {};
        for (int i = 0; i < requests.size(); ++i)
        {
            auto& request = requests[i];

            if (request.DescriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            {
                auto        handle      = UniformBufferSetManager.ToHandle(request.Handle);
                auto&       buffer_set  = UniformBufferSetManager.Access(handle);
                const auto& buffer_info = buffer_set->At(request.FrameIndex).GetDescriptorBufferInfo();

                if (!buffer_info.buffer)
                {
                    continue;
                }
                write_descriptor_set_collection.emplace_back(VkWriteDescriptorSet{
                    .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .pNext            = nullptr,
                    .dstSet           = request.DstSet,
                    .dstBinding       = request.Binding,
                    .dstArrayElement  = request.DstArrayElement,
                    .descriptorCount  = request.DescriptorCount,
                    .descriptorType   = request.DescriptorType,
                    .pImageInfo       = nullptr,
                    .pBufferInfo      = &(buffer_info),
                    .pTexelBufferView = nullptr});
            }

            else if (request.DescriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            {
                auto        handle      = StorageBufferSetManager.ToHandle(request.Handle);
                auto&       buffer_set  = StorageBufferSetManager.Access(handle);
                const auto& buffer_info = buffer_set->At(request.FrameIndex).GetDescriptorBufferInfo();

                if (!buffer_info.buffer)
                {
                    continue;
                }
                write_descriptor_set_collection.emplace_back(VkWriteDescriptorSet{
                    .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .pNext            = nullptr,
                    .dstSet           = request.DstSet,
                    .dstBinding       = request.Binding,
                    .dstArrayElement  = request.DstArrayElement,
                    .descriptorCount  = request.DescriptorCount,
                    .descriptorType   = request.DescriptorType,
                    .pImageInfo       = nullptr,
                    .pBufferInfo      = &(buffer_info),
                    .pTexelBufferView = nullptr});
            }
            else if (request.DescriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            {
                auto  handle  = Device->GlobalTextures->ToHandle(request.Handle);
                auto& texture = Device->GlobalTextures->Access(handle);

                if (!texture)
                {
                    continue;
                }
                const auto& image_info = texture->ImageBuffer->GetDescriptorImageInfo();
                write_descriptor_set_collection.emplace_back(VkWriteDescriptorSet{
                    .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .pNext            = nullptr,
                    .dstSet           = request.DstSet,
                    .dstBinding       = request.Binding,
                    .dstArrayElement  = request.DstArrayElement,
                    .descriptorCount  = request.DescriptorCount,
                    .descriptorType   = request.DescriptorType,
                    .pImageInfo       = &(image_info),
                    .pBufferInfo      = nullptr,
                    .pTexelBufferView = nullptr});
            }
        }
        vkUpdateDescriptorSets(Device->LogicalDevice, write_descriptor_set_collection.size(), write_descriptor_set_collection.data(), 0, nullptr);
    }

    VkDescriptorSet GraphicRenderer::GetImguiFrameOutput()
    {
        auto rt_handle = RenderGraph->GetRenderTarget(FrameColorRenderTargetName);
        return ImguiRenderer->UpdateFrameOutput(rt_handle);
    }

    Buffers::VertexBufferSetHandle GraphicRenderer::CreateVertexBufferSet()
    {
        auto handle = VertexBufferSetManager.Create();
        if (handle)
        {
            auto& buffer = VertexBufferSetManager.Access(handle);
            buffer       = CreateRef<Buffers::VertexBufferSet>(Device, Device->SwapchainImageCount);
        }

        return handle;
    }

    Buffers::StorageBufferSetHandle GraphicRenderer::CreateStorageBufferSet()
    {
        auto handle = StorageBufferSetManager.Create();
        if (handle)
        {
            auto& buffer = StorageBufferSetManager.Access(handle);
            buffer       = CreateRef<Buffers::StorageBufferSet>(Device, Device->SwapchainImageCount);
        }
        return handle;
    }

    Buffers::IndirectBufferSetHandle GraphicRenderer::CreateIndirectBufferSet()
    {
        auto handle = IndirectBufferSetManager.Create();
        if (handle)
        {
            auto& buffer = IndirectBufferSetManager.Access(handle);
            buffer       = CreateRef<Buffers::IndirectBufferSet>(Device, Device->SwapchainImageCount);
        }
        return handle;
    }

    Buffers::IndexBufferSetHandle GraphicRenderer::CreateIndexBufferSet()
    {
        auto handle = IndexBufferSetManager.Create();
        if (handle)
        {
            auto& buffer = IndexBufferSetManager.Access(handle);
            buffer       = CreateRef<Buffers::IndexBufferSet>(Device, Device->SwapchainImageCount);
        }
        return handle;
    }

    Buffers::UniformBufferSetHandle GraphicRenderer::CreateUniformBufferSet()
    {
        auto handle = UniformBufferSetManager.Create();
        if (handle)
        {
            auto& buffer = UniformBufferSetManager.Access(handle);
            buffer       = CreateRef<Buffers::UniformBufferSet>(Device, Device->SwapchainImageCount);
        }
        return handle;
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
        Textures::TextureHandle handle = Device->GlobalTextures->Create();
        m_resource_loader->EnqueueTextureRequest(filename, handle);
        return handle;
    }

    // AsyncResourceLoader
    //
    void AsyncResourceLoader::Initialize(GraphicRenderer* renderer)
    {
        Renderer = renderer;
        m_buffer_manager.Initialize(Renderer->Device);
        Helpers::ThreadPoolHelper::Submit([this] {
            Run();
        });
    }

    void AsyncResourceLoader::Run()
    {
        while (true)
        {
            std::unique_lock l(m_mutex);
            m_cond.wait(l, [this] {
                return !m_file_requests.Empty() || !m_update_texture_request.Empty() || !m_upload_requests.Empty() || m_cancellation_token.load() == true;
            });

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
                    barrier_spec.OldLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
                    barrier_spec.NewLayout        = VkImageAspectFlagBits(image_aspect) == VK_IMAGE_ASPECT_DEPTH_BIT ? Specifications::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                                                                                     : Specifications::ImageLayout::SHADER_READ_ONLY_OPTIMAL;
                    barrier_spec.ImageAspectMask  = VkImageAspectFlagBits(image_aspect);
                    barrier_spec.SourceAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier_spec.DestinationAccessMask  = VK_ACCESS_SHADER_READ_BIT;
                    barrier_spec.SourceStageMask        = VK_PIPELINE_STAGE_TRANSFER_BIT;
                    barrier_spec.DestinationStageMask   = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    barrier_spec.LayerCount             = spec.LayerCount;
                    barrier_spec.SourceQueueFamily      = Renderer->Device->TransferFamilyIndex;
                    barrier_spec.DestinationQueueFamily = Renderer->Device->GraphicFamilyIndex;
                    Primitives::ImageMemoryBarrier barrier{barrier_spec};

                    auto command_buffer = m_buffer_manager.GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE, Renderer->Device->CurrentFrameIndex);
                    {
                        command_buffer->TransitionImageLayout(barrier);
                    }
                    m_buffer_manager.EndInstantCommandBuffer(command_buffer, Renderer->Device);

                    Renderer->Device->GlobalTextures->Update(tr.Handle, std::move(tr.Texture));
                }
            }

            // Processing upload requests
            if (m_upload_requests.Size())
            {
                TextureUploadRequest upload_request;
                if (m_upload_requests.Pop(upload_request))
                {
                    Buffers::BufferView staging_buffer =
                        Renderer->Device->CreateBuffer(upload_request.BufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
                    Renderer->Device->MapAndCopyToMemory(staging_buffer, upload_request.BufferSize, upload_request.TextureSpec.Data);

                    /* Create VkImage */
                    uint32_t storage_bit   = upload_request.TextureSpec.IsUsageStorage ? VK_IMAGE_USAGE_STORAGE_BIT : 0;
                    uint32_t transfert_bit = upload_request.TextureSpec.IsUsageTransfert ? VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0;
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

                    auto command_buffer = m_buffer_manager.GetInstantCommandBuffer(QueueType::TRANSFER_QUEUE, Renderer->Device->CurrentFrameIndex);
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
                        barrier_spec_0.SourceQueueFamily                               = Renderer->Device->TransferFamilyIndex;
                        barrier_spec_0.DestinationQueueFamily                          = Renderer->Device->TransferFamilyIndex;
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
                    m_buffer_manager.EndInstantCommandBuffer(command_buffer, Renderer->Device, VK_PIPELINE_STAGE_TRANSFER_BIT);

                    UpdateTextureRequest tr = {
                        .Handle = upload_request.Handle, .Texture = CreateRef<Textures::Texture>(std::move(upload_request.TextureSpec), std::move(image_2d_buffer))};

                    m_update_texture_request.Emplace(std::move(tr));

                    /* Cleanup resource */
                    ZENGINE_CLEAR_STD_VECTOR(m_temp_buffer)
                    Renderer->Device->EnqueueBufferForDeletion(staging_buffer);
                }
            }

            // Processing file requests
            TextureFileRequest file_request;
            if (m_file_requests.Pop(file_request))
            {
                const std::set<std::string_view> known_cubmap_file_ext = {".hdr", ".exr"};
                auto                             file_ext              = std::filesystem::path(file_request.Filename).extension().string();

                Specifications::TextureSpecification spec{};
                if (known_cubmap_file_ext.contains(file_ext))
                {
                    stbi_set_flip_vertically_on_load(1);
                    spec.IsCubemap = true;
                }

                int width = 0, height = 0, channel = 0;

                if (spec.IsCubemap)
                {
                    const float* image_data = stbi_loadf(file_request.Filename.data(), &width, &height, &channel, 4);
                    if (!image_data)
                    {
                        ZENGINE_CORE_ERROR("Failed to load texture file : {0}", file_request.Filename.data())
                        continue;
                    }

                    spec.LayerCount = 6;
                    spec.Format     = Specifications::ImageFormat::R32G32B32A32_SFLOAT;
                    channel         = (channel == STBI_rgb) ? STBI_rgb_alpha : channel;
                    std::vector<float> output_buffer(width * height * channel);
                    stbir_resize_float(image_data, width, height, 0, output_buffer.data(), width, height, 0, channel);
                    stbi_image_free((void*) image_data);

                    Buffers::Bitmap in             = {width, height, 4, Buffers::BitmapFormat::FLOAT, output_buffer.data()};
                    Buffers::Bitmap vertical_cross = Buffers::Bitmap::EquirectangularMapToVerticalCross(in);
                    Buffers::Bitmap cubemap        = Buffers::Bitmap::VerticalCrossToCubemap(vertical_cross);

                    spec.Width         = cubemap.Width;
                    spec.Height        = cubemap.Height;
                    size_t buffer_size = cubemap.Buffer.size();
                    size_t buffer_byte = buffer_size * sizeof(uint8_t);
                    m_temp_buffer.resize(buffer_size);
                    Helpers::secure_memmove(m_temp_buffer.data(), buffer_byte, cubemap.Buffer.data(), buffer_byte);
                }
                else
                {
                    stbi_uc* image_data = stbi_load(file_request.Filename.data(), &width, &height, &channel, STBI_rgb_alpha);
                    if (!image_data)
                    {
                        ZENGINE_CORE_ERROR("Failed to load texture file : {0}", file_request.Filename.data())
                        continue;
                    }

                    spec.LayerCount = 1;
                    spec.Format     = Specifications::ImageFormat::R8G8B8A8_SRGB;
                    channel         = (channel == STBI_rgb) ? STBI_rgb_alpha : channel;
                    m_temp_buffer.resize(width * height * channel);
                    stbir_resize_uint8(image_data, width, height, 0, m_temp_buffer.data(), width, height, 0, channel);
                    stbi_image_free(image_data);
                }

                spec.Data         = m_temp_buffer.data();
                spec.BytePerPixel = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(spec.Format)];

                m_upload_requests.Emplace({.BufferSize = (m_temp_buffer.size() * sizeof(uint8_t)), .Handle = file_request.Handle, .TextureSpec = std::move(spec)});
            }
        }

        ZENGINE_CLEAR_STD_VECTOR(m_temp_buffer)
    }

    void AsyncResourceLoader::Shutdown()
    {
        {
            std::unique_lock l(m_mutex);
            m_cancellation_token = true;
        }
        m_cond.notify_one();

        m_buffer_manager.Deinitialize();
    }

    void AsyncResourceLoader::EnqueueTextureRequest(std::string_view file, const Textures::TextureHandle& handle)
    {
        m_file_requests.Emplace({.Filename = file.data(), .Handle = handle});
        m_cond.notify_one();
    }
} // namespace ZEngine::Rendering::Renderers
