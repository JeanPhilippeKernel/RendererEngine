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

    void GraphicRenderer::Initialize(Hardwares::VulkanDevicePtr device)
    {
        Device                                  = device;
        RenderGraph                             = ZPushStructCtorArgs(Device->Arena, Renderers::RenderGraph);
        AsyncLoader                             = ZPushStructCtor(Device->Arena, AsyncResourceLoader);
        ImguiRenderer                           = ZPushStructCtor(Device->Arena, ImGUIRenderer);
        RenderSceneData                         = ZPushStructCtor(Device->Arena, Scenes::SceneData);
        /*
         * Shared Buffers
         */
        SceneCameraBufferHandle                 = Device->CreateUniformBufferSet();

        RenderSceneData->VertexBufferHandle     = Device->CreateStorageBufferSet();
        RenderSceneData->IndexBufferHandle      = Device->CreateStorageBufferSet();
        RenderSceneData->TransformBufferHandle  = Device->CreateStorageBufferSet();
        RenderSceneData->RenderDataBufferHandle = Device->CreateStorageBufferSet();
        RenderSceneData->MaterialBufferHandle   = Device->CreateStorageBufferSet();
        RenderSceneData->IndirectBufferHandle   = Device->CreateIndirectBufferSet();

        auto scene_camera                       = Device->UniformBufferSetManager.Access(SceneCameraBufferHandle);

        auto vtx_buffer_set                     = Device->StorageBufferSetManager.Access(RenderSceneData->VertexBufferHandle);
        auto idx_buffer_set                     = Device->StorageBufferSetManager.Access(RenderSceneData->IndexBufferHandle);
        auto tranform_buffer_set                = Device->StorageBufferSetManager.Access(RenderSceneData->TransformBufferHandle);
        auto rd_buffer_set                      = Device->StorageBufferSetManager.Access(RenderSceneData->RenderDataBufferHandle);
        auto material_buffer_set                = Device->StorageBufferSetManager.Access(RenderSceneData->MaterialBufferHandle);
        auto indirect_buffer_set                = Device->IndirectBufferSetManager.Access(RenderSceneData->IndirectBufferHandle);

        for (int i = 0; i < Device->SwapchainImageCount; ++i)
        {
            scene_camera->At(i)->Allocate(sizeof(UBOCameraLayout), SceneCameraBufferName);

            vtx_buffer_set->At(i)->Allocate(DefaultBufferSize, VertexBufferName);
            idx_buffer_set->At(i)->Allocate(DefaultBufferSize, IndexBufferName);
            tranform_buffer_set->At(i)->Allocate(DefaultBufferSize, TransformBufferName);
            rd_buffer_set->At(i)->Allocate(DefaultBufferSize, RenderDataBufferName);
            material_buffer_set->At(i)->Allocate(DefaultBufferSize, MaterialBufferName);
            indirect_buffer_set->At(i)->Allocate(DefaultBufferSize, "indirectbuffer");
        }

        /*
         * Renderer Passes
         */
        auto initial_pass        = ZPushStructCtor(Device->Arena, InitialPass);
        auto scene_depth_prepass = ZPushStructCtor(Device->Arena, DepthPrePass);
        auto skybox_pass         = ZPushStructCtor(Device->Arena, SkyboxPass);
        auto grid_pass           = ZPushStructCtor(Device->Arena, GridPass);
        auto gbuffer_pass        = ZPushStructCtor(Device->Arena, GbufferPass);
        auto lighting_pass       = ZPushStructCtor(Device->Arena, LightingPass);

        FrameColorRenderTarget   = Device->CreateTexture({.PerformTransition = false, .Width = 1280, .Height = 780, .Format = ImageFormat::R8G8B8A8_UNORM});
        FrameDepthRenderTarget   = Device->CreateTexture({.PerformTransition = false, .Width = 1280, .Height = 780, .Format = ImageFormat::DEPTH_STENCIL_FROM_DEVICE});

        Device->TextureHandleToUpdates.Enqueue(FrameColorRenderTarget);
        /*
         * Subsystems initialization
         */
        RenderGraph->Initialize(Device->Arena, this);
        AsyncLoader->Initialize(this);
        ImguiRenderer->Initialize(Device, this->RenderGraph->RenderPassBuilder);
        /*
         * Render Graph definition
         */
        RenderGraph->Builder->AttachRenderTarget(FrameDepthRenderTargetName, FrameDepthRenderTarget);
        RenderGraph->Builder->AttachRenderTarget(FrameColorRenderTargetName, FrameColorRenderTarget);

        RenderGraph->Builder->CreateBufferSet("g_scene_directional_light_buffer");
        RenderGraph->Builder->CreateBufferSet("g_scene_point_light_buffer");
        RenderGraph->Builder->CreateBufferSet("g_scene_spot_light_buffer");

        RenderGraph->AddCallbackPass("Initial Pass", initial_pass);
        RenderGraph->AddCallbackPass("Depth Pre-Pass", scene_depth_prepass);
        // RenderGraph->AddCallbackPass("Skybox Pass", skybox_pass);
        RenderGraph->AddCallbackPass("Grid Pass", grid_pass);
        RenderGraph->AddCallbackPass("G-Buffer Pass", gbuffer_pass);
        //  RenderGraph->AddCallbackPass("Lighting Pass", lighting_pass);

        RenderGraph->Setup();
        RenderGraph->Compile();
    }

    void GraphicRenderer::Deinitialize()
    {
        AsyncLoader->Shutdown();

        RenderGraph->Dispose();
        Device->GlobalTextures.Remove(FrameColorRenderTarget);
        Device->GlobalTextures.Remove(FrameDepthRenderTarget);

        ImguiRenderer->Deinitialize();
    }

    void GraphicRenderer::DrawScene(Hardwares::CommandBufferPtr const cb, Cameras::Camera* const camera)
    {
        auto ubo_camera_data = UBOCameraLayout{.View = camera->GetViewMatrix(), .Projection = camera->GetPerspectiveMatrix(), .Position = glm::vec4(camera->GetPosition(), 1.0f)};

        auto buffer_set      = Device->UniformBufferSetManager.Access(SceneCameraBufferHandle);
        auto camera_buf      = buffer_set->At(Device->CurrentFrameIndex);

        camera_buf->Write(reinterpret_cast<void*>(&ubo_camera_data), sizeof(UBOCameraLayout));

        RenderGraph->Execute(cb, RenderSceneData);
    }

    Textures::TextureHandle GraphicRenderer::GetFrameOutput()
    {
        return RenderGraph->GetRenderTarget(FrameColorRenderTargetName);
    }

    // AsyncResourceLoader
    //
    void AsyncResourceLoader::Initialize(GraphicRenderer* renderer)
    {
        Renderer = renderer;
        m_buffer_manager.Initialize(Renderer->Device);
        Helpers::ThreadPoolHelper::Submit([this] { Run(); });
    }

    Textures::TextureHandle AsyncResourceLoader::LoadTextureFile(std::string_view filename)
    {
        std::unique_lock l(m_mutex_2);

        auto             abs_filename = std::filesystem::absolute(filename).string();

        int              w, h, ch;
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

        TextureFileRequest tex_file_req       = {};
        tex_file_req.Filename                 = filename.data();
        tex_file_req.TextureSpec              = spec;
        tex_file_req.TextureSpec.BytePerPixel = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(spec.Format)];
        tex_file_req.Handle                   = Renderer->Device->CreateTexture(tex_file_req.TextureSpec);

        m_file_requests.Enqueue(tex_file_req);
        m_cond.notify_one();

        return tex_file_req.Handle;
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
                    auto     texture      = Renderer->Device->GlobalTextures.Access(tr.Handle);
                    auto     img_buf      = Renderer->Device->Image2DBufferManager.Access(texture->BufferHandle);
                    auto&    spec         = texture->Specification;
                    auto     image_handle = img_buf->GetHandle();
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
                            img_buf->Layout = barrier_spec_0.NewLayout;
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
                        img_buf->Layout = barrier_spec.NewLayout;
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
                    auto     texture        = Renderer->Device->GlobalTextures.Access(upload_request.Handle);
                    auto     img_buf        = Renderer->Device->Image2DBufferManager.Access(texture->BufferHandle);
                    uint32_t image_aspect   = (texture->Specification.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

                    auto     command_buffer = m_buffer_manager.GetInstantCommandBuffer(QueueType::TRANSFER_QUEUE, Renderer->Device->CurrentFrameIndex);
                    {
                        auto                                            image_handle   = img_buf->GetHandle();
                        auto&                                           image_buffer   = img_buf->GetBuffer();

                        Specifications::ImageMemoryBarrierSpecification barrier_spec_0 = {};
                        barrier_spec_0.ImageHandle                                     = image_handle;
                        barrier_spec_0.OldLayout                                       = img_buf->Layout;
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

                        img_buf->Layout = barrier_spec_0.NewLayout;

                        Renderer->Device->WriteTextureData(command_buffer, upload_request.Handle, upload_request.Buffer.data());
                    }
                    m_buffer_manager.EndInstantCommandBuffer(command_buffer, Renderer->Device, VK_PIPELINE_STAGE_TRANSFER_BIT);

                    UpdateTextureRequest tr = {.Handle = upload_request.Handle};

                    m_update_texture_request.Emplace(std::move(tr));
                }
            }

            // Processing file requests
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

                m_upload_requests.Emplace(std::move(upload_req));
            }
        }
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
} // namespace ZEngine::Rendering::Renderers
