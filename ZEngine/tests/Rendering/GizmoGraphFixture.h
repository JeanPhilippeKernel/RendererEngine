#pragma once

// Private extension of the offscreen bootstrap in GpuAllocatorTest.cpp. The
// production graph owns registration, compilation, targets, barriers, backend
// pipeline creation, framebuffer resolution and primary/secondary execution.
namespace
{
    struct GizmoGraphClearPass final : Renderers::IRenderGraphCallbackPass
    {
        Renderers::GizmoPass*       Template = nullptr;

        Specs::GraphicsPipelineDesc BuildGraphicsPipelineDescription(ArenaAllocator* arena) const override
        {
            return Template->BuildGraphicsPipelineDescription(arena);
        }

        bool Register(ZEngine::Hardwares::VulkanDevice*, cstring, const Renderers::RenderGraphFrameContext& context, Renderers::RenderGraphResourceBuilder* builder, Renderers::RenderGraphResourceInspector*) override
        {
            Specs::TextureSpecification target = {
                .IsUsageTransferSource = true, .IsRenderTargetSized = true, .Width = context.RenderWidth, .Height = context.RenderHeight, .Format = Specs::ImageFormat::R8G8B8A8_UNORM, .ClearColor = {32.f / 255.f, 64.f / 255.f, 128.f / 255.f, 1.f}
            };
            return builder->WriteColorAttachment(Renderers::RendererResourceName::FrameColorRenderTargetName, target).Valid();
        }

        void Prepare(ZEngine::Hardwares::VulkanDevice* device, ZEngine::Rendering::Scenes::SceneData* scene, Renderers::RenderGraphResourceInspector* inspector, Renderers::RenderPasses::RenderPass* pass) override
        {
            Template->Prepare(device, scene, inspector, pass);
        }

        void Execute(ZEngine::Hardwares::VulkanDevice*, Renderers::RenderGraphResourceInspector*, ZEngine::Rendering::Scenes::SceneData*, Renderers::RenderPasses::RenderPass* pass, ZEngine::Rendering::Buffers::FramebufferVNext* framebuffer, ZEngine::Hardwares::CommandBuffer* commands) override
        {
            ASSERT_TRUE(commands->BeginRenderPass(static_cast<Renderers::RenderPasses::GraphicPass*>(pass), framebuffer ? framebuffer->Handle : VK_NULL_HANDLE, false));
            commands->EndRenderPass();
        }

        bool SupportsSecondaryRecording() const override
        {
            return true;
        }

        bool RecordDraw(ZEngine::Hardwares::VulkanDevice*, Renderers::RenderGraphResourceInspector*, ZEngine::Rendering::Scenes::SceneData*, Renderers::RenderPasses::RenderPass*, ZEngine::Rendering::Buffers::FramebufferVNext*, ZEngine::Hardwares::CommandBuffer*) override
        {
            // The graph's attachment CLEAR is the complete body of this pass.
            return true;
        }
    };

    struct GizmoGraphRecordingProbe final : Renderers::IRenderGraphCallbackPass
    {
        Renderers::GizmoPass*       Pass            = nullptr;
        uint32_t                    PrimaryCalls    = 0;
        uint32_t                    BodyCalls       = 0;
        std::thread::id             RecordingThread = {};

        Specs::GraphicsPipelineDesc BuildGraphicsPipelineDescription(ArenaAllocator* arena) const override
        {
            return Pass->BuildGraphicsPipelineDescription(arena);
        }
        bool Register(ZEngine::Hardwares::VulkanDevice* device, cstring name, const Renderers::RenderGraphFrameContext& context, Renderers::RenderGraphResourceBuilder* builder, Renderers::RenderGraphResourceInspector* inspector) override
        {
            return Pass->Register(device, name, context, builder, inspector);
        }
        void Prepare(ZEngine::Hardwares::VulkanDevice* device, ZEngine::Rendering::Scenes::SceneData* scene, Renderers::RenderGraphResourceInspector* inspector, Renderers::RenderPasses::RenderPass* pass) override
        {
            Pass->Prepare(device, scene, inspector, pass);
        }
        bool SupportsSecondaryRecording() const override
        {
            return Pass->SupportsSecondaryRecording();
        }
        void Execute(ZEngine::Hardwares::VulkanDevice* device, Renderers::RenderGraphResourceInspector* inspector, ZEngine::Rendering::Scenes::SceneData* scene, Renderers::RenderPasses::RenderPass* pass, ZEngine::Rendering::Buffers::FramebufferVNext* framebuffer, ZEngine::Hardwares::CommandBuffer* commands) override
        {
            ++PrimaryCalls;
            RecordingThread = std::this_thread::get_id();
            Pass->Execute(device, inspector, scene, pass, framebuffer, commands);
        }
        bool RecordDraw(ZEngine::Hardwares::VulkanDevice* device, Renderers::RenderGraphResourceInspector* inspector, ZEngine::Rendering::Scenes::SceneData* scene, Renderers::RenderPasses::RenderPass* pass, ZEngine::Rendering::Buffers::FramebufferVNext* framebuffer, ZEngine::Hardwares::CommandBuffer* commands) override
        {
            ++BodyCalls;
            RecordingThread = std::this_thread::get_id();
            return Pass->RecordDraw(device, inspector, scene, pass, framebuffer, commands);
        }
        void Deinitialize(ZEngine::Hardwares::VulkanDevice* device) override
        {
            Pass->Deinitialize(device);
        }
    };

    struct GizmoGraphSink final : Renderers::IRenderGraphCallbackPass
    {
        uint32_t                    Executions = 0;
        Renderers::RGResourceHandle Input      = {};

        bool                        RequiresRenderPass() const override
        {
            return false;
        }
        Renderers::RGPassFlags GetPassFlags() const override
        {
            return Renderers::RGPassFlags::NeverCull;
        }
        bool Register(ZEngine::Hardwares::VulkanDevice*, cstring, const Renderers::RenderGraphFrameContext&, Renderers::RenderGraphResourceBuilder* builder, Renderers::RenderGraphResourceInspector*) override
        {
            Input = builder->ReadTexture(Renderers::RendererResourceName::FrameColorRenderTargetName);
            return Input.Valid();
        }
        void Execute(ZEngine::Hardwares::VulkanDevice*, Renderers::RenderGraphResourceInspector*, ZEngine::Rendering::Scenes::SceneData*, Renderers::RenderPasses::RenderPass*, ZEngine::Rendering::Buffers::FramebufferVNext*, ZEngine::Hardwares::CommandBuffer*) override
        {
            ++Executions;
        }
    };

    struct OffscreenGizmoGraphCycle : OffscreenGizmoCycle
    {
        ZEngine::Hardwares::CommandBufferManager GraphCommands = {};
        ZEngine::Rendering::Scenes::SceneData    Scene         = {};
        GizmoGraphClearPass                      Clear         = {};
        GizmoGraphRecordingProbe                 Probe         = {};
        GizmoGraphSink                           Sink          = {};
        bool                                     CommandsReady = false;
        bool                                     GraphReady    = false;

        ~OffscreenGizmoGraphCycle()
        {
            if (Vulkan.Device)
                vkDeviceWaitIdle(Vulkan.Device);
            if (GraphReady)
            {
                // Retire framebuffer handles before the graph's views/images.
                for (auto& pass : Graph->PersistentPasses)
                    if (pass.Framebuffer)
                        pass.Framebuffer->Dispose();
                Graph->Dispose();
                for (auto& pass : Graph->PersistentPasses)
                    if (pass.Handle)
                    {
                        // The graph, not GraphicPass::Dispose's legacy path,
                        // owns these texture handles.
                        pass.Handle->Specification.ExternalOutputs.clear();
                        static_cast<Renderers::RenderPasses::GraphicPass*>(pass.Handle)->~GraphicPass();
                        pass.Handle = nullptr;
                    }
                // Match the device's shutdown drain without bootstrapping a
                // surface, presentation timeline or unrelated shader lifecycle.
                // DestroyTexture queues texture handles; PendingFree alone does
                // not consume that queue.
                ZEngine::Hardwares::TextureDisposeEntry entry = {};
                while (Device->TextureHandleToDispose.pop(entry))
                {
                    auto* texture = Device->GlobalTextures.Access(entry.Handle);
                    if (!texture)
                        continue;
                    if (auto* image = Device->ImageBufferManager.Access(texture->BufferHandle))
                        image->Dispose();
                    Device->ImageBufferManager.Remove(texture->BufferHandle);
                    Device->GlobalTextures.Remove(entry.Handle);
                }
                Device->PendingFree.Drain(&Device->GpuMem, Vulkan.Device, UINT64_MAX);
                EXPECT_EQ(Device->GlobalTextures.Size(), 0u);
                EXPECT_EQ(Device->ImageBufferManager.Size(), 0u);
            }
            if (CommandsReady)
                GraphCommands.Deinitialize();
            Device->CommandBufferMgr = nullptr;
        }

        struct GraphRecipe
        {
            Renderers::IRenderGraphCallbackPass* Clear       = nullptr;
            Renderers::IRenderGraphCallbackPass* Draw        = nullptr;
            cstring                              DrawName    = "Gizmo Pass";
            bool                                 LabelShader = false;
        };

        void InitializeGraph(Renderers::GizmoPass& pass, const std::vector<uint32_t> (&binaries)[2])
        {
            InitializeGraph(pass, binaries, {});
        }

        void InitializeGraph(Renderers::GizmoPass& pass, const std::vector<uint32_t> (&binaries)[2], const GraphRecipe& recipe)
        {
            ASSERT_NO_FATAL_FAILURE(Initialize(pass, binaries, true, recipe.LabelShader));
            Device->GlobalTextures.Initialize(&Arena, 8);
            Device->ImageBufferManager.Initialize(&Arena, 8);
            Device->MaxGlobalTexture               = 8;
            Device->PhysicalDeviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            vkGetPhysicalDeviceProperties2(Vulkan.PhysicalDevice, &Device->PhysicalDeviceProperties);
            Swapchain.Device               = Device.get();
            Swapchain.SwapchainImageWidth  = 96;
            Swapchain.SwapchainImageHeight = 64;
            Swapchain.FrameAsyncOperations.init(&Arena, 8);
            Swapchain.RenderWorkSubmittedCallbacks.init(&Arena, 4);
            RRMUploadBatchTestHelper::InitializeGraphicsQueue(*Device, Arena, Queue);
            GraphCommands.Initialize(Device.get(), 3, 2);
            CommandsReady            = true;
            Device->CommandBufferMgr = &GraphCommands;
            Graph->Initialize(Device.get(), &Scene);
            GraphReady     = true;
            Clear.Template = &pass;
            Probe.Pass     = &pass;
            Graph->AddCallbackPass("Fixture clear", recipe.Clear ? recipe.Clear : &Clear);
            Graph->AddCallbackPass(recipe.DrawName, recipe.Draw ? recipe.Draw : &Probe);
            Graph->AddCallbackPass("Fixture sink", &Sink);
        }

        void RenderGraphFrame(const Renderers::GizmoFrameData& frame, uint8_t slot, uint32_t width, uint32_t height, bool workers)
        {
            CurrentFrame.Index     = slot;
            Scene.CameraHeapOffset = (slot % 2) * CameraStride;
            Swapchain.FrameAsyncOperations.clear();
            Swapchain.RenderWorkSubmittedCallbacks.clear();
            Graph->Resize(width, height);
            Pass->SetFrame(frame);
            const uint32_t before_primary  = Probe.PrimaryCalls;
            const uint32_t before_body     = Probe.BodyCalls;
            const uint32_t before_sink     = Sink.Executions;
            const auto     render_thread   = std::this_thread::get_id();
            GraphCommands.TotalThreadCount = workers ? 2 : 0;
            Commands->Begin();
            ASSERT_EQ(Graph->Execute(Commands.get()), Commands.get());
            GraphCommands.TotalThreadCount = 2;
            ASSERT_TRUE(Graph->m_compile_valid);
            ASSERT_EQ(Graph->QueueBatches.size(), 1u);
            ASSERT_EQ(Sink.Executions, before_sink + 1);
            ASSERT_EQ(Graph->SortedPassIndices.size(), frame.Empty() ? 2u : 3u);
            auto* gizmo = Graph->GetPass("Gizmo Pass");
            ASSERT_NE(gizmo, nullptr);
            EXPECT_EQ(gizmo->IsActive(), !frame.Empty());
            if (!frame.Empty())
            {
                ASSERT_NE(gizmo->Handle, nullptr);
                auto* graphic = static_cast<Renderers::RenderPasses::GraphicPass*>(gizmo->Handle);
                EXPECT_EQ(graphic->GetRenderAreaWidth(), width);
                EXPECT_EQ(graphic->GetRenderAreaHeight(), height);
                EXPECT_EQ(gizmo->Writes[0].Access, Renderers::RGAccess::ColorReadWrite);
                EXPECT_EQ(Sink.Input.Version, gizmo->Writes[0].Handle.Version);
                if (workers)
                {
                    ASSERT_TRUE(gizmo->SecondaryRecorded);
                    ASSERT_NE(gizmo->Secondary, nullptr);
                    EXPECT_NE(gizmo->Secondary->GetHandle(), Commands->GetHandle());
                    EXPECT_NE(Probe.RecordingThread, render_thread);
                    EXPECT_EQ(Probe.BodyCalls, before_body + 1);
                    EXPECT_EQ(Probe.PrimaryCalls, before_primary);
                }
                else
                {
                    EXPECT_FALSE(gizmo->SecondaryRecorded);
                    EXPECT_EQ(gizmo->Secondary, nullptr);
                    EXPECT_EQ(Probe.RecordingThread, render_thread);
                    EXPECT_EQ(Probe.BodyCalls, before_body + (Vulkan.DynamicRenderingEnabled ? 1 : 0));
                    EXPECT_EQ(Probe.PrimaryCalls, before_primary + (Vulkan.DynamicRenderingEnabled ? 0 : 1));
                }
            }
            else
            {
                EXPECT_EQ(Probe.BodyCalls, before_body);
                EXPECT_EQ(Probe.PrimaryCalls, before_primary);
            }

            ReadGraphPixels(width, height);
        }

        void ReadGraphPixels(uint32_t width, uint32_t height)
        {
            ASSERT_LE(size_t(width) * height, Pixels.size());
            // Test-only readback follows the compiled graph's sampled-image sink.
            // Restore that layout afterward; graph scheduling never relies on an
            // unannounced transfer state from this independent pixel oracle.
            const auto  texture_handle = Graph->ResourceInspector->GetTextureHandle(Sink.Input);
            const auto* texture        = Device->GlobalTextures.Access(texture_handle);
            ASSERT_NE(texture, nullptr);
            auto* image = Device->ImageBufferManager.Access(texture->BufferHandle);
            ASSERT_NE(image, nullptr);
            const auto                    native_image = image->GetBuffer().Handle;
            const VkImageSubresourceRange range        = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkImageMemoryBarrier          barrier      = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_READ_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT, .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = native_image, .subresourceRange = range};
            vkCmdPipelineBarrier(Commands->GetHandle(), VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            const VkBufferImageCopy copy = {
                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                  .imageExtent = {width, height, 1}
            };
            vkCmdCopyImageToBuffer(Commands->GetHandle(), native_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Readback.Handle, 1, &copy);
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkCmdPipelineBarrier(Commands->GetHandle(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            const VkBufferMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .buffer = Readback.Handle, .offset = 0, .size = width * height * sizeof(uint32_t)};
            vkCmdPipelineBarrier(Commands->GetHandle(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host, 0, nullptr);
            Commands->End();
            const auto         command    = Commands->GetHandle();
            const VkSubmitInfo submission = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command};
            ASSERT_EQ(vkQueueSubmit(Queue, 1, &submission, Fence), VK_SUCCESS);
            ASSERT_EQ(vkWaitForFences(Vulkan.Device, 1, &Fence, VK_TRUE, 10'000'000'000), VK_SUCCESS);
            ASSERT_EQ(vmaCopyAllocationToMemory(Device->GpuMem.Allocator, Readback.Allocation, 0, Pixels.data(), width * height * sizeof(uint32_t)), VK_SUCCESS);
            ASSERT_EQ(vkResetFences(Vulkan.Device, 1, &Fence), VK_SUCCESS);
            ASSERT_EQ(vkResetCommandBuffer(command, 0), VK_SUCCESS);
            Commands->SetState(ZEngine::Hardwares::CommandBufferState::Idle);
            Device->PendingFree.Drain(&Device->GpuMem, Vulkan.Device, UINT64_MAX);
            EXPECT_EQ(Vulkan.ValidationErrors.load(std::memory_order_relaxed), 0u);
        }

        uint32_t Pixel(float x, float y) const
        {
            const auto column = static_cast<uint32_t>((x + 1.f) * .5f * Graph->RenderWidth);
            const auto row    = static_cast<uint32_t>((y + 1.f) * .5f * Graph->RenderHeight);
            return Pixels[row * Graph->RenderWidth + column];
        }
    };

    void RunGizmoGraphCycles(bool dynamic_rendering)
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
        } worker_lifetime        = {.Owned = owned_workers};
        GlslangLifetime compiler = {};
        ASSERT_TRUE(compiler.Ready);
        std::vector<uint32_t> binaries[2] = {};
        ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Vertex, sizeof(ZEngine::Tests::GizmoShaders::Vertex) - 1, EShLangVertex, binaries[0]));
        ASSERT_TRUE(CompileGizmoStage(ZEngine::Tests::GizmoShaders::Fragment, sizeof(ZEngine::Tests::GizmoShaders::Fragment) - 1, EShLangFragment, binaries[1]));
        Renderers::GizmoPass pass                      = {};
        auto                 frame                     = std::make_unique<Renderers::GizmoFrameData>();
        uint32_t             validated                 = 0;
        uint32_t             synchronization_validated = 0;
        constexpr uint32_t   background                = 0xff804020u;
        constexpr uint32_t   colors[3]                 = {0xff0000ffu, 0xff00ff00u, 0xffff0000u};
        for (uint32_t cycle = 0; cycle < 3; ++cycle)
        {
            SCOPED_TRACE(cycle);
            OffscreenGizmoGraphCycle owner = {};
            ASSERT_TRUE(owner.Vulkan.Create(true, dynamic_rendering, true));
            if (dynamic_rendering && !owner.Vulkan.DynamicRenderingEnabled)
                GTEST_SKIP() << "The selected Vulkan device does not support dynamic rendering";
            ASSERT_NO_FATAL_FAILURE(owner.InitializeGraph(pass, binaries));
            validated                 += owner.Vulkan.ValidationEnabled ? 1u : 0u;
            synchronization_validated += owner.Vulkan.SynchronizationValidationEnabled ? 1u : 0u;
            frame->Reset();
            ASSERT_NO_FATAL_FAILURE(owner.RenderGraphFrame(*frame, 0, 96, 64, false));
            EXPECT_EQ(owner.Pixel(-.55f, -.5f), background);
            VkPipeline pipeline = VK_NULL_HANDLE;
            for (uint32_t step = 0; step < 12; ++step)
            {
                SCOPED_TRACE(step);
                frame->TriangleCount = frame->LineCount = frame->SpriteCount = 1;
                frame->Triangles[0]                                          = {
                    .A = { -.9f, -.8f, .5f},
                      .B = { -.2f, -.8f, .5f},
                      .C = {-.55f, .15f, .5f},
                      .Color = colors[step % 3]
                };
                frame->Lines[0] = {
                    .A = { .2f, -.5f, .5f},
                      .B = {.85f, -.5f, .5f},
                      .Width = 6.f, .Color = colors[(step + 1) % 3]
                };
                frame->Sprites[0] = {
                    .Position = {.5f, .5f, .5f},
                      .Size = 12.f, .Fill = colors[(step + 2) % 3]
                };
                ASSERT_NO_FATAL_FAILURE(owner.RenderGraphFrame(*frame, step % 3, step < 6 ? 96 : 128, step < 6 ? 64 : 96, step % 2 != 0));
                EXPECT_EQ(owner.Pixel(-.55f, -.5f), frame->Triangles[0].Color);
                EXPECT_EQ(owner.Pixel(.5f, -.5f), frame->Lines[0].Color);
                EXPECT_EQ(owner.Pixel(.5f, .5f), frame->Sprites[0].Fill);
                EXPECT_EQ(owner.Pixel(-.9f, .9f), background);
                const auto current = static_cast<Renderers::RenderPasses::GraphicPass*>(owner.Graph->GetPass("Gizmo Pass")->Handle)->Pipeline->Handle;
                if (pipeline)
                    EXPECT_EQ(current, pipeline);
                pipeline = current;
            }
            const auto warmed = owner.Arena.m_current_offset;
            for (uint32_t repeat = 0; repeat < 6; ++repeat)
            {
                ASSERT_NO_FATAL_FAILURE(owner.RenderGraphFrame(*frame, repeat % 3, 128, 96, repeat % 2 != 0));
                EXPECT_EQ(owner.Arena.m_current_offset, warmed);
                EXPECT_EQ(owner.Pixel(-.55f, -.5f), frame->Triangles[0].Color);
            }
            frame->Reset();
            ASSERT_NO_FATAL_FAILURE(owner.RenderGraphFrame(*frame, 2, 128, 96, true));
            EXPECT_EQ(owner.Pixel(-.55f, -.5f), background);
            EXPECT_EQ(owner.Pixel(.5f, -.5f), background);
            EXPECT_EQ(owner.Pixel(.5f, .5f), background);
        }
        ::testing::Test::RecordProperty("fresh_graphics_device_cycles", 3);
        ::testing::Test::RecordProperty("drawn_frames", 54);
        ::testing::Test::RecordProperty("worker_recorded_frames", 27);
        ::testing::Test::RecordProperty("primary_recorded_frames", 27);
        ::testing::Test::RecordProperty("disabled_frames", 6);
        ::testing::Test::RecordProperty("validated_graphics_device_cycles", validated);
        ::testing::Test::RecordProperty("synchronization_validated_graphics_device_cycles", synchronization_validated);
        ::testing::Test::RecordProperty("dynamic_rendering", dynamic_rendering ? 1 : 0);
    }
} // namespace

TEST_F(GpuAllocatorTest, GizmoGraphCompilesResizesAndExecutesPrimaryAndWorkerCommands)
{
    ASSERT_NE(s_allocator, nullptr);
    RunGizmoGraphCycles(false);
}

TEST_F(GpuAllocatorTest, DynamicGizmoGraphCompilesResizesAndExecutesPrimaryAndWorkerCommands)
{
    ASSERT_NE(s_allocator, nullptr);
    RunGizmoGraphCycles(true);
}
