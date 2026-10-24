#pragma once

// Private extension of the existing headless bootstrap. Only the atlas, shader
// metadata and clear/readback oracle are test-owned; labels use the production
// builder, shaders, callback, graph scheduling, descriptors and graphics backends.
namespace
{
    struct GridLabelGraphClearPass final : Renderers::IRenderGraphCallbackPass
    {
        Renderers::GridLabelPass*                   Labels = nullptr;
        ZEngine::Rendering::Textures::TextureHandle Atlas  = {};
        float                                       Depth  = 1.f;

        Specs::GraphicsPipelineDesc                 BuildGraphicsPipelineDescription(ArenaAllocator* arena) const override
        {
            auto description            = Labels->BuildGraphicsPipelineDescription(arena);
            // This clear-only subpass owns a depth attachment. A depth/stencil
            // state is required even though it issues no geometry or depth writes.
            description.EnableDepthTest = true;
            return description;
        }

        bool Register(ZEngine::Hardwares::VulkanDevice*, cstring, const Renderers::RenderGraphFrameContext& context, Renderers::RenderGraphResourceBuilder* builder, Renderers::RenderGraphResourceInspector*) override
        {
            const Specs::TextureSpecification color = {
                .IsUsageTransferSource = true, .IsRenderTargetSized = true, .Width = context.RenderWidth, .Height = context.RenderHeight, .Format = Specs::ImageFormat::R8G8B8A8_UNORM, .ClearColor = {32.f / 255.f, 64.f / 255.f, 128.f / 255.f, 1.f}
            };
            const Specs::TextureSpecification depth = {.IsRenderTargetSized = true, .Width = context.RenderWidth, .Height = context.RenderHeight, .Format = Specs::ImageFormat::DEPTH_STENCIL_FROM_DEVICE};
            return builder->WriteColorAttachment(Renderers::RendererResourceName::FrameColorRenderTargetName, color).Valid() && builder->WriteDepthAttachment(Renderers::RendererResourceName::FrameDepthRenderTargetName, depth).Valid();
        }

        void Prepare(ZEngine::Hardwares::VulkanDevice* device, ZEngine::Rendering::Scenes::SceneData*, Renderers::RenderGraphResourceInspector*, Renderers::RenderPasses::RenderPass* pass) override
        {
            auto* graphic = static_cast<Renderers::RenderPasses::GraphicPass*>(pass);
            // No shader is executed by this clear-only pass. Supply valid unused
            // descriptors; the label callback binds the actual scene-depth image.
            graphic->SetTexture("FontAtlas", Atlas);
            graphic->SetTexture("SceneDepth", Atlas);
            graphic->SetSampler("AtlasSampler", device->GlobalLinearClampToEdgeSamplerImageInfo);
        }

        void Execute(ZEngine::Hardwares::VulkanDevice*, Renderers::RenderGraphResourceInspector*, ZEngine::Rendering::Scenes::SceneData*, Renderers::RenderPasses::RenderPass* pass, ZEngine::Rendering::Buffers::FramebufferVNext* framebuffer, ZEngine::Hardwares::CommandBuffer* commands) override
        {
            ASSERT_TRUE(commands->BeginRenderPass(static_cast<Renderers::RenderPasses::GraphicPass*>(pass), framebuffer ? framebuffer->Handle : VK_NULL_HANDLE, false));
            ClearDepth(pass, commands);
            commands->EndRenderPass();
        }

        void ClearDepth(Renderers::RenderPasses::RenderPass* pass, ZEngine::Hardwares::CommandBuffer* commands) const
        {
            // Produce the occluder each frame, not by changing allocation-time
            // texture clear metadata on an already pooled image.
            const auto*             graphic    = static_cast<Renderers::RenderPasses::GraphicPass*>(pass);
            const VkClearAttachment attachment = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .clearValue = {.depthStencil = {.depth = Depth}}};
            const VkClearRect       rect       = {.rect = {.extent = {graphic->GetRenderAreaWidth(), graphic->GetRenderAreaHeight()}}, .layerCount = 1};
            vkCmdClearAttachments(commands->GetHandle(), 1, &attachment, 1, &rect);
        }
        bool SupportsSecondaryRecording() const override
        {
            return true;
        }
        bool RecordDraw(ZEngine::Hardwares::VulkanDevice*, Renderers::RenderGraphResourceInspector*, ZEngine::Rendering::Scenes::SceneData*, Renderers::RenderPasses::RenderPass* pass, ZEngine::Rendering::Buffers::FramebufferVNext*, ZEngine::Hardwares::CommandBuffer* commands) override
        {
            ClearDepth(pass, commands);
            return true;
        }
    };

    struct GridLabelGraphRecordingProbe final : Renderers::IRenderGraphCallbackPass
    {
        Renderers::GridLabelPass    Labels          = {};
        uint32_t                    PrimaryCalls    = 0;
        uint32_t                    BodyCalls       = 0;
        std::thread::id             RecordingThread = {};

        Specs::GraphicsPipelineDesc BuildGraphicsPipelineDescription(ArenaAllocator* arena) const override
        {
            return Labels.BuildGraphicsPipelineDescription(arena);
        }
        bool Register(ZEngine::Hardwares::VulkanDevice* device, cstring name, const Renderers::RenderGraphFrameContext& context, Renderers::RenderGraphResourceBuilder* builder, Renderers::RenderGraphResourceInspector* inspector) override
        {
            return Labels.Register(device, name, context, builder, inspector);
        }
        void Prepare(ZEngine::Hardwares::VulkanDevice* device, ZEngine::Rendering::Scenes::SceneData* scene, Renderers::RenderGraphResourceInspector* inspector, Renderers::RenderPasses::RenderPass* pass) override
        {
            Labels.Prepare(device, scene, inspector, pass);
        }
        bool SupportsSecondaryRecording() const override
        {
            return true;
        }
        void Execute(ZEngine::Hardwares::VulkanDevice* device, Renderers::RenderGraphResourceInspector* inspector, ZEngine::Rendering::Scenes::SceneData* scene, Renderers::RenderPasses::RenderPass* pass, ZEngine::Rendering::Buffers::FramebufferVNext* framebuffer, ZEngine::Hardwares::CommandBuffer* commands) override
        {
            ++PrimaryCalls;
            RecordingThread = std::this_thread::get_id();
            Labels.Execute(device, inspector, scene, pass, framebuffer, commands);
        }
        bool RecordDraw(ZEngine::Hardwares::VulkanDevice* device, Renderers::RenderGraphResourceInspector* inspector, ZEngine::Rendering::Scenes::SceneData* scene, Renderers::RenderPasses::RenderPass* pass, ZEngine::Rendering::Buffers::FramebufferVNext* framebuffer, ZEngine::Hardwares::CommandBuffer* commands) override
        {
            ++BodyCalls;
            RecordingThread = std::this_thread::get_id();
            return Labels.RecordDraw(device, inspector, scene, pass, framebuffer, commands);
        }
    };

    struct OffscreenGridLabelCycle
    {
        static constexpr uint32_t                   AtlasWidth = 16, AtlasHeight = 8;
        GridLabelGraphRecordingProbe                Probe  = {};
        GridLabelGraphClearPass                     Clear  = {};
        ZEngine::Rendering::Textures::TextureHandle Atlas  = {};
        BufferView                                  Upload = {};
        // Declared last: graph disposal must run before the callback members die.
        OffscreenGizmoGraphCycle                    Owner  = {};

        ~OffscreenGridLabelCycle()
        {
            if (Upload)
            {
                vkDeviceWaitIdle(Owner.Vulkan.Device);
                Owner.Device->GpuMem.FreeBuffer(Upload);
            }
            if (Atlas.Valid())
                Owner.Device->DestroyTexture(Atlas);
        }

        static float AtlasAlpha(int x, int y)
        {
            x                = std::clamp(x, 0, int(AtlasWidth) - 1);
            y                = std::clamp(y, 0, int(AtlasHeight) - 1);
            const int  local = x % 8;
            const bool ink   = x < 8 ? ((local <= 1 || local >= 6 || y >= 6) && y <= 6) : (y < 6 ? (local == y / 2 || local == y / 2 + 1 || local == 6 - y / 2 || local == 7 - y / 2) : (local >= 3 && local <= 4));
            return ink ? 1.f : 0.f;
        }

        static double SampleAlpha(double u, double v)
        {
            const double x = u * AtlasWidth - .5, y = v * AtlasHeight - .5;
            const int    ix = int(std::floor(x)), iy = int(std::floor(y));
            const double fx = x - ix, fy = y - iy;
            return (1.0 - fy) * ((1.0 - fx) * AtlasAlpha(ix, iy) + fx * AtlasAlpha(ix + 1, iy)) + fy * ((1.0 - fx) * AtlasAlpha(ix, iy + 1) + fx * AtlasAlpha(ix + 1, iy + 1));
        }

        void Initialize(Renderers::GizmoPass& unused_gizmo, const std::vector<uint32_t> (&binaries)[2])
        {
            Clear.Labels = &Probe.Labels;
            ASSERT_NO_FATAL_FAILURE(Owner.InitializeGraph(unused_gizmo, binaries, {.Clear = &Clear, .Draw = &Probe, .DrawName = "Grid Labels", .LabelShader = true}));
            auto& device          = *Owner.Device;
            device.PhysicalDevice = Owner.Vulkan.PhysicalDevice;
            device.DefaultDepthFormats.init(&Owner.Arena, 1);
            device.DefaultDepthFormats.push(VK_FORMAT_D32_SFLOAT);
            const VkSamplerCreateInfo sampler              = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST, .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
            device.GlobalLinearClampToEdgeSamplerImageInfo = {.sampler = Owner.Cache->GetOrCreateSampler(sampler)};
            Atlas                                          = device.CreateTexture({.Width = AtlasWidth, .Height = AtlasHeight, .Format = Specs::ImageFormat::R8G8B8A8_UNORM}, "grid_label_test_atlas");
            ASSERT_TRUE(Atlas.Valid());
            Clear.Atlas                                                                      = Atlas;
            ZEngine::Core::Containers::FixedArray<uint32_t, AtlasWidth * AtlasHeight> pixels = {};
            for (uint32_t y = 0; y < AtlasHeight; ++y)
                for (uint32_t x = 0; x < AtlasWidth; ++x)
                    pixels[y * AtlasWidth + x] = 0x00ffffffu | (AtlasAlpha(int(x), int(y)) == 1.f ? 0xff000000u : 0u);
            Upload = device.GpuMem.AllocateBuffer(sizeof(pixels), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, GpuMemoryDomain::HostStaging, "grid_label_test_atlas_upload");
            ASSERT_TRUE(Upload);
            Owner.Manager->UpdateBuffer(Upload, pixels.data(), sizeof(pixels));
            const auto* texture = device.GlobalTextures.Access(Atlas);
            ASSERT_NE(texture, nullptr);
            const auto* image = device.ImageBufferManager.Access(texture->BufferHandle);
            ASSERT_NE(image, nullptr);
            const auto native_image = image->GetBuffer().Handle;
            Owner.Commands->Begin();
            VkImageMemoryBarrier barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = native_image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
            };
            vkCmdPipelineBarrier(Owner.Commands->GetHandle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            const VkBufferImageCopy copy = {
                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                  .imageExtent = {AtlasWidth, AtlasHeight, 1}
            };
            vkCmdCopyBufferToImage(Owner.Commands->GetHandle(), Upload.Handle, native_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkCmdPipelineBarrier(Owner.Commands->GetHandle(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            Owner.Commands->End();
            const auto         command    = Owner.Commands->GetHandle();
            const VkSubmitInfo submission = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command};
            ASSERT_EQ(vkQueueSubmit(Owner.Queue, 1, &submission, Owner.Fence), VK_SUCCESS);
            ASSERT_EQ(vkWaitForFences(Owner.Vulkan.Device, 1, &Owner.Fence, VK_TRUE, 10'000'000'000), VK_SUCCESS);
            ASSERT_EQ(vkResetFences(Owner.Vulkan.Device, 1, &Owner.Fence), VK_SUCCESS);
            ASSERT_EQ(vkResetCommandBuffer(command, 0), VK_SUCCESS);
            Owner.Commands->SetState(ZEngine::Hardwares::CommandBufferState::Idle);
            device.GpuMem.FreeBuffer(Upload);
        }

        void Render(const Renderers::GridLabelFrameData& frame, uint32_t width, uint32_t height, uint8_t slot, float depth, bool workers, bool expected_active)
        {
            Owner.CurrentFrame.Index = slot;
            Owner.Swapchain.FrameAsyncOperations.clear();
            Owner.Swapchain.RenderWorkSubmittedCallbacks.clear();
            Owner.Graph->Resize(width, height);
            Probe.Labels.SetFrame(frame);
            Clear.Depth        = depth;
            const auto primary = Probe.PrimaryCalls, body = Probe.BodyCalls;
            const auto thread                    = std::this_thread::get_id();
            Owner.GraphCommands.TotalThreadCount = workers ? 2 : 0;
            Owner.Commands->Begin();
            ASSERT_EQ(Owner.Graph->Execute(Owner.Commands.get()), Owner.Commands.get());
            Owner.GraphCommands.TotalThreadCount = 2;
            ASSERT_TRUE(Owner.Graph->m_compile_valid);
            const auto* pass = Owner.Graph->GetPass("Grid Labels");
            ASSERT_NE(pass, nullptr);
            EXPECT_EQ(pass->IsActive(), expected_active);
            EXPECT_EQ(Owner.Graph->SortedPassIndices.size(), expected_active ? 3u : 2u);
            if (expected_active)
            {
                EXPECT_EQ(Probe.BodyCalls - body, workers || Owner.Vulkan.DynamicRenderingEnabled ? 1u : 0u);
                EXPECT_EQ(Probe.PrimaryCalls - primary, !workers && !Owner.Vulkan.DynamicRenderingEnabled ? 1u : 0u);
                EXPECT_EQ(Probe.RecordingThread == thread, !workers);
                EXPECT_EQ(pass->SecondaryRecorded, workers);
                EXPECT_EQ(pass->Reads.size(), 2u);
                ASSERT_EQ(pass->Writes.size(), 1u);
                EXPECT_EQ(pass->Writes[0].Access, Renderers::RGAccess::ColorReadWrite);
                EXPECT_EQ(Owner.Sink.Input.Version, pass->Writes[0].Handle.Version);
            }
            else
            {
                EXPECT_EQ(Probe.PrimaryCalls, primary);
                EXPECT_EQ(Probe.BodyCalls, body);
            }
            ASSERT_NO_FATAL_FAILURE(Owner.ReadGraphPixels(width, height));
        }

        void ExpectPixels(const Renderers::GridLabelFrameData& frame, float scene_depth, bool visible) const
        {
            const uint32_t width = Owner.Graph->RenderWidth, height = Owner.Graph->RenderHeight;
            int            bounds[2][4] = {};
            for (uint32_t label = 0; visible && label < frame.Count; ++label)
            {
                const auto& rect = frame.Push.Rects[label];
                bounds[label][0] = int(std::floor((double(rect[0]) + 1.0) * .5 * width));
                bounds[label][1] = int(std::floor((double(rect[1]) + 1.0) * .5 * height));
                bounds[label][2] = int(std::ceil((double(rect[0]) + rect[2] + 1.0) * .5 * width));
                bounds[label][3] = int(std::ceil((double(rect[1]) + rect[3] + 1.0) * .5 * height));
            }
            uint32_t changed = 0;
            for (uint32_t y = 0; y < height; ++y)
                for (uint32_t x = 0; x < width; ++x)
                {
                    const auto actual         = Owner.Pixels[y * width + x];
                    bool       possible_glyph = false;
                    for (uint32_t label = 0; visible && label < frame.Count; ++label)
                        possible_glyph |= int(x) >= bounds[label][0] && int(x) < bounds[label][2] && int(y) >= bounds[label][1] && int(y) < bounds[label][3];
                    // Keep a complete background oracle without expensive float/
                    // gtest work on the hundreds of thousands of untouched pixels.
                    if (actual == 0xff804020u && !possible_glyph)
                        continue;
                    double expected[3] = {32.0, 64.0, 128.0};
                    for (uint32_t label = 0; visible && label < frame.Count; ++label)
                    {
                        const auto&  rect  = frame.Push.Rects[label];
                        const double nx    = (2.0 * (x + .5) / width - 1.0 - rect[0]) / rect[2];
                        const double ny    = (2.0 * (y + .5) / height - 1.0 - rect[1]) / rect[3];
                        const float  depth = frame.Push.Depths[label];
                        if (nx < 0.0 || nx >= 1.0 || ny < 0.0 || ny >= 1.0 || (frame.Push.Depths[2] ? depth + 2.e-6f < scene_depth : depth > scene_depth + 2.e-6f))
                            continue;
                        const auto&  uv    = frame.Push.UVs[label];
                        const auto&  color = frame.Push.Colors[label];
                        const double alpha = color[3] * SampleAlpha(uv[0] + nx * (uv[2] - uv[0]), uv[1] + ny * (uv[3] - uv[1]));
                        if (alpha <= .001)
                            continue;
                        for (uint32_t channel = 0; channel < 3; ++channel)
                            expected[channel] = 255.0 * color[channel] * alpha + expected[channel] * (1.0 - alpha);
                    }
                    changed += actual != 0xff804020u;
                    for (uint32_t channel = 0; channel < 3; ++channel)
                        ASSERT_NEAR(double((actual >> (8 * channel)) & 255u), expected[channel], 1.5) << "pixel=" << x << ',' << y << " channel=" << channel;
                    ASSERT_EQ(actual >> 24, 255u);
                }
            if (visible)
                EXPECT_GT(changed, 0u);
            else
                EXPECT_EQ(changed, 0u);
        }
    };

    void RunGridLabelGraphCycles(bool dynamic_rendering)
    {
        const bool owned_workers = !ZEngine::Helpers::ThreadPoolHelper::Pool;
        if (owned_workers)
            ZEngine::Helpers::ThreadPoolHelper::Pool = ZEngine::Helpers::CreateScope<ZEngine::Helpers::ThreadPool>(3);
        struct WorkerLifetime
        {
            bool Owned = false;
            ~WorkerLifetime()
            {
                if (Owned)
                    ZEngine::Helpers::ThreadPoolHelper::Shutdown();
            }
        } workers                = {.Owned = owned_workers};
        GlslangLifetime compiler = {};
        ASSERT_TRUE(compiler.Ready);
        std::vector<uint32_t> binaries[2] = {};
        ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GridLabelShaders::Vertex, sizeof(ZEngine::Tests::GridLabelShaders::Vertex) - 1, EShLangVertex, binaries[0]));
        ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GridLabelShaders::Fragment, sizeof(ZEngine::Tests::GridLabelShaders::Fragment) - 1, EShLangFragment, binaries[1]));
        Renderers::GizmoPass unused_gizmo = {};
        uint32_t             validated = 0, synchronization_validated = 0, submissions = 0;
        for (uint32_t cycle = 0; cycle < 2; ++cycle)
        {
            SCOPED_TRACE(cycle);
            OffscreenGridLabelCycle fixture = {};
            ASSERT_TRUE(fixture.Owner.Vulkan.Create(true, dynamic_rendering, true));
            if (dynamic_rendering && !fixture.Owner.Vulkan.DynamicRenderingEnabled)
                GTEST_SKIP() << "The selected Vulkan device does not support dynamic rendering";
            ASSERT_NO_FATAL_FAILURE(fixture.Initialize(unused_gizmo, binaries));
            validated                          += fixture.Owner.Vulkan.ValidationEnabled;
            synchronization_validated          += fixture.Owner.Vulkan.SynchronizationValidationEnabled;
            Renderers::GridLabelFrameData last  = {};
            for (const bool orthographic : {false, true})
                for (const bool reverse : {false, true})
                    for (const uint32_t dpi : {1u, 2u})
                    {
                        SCOPED_TRACE(orthographic);
                        SCOPED_TRACE(reverse);
                        SCOPED_TRACE(dpi);
                        using namespace ZEngine;
                        const uint32_t                width = 512 * dpi, height = 384 * dpi;
                        Rendering::Cameras::FlyCamera camera(4.f / 3.f, {});
                        camera.SetPosition({0.f, 0.f, 10.f});
                        camera.SetOrientation(0.f, 0.f);
                        camera.SetProjectionType(orthographic ? Rendering::Cameras::CameraType::ORTHOGRAPHIC : Rendering::Cameras::CameraType::PERSPECTIVE);
                        auto view = camera.CaptureFrameData();
                        if (reverse)
                            for (uint32_t column = 0; column < 4; ++column)
                                view.Projection(2, column) = view.Projection(3, column) - view.Projection(2, column);
                        view.UsesReverseZ                               = reverse;
                        ECS::SceneGridSettings settings                 = {};
                        settings.Origin                                 = {.2f, .3f, 0.f};
                        settings.Orientation                            = {std::sqrt(.5f), 0.f, 0.f, std::sqrt(.5f)};
                        settings.Labels                                 = ECS::SceneGridLabels::UV;
                        settings.FadeStrength                           = 0.f;
                        settings.UColor                                 = {1.f, 0.f, 0.f, 1.f};
                        settings.VColor                                 = {0.f, 1.f, 0.f, .5f};
                        Rendering::Scenes::SceneGridRenderSnapshot grid = {};
                        ASSERT_TRUE(Rendering::Scenes::MakeSceneGridRenderSnapshot(settings, grid));
                        Renderers::GridLabelFontSnapshot font = {.Atlas = fixture.Atlas, .LogicalViewportHeight = 384.f};
                        font.Glyphs[0]                        = {
                            {0.f, 0.f, .5f, 1.f},
                            {8.f, 8.f}
                        };
                        font.Glyphs[1] = {
                            {.5f, 0.f, 1.f, 1.f},
                            {8.f, 8.f}
                        };
                        Renderers::GridLabelFrameData frame = {};
                        ASSERT_TRUE(Renderers::BuildGridLabelFrame(grid, view, font, width, height, frame));
                        ASSERT_EQ(frame.Count, 2u);
                        const bool  secondary = (submissions % 2) != 0;
                        const float clear = reverse ? 0.f : 1.f, occluder = reverse ? 1.f : 0.f;
                        ASSERT_NO_FATAL_FAILURE(fixture.Render(frame, width, height, submissions++ % 3, clear, secondary, true));
                        ASSERT_NO_FATAL_FAILURE(fixture.ExpectPixels(frame, clear, true));
                        ASSERT_NO_FATAL_FAILURE(fixture.Render(frame, width, height, submissions++ % 3, occluder, !secondary, true));
                        ASSERT_NO_FATAL_FAILURE(fixture.ExpectPixels(frame, occluder, false));
                        const float within_tolerance = frame.Push.Depths[0] + (reverse ? 1.e-6f : -1.e-6f);
                        ASSERT_NO_FATAL_FAILURE(fixture.Render(frame, width, height, submissions++ % 3, within_tolerance, secondary, true));
                        ASSERT_NO_FATAL_FAILURE(fixture.ExpectPixels(frame, within_tolerance, true));
                        const float outside_tolerance = frame.Push.Depths[0] + (reverse ? 8.e-6f : -8.e-6f);
                        ASSERT_NO_FATAL_FAILURE(fixture.Render(frame, width, height, submissions++ % 3, outside_tolerance, !secondary, true));
                        ASSERT_NO_FATAL_FAILURE(fixture.ExpectPixels(frame, outside_tolerance, false));
                        Renderers::GridLabelFrameData invalid = frame;
                        invalid.Atlas.Generation++;
                        ASSERT_NO_FATAL_FAILURE(fixture.Render(invalid, width, height, submissions++ % 3, clear, secondary, false));
                        ASSERT_NO_FATAL_FAILURE(fixture.ExpectPixels({}, clear, false));
                        ASSERT_NO_FATAL_FAILURE(fixture.Render({}, width, height, submissions++ % 3, clear, !secondary, false));
                        ASSERT_NO_FATAL_FAILURE(fixture.ExpectPixels({}, clear, false));
                        invalid                = frame;
                        invalid.Push.UVs[0][0] = std::numeric_limits<float>::quiet_NaN();
                        ASSERT_NO_FATAL_FAILURE(fixture.Render(invalid, width, height, submissions++ % 3, clear, secondary, false));
                        ASSERT_NO_FATAL_FAILURE(fixture.ExpectPixels({}, clear, false));
                        auto clipped             = frame;
                        clipped.Push.Rects[0][0] = -1.f - .5f * clipped.Push.Rects[0][2];
                        clipped.Push.Rects[1][0] = 1.f - .5f * clipped.Push.Rects[1][2];
                        ASSERT_NO_FATAL_FAILURE(fixture.Render(clipped, width, height, submissions++ % 3, clear, !secondary, true));
                        ASSERT_NO_FATAL_FAILURE(fixture.ExpectPixels(clipped, clear, true));
                        ASSERT_NO_FATAL_FAILURE(fixture.Render(frame, width, height, submissions++ % 3, clear, secondary, true));
                        ASSERT_NO_FATAL_FAILURE(fixture.ExpectPixels(frame, clear, true));
                        last = frame;
                    }
            const auto         arena_offset = fixture.Owner.Arena.m_current_offset;
            VmaTotalStatistics before = {}, after = {};
            vmaCalculateStatistics(fixture.Owner.Device->GpuMem.Allocator, &before);
            const auto pipeline = static_cast<Renderers::RenderPasses::GraphicPass*>(fixture.Owner.Graph->GetPass("Grid Labels")->Handle)->Pipeline->Handle;
            for (uint32_t repeat = 0; repeat < 8; ++repeat)
            {
                ASSERT_NO_FATAL_FAILURE(fixture.Render(last, 1024, 768, submissions++ % 3, 0.f, repeat % 2, true));
                EXPECT_EQ(static_cast<Renderers::RenderPasses::GraphicPass*>(fixture.Owner.Graph->GetPass("Grid Labels")->Handle)->Pipeline->Handle, pipeline);
            }
            ASSERT_NO_FATAL_FAILURE(fixture.ExpectPixels(last, 0.f, true));
            vmaCalculateStatistics(fixture.Owner.Device->GpuMem.Allocator, &after);
            EXPECT_EQ(fixture.Owner.Arena.m_current_offset, arena_offset);
            EXPECT_EQ(after.total.statistics.allocationCount, before.total.statistics.allocationCount);
            EXPECT_EQ(after.total.statistics.allocationBytes, before.total.statistics.allocationBytes);
        }
        ::testing::Test::RecordProperty("fresh_grid_label_device_cycles", 2);
        ::testing::Test::RecordProperty("grid_label_submissions", submissions);
        ::testing::Test::RecordProperty("validated_grid_label_device_cycles", validated);
        ::testing::Test::RecordProperty("synchronization_validated_grid_label_device_cycles", synchronization_validated);
        ::testing::Test::RecordProperty("dynamic_rendering", dynamic_rendering ? 1 : 0);
        ::testing::Test::RecordProperty("warmed_allocation_stability_frames", 16);
    }
} // namespace

TEST_F(GpuAllocatorTest, GridLabelPixelsRespectProjectionDepthDpiResizeAndFreshDevices)
{
    ASSERT_NE(s_allocator, nullptr);
    RunGridLabelGraphCycles(false);
}

TEST_F(GpuAllocatorTest, DynamicGridLabelPixelsRespectProjectionDepthDpiResizeAndFreshDevices)
{
    ASSERT_NE(s_allocator, nullptr);
    RunGridLabelGraphCycles(true);
}
