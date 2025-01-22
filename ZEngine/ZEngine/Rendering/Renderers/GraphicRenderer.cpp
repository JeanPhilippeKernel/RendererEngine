#include <pch.h>
#include <Helpers/ThreadPool.h>
#include <ImGUIRenderer.h>
#include <RendererPasses.h>
#include <Rendering/Buffers/Bitmap.h>
#include <Rendering/Renderers/Contracts/RendererDataContract.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Specifications/FormatSpecification.h>

#define STB_IMAGE_IMPLEMENTATION
#ifdef __GNUC__
#define STBI_NO_SIMD
#endif
#include <stb/stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/stb_image_resize.h>
#include <stb/stb_image_write.h>

using namespace ZEngine::Hardwares;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Rendering::Renderers::Contracts;
using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    GraphicRenderer::GraphicRenderer() {}
    GraphicRenderer::~GraphicRenderer() {}

    void GraphicRenderer::Initialize(Hardwares::VulkanDevice* device)
    {
        Device                       = device;
        RenderGraph                  = CreateScope<Renderers::RenderGraph>(this);
        AsyncLoader                  = CreateRef<AsyncResourceLoader>();
        ImguiRenderer                = CreateRef<ImGUIRenderer>();
        /*
         * Renderer Passes
         */
        auto initial_pass            = CreateRef<InitialPass>();
        auto scene_depth_prepass     = CreateRef<DepthPrePass>();
        auto skybox_pass             = CreateRef<SkyboxPass>();
        auto grid_pass               = CreateRef<GridPass>();
        auto gbuffer_pass            = CreateRef<GbufferPass>();
        auto lighting_pass           = CreateRef<LightingPass>();
        /*
         * Shared Buffers
         */
        SceneCameraBufferHandle      = Device->CreateUniformBufferSet();
        auto&           scene_camera = Device->UniformBufferSetManager.Access(SceneCameraBufferHandle);
        UBOCameraLayout ubo_camera   = {};
        for (int i = 0; i < Device->SwapchainImageCount; ++i)
        {
            scene_camera->At(i).SetData(&ubo_camera, sizeof(UBOCameraLayout));
        }

        FrameColorRenderTarget = Device->GlobalTextures->Add(CreateTexture({.PerformTransition = false, .Width = 1280, .Height = 780, .Format = ImageFormat::R8G8B8A8_UNORM}));
        FrameDepthRenderTarget = Device->GlobalTextures->Add(CreateTexture({.PerformTransition = false, .Width = 1280, .Height = 780, .Format = ImageFormat::DEPTH_STENCIL_FROM_DEVICE}));
        /*
         * Subsystems initialization
         */
        AsyncLoader->Initialize(this);
        ImguiRenderer->Initialize(this);
        /*
         * Render Graph definition
         */
        auto& builder = RenderGraph->Builder;
        builder->AttachRenderTarget(FrameDepthRenderTargetName, FrameDepthRenderTarget);
        builder->AttachRenderTarget(FrameColorRenderTargetName, FrameColorRenderTarget);
        builder->CreateBufferSet("g_scene_directional_light_buffer");
        builder->CreateBufferSet("g_scene_point_light_buffer");
        builder->CreateBufferSet("g_scene_spot_light_buffer");

        RenderGraph->AddCallbackPass("Initial Pass", initial_pass);
        RenderGraph->AddCallbackPass("Depth Pre-Pass", scene_depth_prepass);
        RenderGraph->AddCallbackPass("Skybox Pass", skybox_pass);
        RenderGraph->AddCallbackPass("Grid Pass", grid_pass);
        RenderGraph->AddCallbackPass("G-Buffer Pass", gbuffer_pass);
        // RenderGraph->AddCallbackPass("Lighting Pass", lighting_pass);

        RenderGraph->Setup();
        RenderGraph->Compile(nullptr);
    }

    void GraphicRenderer::Deinitialize()
    {
        AsyncLoader->Shutdown();
        AsyncLoader.reset();

        RenderGraph->Dispose();
        Device->GlobalTextures->Remove(FrameColorRenderTarget);
        Device->GlobalTextures->Remove(FrameDepthRenderTarget);

        ImguiRenderer->Deinitialize();
    }

    void GraphicRenderer::Update() {}

    void GraphicRenderer::DrawScene(Hardwares::CommandBuffer* const command_buffer, Cameras::Camera* const camera, Scenes::SceneRawData* const scene)
    {
        uint32_t frame_index     = Device->CurrentFrameIndex;
        auto&    scene_camera    = Device->UniformBufferSetManager.Access(SceneCameraBufferHandle);
        auto     ubo_camera_data = UBOCameraLayout{.View = camera->GetViewMatrix(), .Projection = camera->GetPerspectiveMatrix(), .Position = glm::vec4(camera->GetPosition(), 1.0f)};

        scene_camera->At(frame_index).SetData(&ubo_camera_data, sizeof(UBOCameraLayout));

        if (RenderGraph->MarkAsDirty)
        {
            RenderGraph->Compile(scene);
            RenderGraph->MarkAsDirty = false;
        }

        RenderGraph->Execute(frame_index, command_buffer, scene);
    }

    VkDescriptorSet GraphicRenderer::GetImguiFrameOutput()
    {
        auto rt_handle = RenderGraph->GetRenderTarget(FrameColorRenderTargetName);
        return ImguiRenderer->UpdateFrameOutput(rt_handle);
    }

    Helpers::Ref<RenderPasses::RenderPass> GraphicRenderer::CreateRenderPass(const Specifications::RenderPassSpecification& spec)
    {
        return CreateRef<RenderPasses::RenderPass>(Device, spec);
    }

    Helpers::Ref<Textures::Texture> GraphicRenderer::CreateTexture(const Specifications::TextureSpecification& spec)
    {
        uint32_t                                   storage_bit            = spec.IsUsageStorage ? VK_IMAGE_USAGE_STORAGE_BIT : 0;
        uint32_t                                   transfert_bit          = spec.IsUsageTransfert ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0;
        uint32_t                                   sampled_bit            = spec.IsUsageSampled ? VK_IMAGE_USAGE_SAMPLED_BIT : 0;
        uint32_t                                   image_aspect           = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        uint32_t                                   image_usage_attachment = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        VkFormat                                   image_format           = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? Device->FindDepthFormat() : Specifications::ImageFormatMap[VALUE_FROM_SPEC_MAP(spec.Format)];

        Specifications::Image2DBufferSpecification buffer_spec            = {.Width = spec.Width, .Height = spec.Height, .BufferUsageType = spec.IsCubemap ? Specifications::ImageBufferUsageType::CUBEMAP : Specifications::ImageBufferUsageType::SINGLE_2D_IMAGE, .ImageFormat = image_format, .ImageAspectFlag = VkImageAspectFlagBits(image_aspect), .LayerCount = spec.LayerCount};

        buffer_spec.ImageUsage                                            = VkImageUsageFlagBits(image_usage_attachment | transfert_bit | sampled_bit | storage_bit);
        Ref<Hardwares::Image2DBuffer> image_2d_buffer                     = CreateRef<Hardwares::Image2DBuffer>(Device, std::move(buffer_spec));

        auto                          command_buffer                      = Device->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE);

        auto                          image_handle                        = image_2d_buffer->GetHandle();
        auto&                         image_buffer                        = image_2d_buffer->GetBuffer();

        if (spec.PerformTransition)
        {
            Specifications::ImageMemoryBarrierSpecification barrier_spec_0 = {};
            barrier_spec_0.ImageHandle                                     = image_handle;
            barrier_spec_0.OldLayout                                       = Specifications::ImageLayout::UNDEFINED;
            barrier_spec_0.NewLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
            barrier_spec_0.ImageAspectMask                                 = VkImageAspectFlagBits(image_aspect);
            barrier_spec_0.SourceAccessMask                                = VK_ACCESS_NONE;
            barrier_spec_0.DestinationAccessMask                           = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier_spec_0.SourceStageMask                                 = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            barrier_spec_0.DestinationStageMask                            = VK_PIPELINE_STAGE_TRANSFER_BIT;
            barrier_spec_0.LayerCount                                      = spec.LayerCount;
            Primitives::ImageMemoryBarrier barrier_0{barrier_spec_0};
            command_buffer->TransitionImageLayout(barrier_0);

            if (spec.Data)
            {
                auto       buffer_size    = spec.Width * spec.Height * spec.BytePerPixel * spec.LayerCount;
                BufferView staging_buffer = Device->CreateBuffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
                Device->MapAndCopyToMemory(staging_buffer, buffer_size, spec.Data);
                command_buffer->CopyBufferToImage(staging_buffer, image_2d_buffer->GetBuffer(), spec.Width, spec.Height, spec.LayerCount, barrier_0.GetHandle().newLayout);
                Device->EnqueueBufferForDeletion(staging_buffer);
            }

            Specifications::ImageMemoryBarrierSpecification barrier_spec_1 = {};
            barrier_spec_1.ImageHandle                                     = image_handle;
            barrier_spec_1.OldLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
            barrier_spec_1.NewLayout                                       = VkImageAspectFlagBits(image_aspect) == VK_IMAGE_ASPECT_DEPTH_BIT ? Specifications::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : Specifications::ImageLayout::SHADER_READ_ONLY_OPTIMAL;
            barrier_spec_1.ImageAspectMask                                 = VkImageAspectFlagBits(image_aspect);
            barrier_spec_1.SourceAccessMask                                = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier_spec_1.DestinationAccessMask                           = VK_ACCESS_SHADER_READ_BIT;
            barrier_spec_1.SourceStageMask                                 = VK_PIPELINE_STAGE_TRANSFER_BIT;
            barrier_spec_1.DestinationStageMask                            = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            barrier_spec_1.LayerCount                                      = spec.LayerCount;
            Primitives::ImageMemoryBarrier barrier_1{barrier_spec_1};
            command_buffer->TransitionImageLayout(barrier_1);
        }

        Device->EnqueueInstantCommandBuffer(command_buffer);

        return CreateRef<Textures::Texture>(spec, std::move(image_2d_buffer));
    }

    Helpers::Ref<Textures::Texture> GraphicRenderer::CreateTexture(uint32_t width, uint32_t height)
    {
        uint32_t                             BytePerPixel = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(Specifications::ImageFormat::R8G8B8A8_SRGB)];
        size_t                               data_size    = width * height * BytePerPixel;
        std::vector<unsigned char>           image_data(data_size, 255);

        Specifications::TextureSpecification spec = {
            .Width        = width,
            .Height       = height,
            .BytePerPixel = BytePerPixel,
            .Format       = Specifications::ImageFormat::R8G8B8A8_SRGB,
            .Data         = image_data.data(),
        };

        return CreateTexture(spec);
    }

    Helpers::Ref<Textures::Texture> GraphicRenderer::CreateTexture(uint32_t width, uint32_t height, float r, float g, float b, float a)
    {
        uint32_t                   BytePerPixel = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(Specifications::ImageFormat::R8G8B8A8_SRGB)];
        size_t                     data_size    = width * height * BytePerPixel;
        std::vector<unsigned char> image_data(data_size);

        unsigned char              r_byte = static_cast<unsigned char>(std::clamp(r * 255.0f, 0.0f, 255.0f));
        unsigned char              g_byte = static_cast<unsigned char>(std::clamp(g * 255.0f, 0.0f, 255.0f));
        unsigned char              b_byte = static_cast<unsigned char>(std::clamp(b * 255.0f, 0.0f, 255.0f));
        unsigned char              a_byte = static_cast<unsigned char>(std::clamp(a * 255.0f, 0.0f, 255.0f));

        for (size_t i = 0; i < data_size; i += BytePerPixel)
        {
            image_data[i]     = r_byte;
            image_data[i + 1] = g_byte;
            image_data[i + 2] = b_byte;
            image_data[i + 3] = a_byte;
        }

        Specifications::TextureSpecification spec = {.Width = width, .Height = height, .BytePerPixel = BytePerPixel, .Format = Specifications::ImageFormat::R8G8B8A8_SRGB, .Data = image_data.data()};
        return CreateTexture(spec);
    }

    // AsyncResourceLoader
    //
    void AsyncResourceLoader::Initialize(GraphicRenderer* renderer)
    {
        Renderer = renderer;
        m_buffer_manager.Initialize(Renderer->Device);
        Helpers::ThreadPoolHelper::Submit([this] { Run(); });
    }

    Textures::TextureHandle AsyncResourceLoader::LoadTextureFileSync(std::string_view filename)
    {
        int      width = 0, height = 0, channel = 0;
        stbi_uc* image_data = stbi_load(filename.data(), &width, &height, &channel, STBI_rgb_alpha);
        if (!image_data)
        {
            ZENGINE_CORE_ERROR("Failed to load texture file synchronously: {}", filename.data());
            return Textures::TextureHandle{};
        }

        Specifications::TextureSpecification spec = {
            .Width        = static_cast<uint32_t>(width),
            .Height       = static_cast<uint32_t>(height),
            .BytePerPixel = 4, // RGBA
            .Format       = Specifications::ImageFormat::R8G8B8A8_SRGB,
            .Data         = image_data,
        };

        Textures::TextureHandle handle = Renderer->Device->GlobalTextures->Add(Renderer->CreateTexture(spec));
        stbi_image_free(image_data);

        return handle;
    }

    Textures::TextureHandle AsyncResourceLoader::LoadTextureFile(std::string_view filename)
    {
        auto abs_filename = std::filesystem::absolute(filename).string();

        int  w, h, ch;
        if (!stbi_info(abs_filename.c_str(), &w, &h, &ch))
        {
            return {};
        }

        const std::set<std::string_view>     known_cubmap_file_ext = {".hdr", ".exr"};
        auto                                 file_ext              = std::filesystem::path(filename).extension().string();

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

        Textures::TextureHandle handle = Renderer->Device->GlobalTextures->Add(Renderer->CreateTexture(spec));
        EnqueueTextureRequest(filename, handle);
        return handle;
    }

    void AsyncResourceLoader::Run()
    {
        while (true)
        {
            std::unique_lock l(m_mutex);
            m_cond.wait(l, [this] { return !m_file_requests.Empty() || !m_update_texture_request.Empty() || !m_upload_requests.Empty() || m_cancellation_token.load() == true; });

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
                    auto&    texture      = Renderer->Device->GlobalTextures->Access(tr.Handle);
                    auto&    spec         = texture->Specification;
                    auto     image_handle = texture->ImageBuffer->GetHandle();
                    uint32_t image_aspect = (texture->Specification.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

                    if (Renderer->Device->HasSeperateTransfertQueueFamily)
                    {
                        Specifications::ImageMemoryBarrierSpecification barrier_spec_0 = {};
                        barrier_spec_0.ImageHandle                                     = image_handle;
                        barrier_spec_0.OldLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
                        barrier_spec_0.NewLayout                                       = VkImageAspectFlagBits(image_aspect) == VK_IMAGE_ASPECT_DEPTH_BIT ? Specifications::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : Specifications::ImageLayout::SHADER_READ_ONLY_OPTIMAL;
                        barrier_spec_0.ImageAspectMask                                 = VkImageAspectFlagBits(image_aspect);
                        barrier_spec_0.SourceAccessMask                                = VK_ACCESS_TRANSFER_WRITE_BIT;
                        barrier_spec_0.DestinationAccessMask                           = VK_ACCESS_NONE;
                        barrier_spec_0.SourceStageMask                                 = VK_PIPELINE_STAGE_TRANSFER_BIT;
                        barrier_spec_0.DestinationStageMask                            = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                        barrier_spec_0.LayerCount                                      = spec.LayerCount;
                        barrier_spec_0.SourceQueueFamily                               = Renderer->Device->TransferFamilyIndex;
                        barrier_spec_0.DestinationQueueFamily                          = Renderer->Device->GraphicFamilyIndex;
                        Primitives::ImageMemoryBarrier barrier_0{barrier_spec_0};
                        auto                           command_buffer_0 = m_buffer_manager.GetInstantCommandBuffer(QueueType::TRANSFER_QUEUE, Renderer->Device->CurrentFrameIndex);
                        {
                            command_buffer_0->TransitionImageLayout(barrier_0);
                        }
                        m_buffer_manager.EndInstantCommandBuffer(command_buffer_0, Renderer->Device);
                    }

                    VkAccessFlags                                   access_flag  = Renderer->Device->HasSeperateTransfertQueueFamily ? VK_ACCESS_NONE : VK_ACCESS_TRANSFER_WRITE_BIT;
                    VkPipelineStageFlagBits                         src_stage    = Renderer->Device->HasSeperateTransfertQueueFamily ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT;

                    Specifications::ImageMemoryBarrierSpecification barrier_spec = {};
                    barrier_spec.ImageHandle                                     = image_handle;
                    barrier_spec.OldLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
                    barrier_spec.NewLayout                                       = VkImageAspectFlagBits(image_aspect) == VK_IMAGE_ASPECT_DEPTH_BIT ? Specifications::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : Specifications::ImageLayout::SHADER_READ_ONLY_OPTIMAL;
                    barrier_spec.ImageAspectMask                                 = VkImageAspectFlagBits(image_aspect);
                    barrier_spec.SourceAccessMask                                = access_flag;
                    barrier_spec.DestinationAccessMask                           = VK_ACCESS_SHADER_READ_BIT;
                    barrier_spec.SourceStageMask                                 = src_stage;
                    barrier_spec.DestinationStageMask                            = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    barrier_spec.LayerCount                                      = spec.LayerCount;
                    barrier_spec.SourceQueueFamily                               = Renderer->Device->TransferFamilyIndex;
                    barrier_spec.DestinationQueueFamily                          = Renderer->Device->GraphicFamilyIndex;
                    Primitives::ImageMemoryBarrier barrier{barrier_spec};

                    auto                           command_buffer = m_buffer_manager.GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE, Renderer->Device->CurrentFrameIndex);
                    {
                        command_buffer->TransitionImageLayout(barrier);
                    }
                    m_buffer_manager.EndInstantCommandBuffer(command_buffer, Renderer->Device);

                    Renderer->Device->TextureHandleToUpdates.Enqueue(tr.Handle);
                }
            }

            // Processing upload requests
            if (m_upload_requests.Size())
            {
                TextureUploadRequest upload_request;
                if (m_upload_requests.Pop(upload_request))
                {
                    auto&    texture        = Renderer->Device->GlobalTextures->Access(upload_request.Handle);
                    uint32_t image_aspect   = (texture->Specification.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

                    auto     command_buffer = m_buffer_manager.GetInstantCommandBuffer(QueueType::TRANSFER_QUEUE, Renderer->Device->CurrentFrameIndex);
                    {
                        auto                                            image_handle   = texture->ImageBuffer->GetHandle();
                        auto&                                           image_buffer   = texture->ImageBuffer->GetBuffer();

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

                        BufferView staging_buffer = Renderer->Device->CreateBuffer(upload_request.BufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
                        Renderer->Device->MapAndCopyToMemory(staging_buffer, upload_request.BufferSize, upload_request.TextureSpec.Data);
                        command_buffer->CopyBufferToImage(staging_buffer, image_buffer, upload_request.TextureSpec.Width, upload_request.TextureSpec.Height, upload_request.TextureSpec.LayerCount, barrier_0.GetHandle().newLayout);
                        Renderer->Device->EnqueueBufferForDeletion(staging_buffer);
                    }
                    m_buffer_manager.EndInstantCommandBuffer(command_buffer, Renderer->Device, VK_PIPELINE_STAGE_TRANSFER_BIT);

                    UpdateTextureRequest tr = {.Handle = upload_request.Handle};

                    m_update_texture_request.Emplace(std::move(tr));

                    /* Cleanup resource */
                    ZENGINE_CLEAR_STD_VECTOR(m_temp_buffer)
                }
            }

            // Processing file requests
            TextureFileRequest file_request;
            if (m_file_requests.Pop(file_request))
            {
                const std::set<std::string_view>     known_cubmap_file_ext = {".hdr", ".exr"};
                auto                                 file_ext              = std::filesystem::path(file_request.Filename).extension().string();

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

                    spec.LayerCount                                = 6;
                    spec.Format                                    = Specifications::ImageFormat::R32G32B32A32_SFLOAT;

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

                    spec.Width                     = cubemap.Width;
                    spec.Height                    = cubemap.Height;
                    size_t buffer_size             = cubemap.Buffer.size();
                    size_t buffer_byte             = buffer_size * sizeof(uint8_t);
                    m_temp_buffer.resize(buffer_size);
                    Helpers::secure_memmove(m_temp_buffer.data(), buffer_byte, cubemap.Buffer.data(), buffer_byte);
                }
                else
                {
                    stbi_set_flip_vertically_on_load(1);

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
                        m_temp_buffer.resize(buffer_size);
                        stbir_resize_uint8(image_data, width, height, 0, m_temp_buffer.data(), width, height, 0, 4);

                        for (int i = 0; i < total_pixel; ++i)
                        {
                            int offset = i * 4; // RGBA format (4 channels)

                            if (channel == 1)
                            {
                                m_temp_buffer[offset + 3] = 255;
                            }
                            else if (channel == 2)
                            {
                                m_temp_buffer[offset + 3] = image_data[i * 2 + 1];
                            }
                            else if (channel == 3)
                            {
                                m_temp_buffer[offset + 3] = 255;
                            }
                        }
                    }
                    else
                    {
                        size_t total_pixel = width * height;
                        size_t buffer_size = total_pixel * channel;
                        m_temp_buffer.resize(buffer_size, 0);
                        Helpers::secure_memmove(m_temp_buffer.data(), buffer_size, image_data, buffer_size);
                    }

                    spec.Width      = width;
                    spec.Height     = height;
                    spec.LayerCount = 1;
                    spec.Format     = Specifications::ImageFormat::R8G8B8A8_SRGB;

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
