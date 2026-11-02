#pragma once
#include <ZEngine/Rendering/Materials/MaterialAlpha.h>
#include "MaterialAlphaShaderSource.h"

// Pixel-level tests of the exact production alpha functions, with real depth
// testing and the scene's straight-alpha blend factors. Full lighting and
// bindless texture plumbing are exercised by the native editor visual checks.
namespace
{
    struct MaterialAlphaGpuFixture : ::testing::Test
    {
        struct Draw
        {
            float    Factor[4]  = {1, 0, 0, 1};
            float    Sampled[4] = {1, 1, 1, 1};
            uint32_t Mode       = 1;
            float    Cutoff     = .5f;
            uint32_t HasMap     = 0;
            float    Depth      = .5f;
        };
        static_assert(sizeof(Draw) == 48);
        HeadlessVulkan   Vulkan;
        GpuAllocator     Allocator;
        bool             AllocatorReady = false;
        VkImage          Images[2]      = {};
        VmaAllocation    Allocations[2] = {};
        VkImageView      Views[2]       = {};
        VkRenderPass     RenderPass     = {};
        VkFramebuffer    Framebuffer    = {};
        VkShaderModule   Modules[2]     = {};
        VkPipelineLayout Layout         = {};
        VkPipeline       Pipelines[2]   = {};
        VkCommandPool    Pool           = {};
        VkCommandBuffer  Commands       = {};
        VkQueue          Queue          = {};
        BufferView       Readback       = {};

        void             SetUp() override
        {
            if (!Vulkan.Create(true, false, true))
                GTEST_SKIP() << "No headless Vulkan device";
            Allocator.Initialize(Vulkan.PhysicalDevice, Vulkan.Device, Vulkan.Instance, false, false);
            AllocatorReady = true;
            Readback       = Allocator.AllocateBuffer(8 * 8 * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, GpuMemoryDomain::HostReadback, "alpha_pixels");
            ASSERT_TRUE(Readback);
            for (uint32_t i = 0; i < 2; ++i)
            {
                const VkImageCreateInfo image = {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = i ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM, .extent = {8, 8, 1},
                                   .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = static_cast<VkImageUsageFlags>(i ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
                };
                const VmaAllocationCreateInfo allocation = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
                ASSERT_EQ(vmaCreateImage(Allocator.Allocator, &image, &allocation, &Images[i], &Allocations[i], nullptr), VK_SUCCESS);
                const VkImageViewCreateInfo view = {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = Images[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = image.format, .subresourceRange = {.aspectMask = static_cast<VkImageAspectFlags>(i ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), .levelCount = 1, .layerCount = 1}
                };
                ASSERT_EQ(vkCreateImageView(Vulkan.Device, &view, nullptr, &Views[i]), VK_SUCCESS);
            }
            const VkAttachmentDescription attachments[] = {
                {.format = VK_FORMAT_R8G8B8A8_UNORM, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,     .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,             .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
                {    .format = VK_FORMAT_D32_SFLOAT, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}
            };
            const VkAttachmentReference color = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, depth = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
            const VkSubpassDescription  subpass        = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &color, .pDepthStencilAttachment = &depth};
            const VkSubpassDependency   dependencies[] = {
                {.srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0, .srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
                {.srcSubpass = 0, .dstSubpass = VK_SUBPASS_EXTERNAL, .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT, .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT}
            };
            const VkRenderPassCreateInfo render_pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 2, .pAttachments = attachments, .subpassCount = 1, .pSubpasses = &subpass, .dependencyCount = 2, .pDependencies = dependencies};
            ASSERT_EQ(vkCreateRenderPass(Vulkan.Device, &render_pass, nullptr, &RenderPass), VK_SUCCESS);
            const VkFramebufferCreateInfo framebuffer = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = RenderPass, .attachmentCount = 2, .pAttachments = Views, .width = 8, .height = 8, .layers = 1};
            ASSERT_EQ(vkCreateFramebuffer(Vulkan.Device, &framebuffer, nullptr, &Framebuffer), VK_SUCCESS);
            const std::string vertex   = R"(#version 460
layout(push_constant) uniform Parameters { vec4 Factor; vec4 Sampled; uint Mode; float Cutoff; uint HasMap; float Depth; } P;
void main() { vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2); gl_Position = vec4(p * 2.0 - 1.0, P.Depth, 1); })";
            const std::string fragment = std::string("#version 460\n") + ZEngine::Tests::MaterialAlphaSource + R"(
layout(push_constant) uniform Parameters { vec4 Factor; vec4 Sampled; uint Mode; float Cutoff; uint HasMap; float Depth; } P;
layout(location = 0) out vec4 Color;
void main() { vec4 base = resolveBaseColor(P.Factor, P.Sampled, P.HasMap != 0u, P.Mode); applyMaterialAlpha(base.a, P.Cutoff, P.Mode);
if (P.Mode == AlphaBlend && base.a <= 0.0) discard;
Color = vec4(base.rgb, P.Mode == AlphaBlend ? clamp(base.a, 0.0, 1.0) : 1.0); })";
            glslang::InitializeProcess();
            std::vector<uint32_t> binaries[2];
            ASSERT_TRUE(CompileGizmoStage(vertex.data(), static_cast<int>(vertex.size()), EShLangVertex, binaries[0]));
            ASSERT_TRUE(CompileGizmoStage(fragment.data(), static_cast<int>(fragment.size()), EShLangFragment, binaries[1]));
            glslang::FinalizeProcess();
            VkPipelineShaderStageCreateInfo stages[2] = {};
            for (uint32_t i = 0; i < 2; ++i)
            {
                const VkShaderModuleCreateInfo module = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = binaries[i].size() * sizeof(uint32_t), .pCode = binaries[i].data()};
                ASSERT_EQ(vkCreateShaderModule(Vulkan.Device, &module, nullptr, &Modules[i]), VK_SUCCESS);
                stages[i] = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT, .module = Modules[i], .pName = "main"};
            }
            const VkPushConstantRange        push   = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Draw)};
            const VkPipelineLayoutCreateInfo layout = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .pushConstantRangeCount = 1, .pPushConstantRanges = &push};
            ASSERT_EQ(vkCreatePipelineLayout(Vulkan.Device, &layout, nullptr, &Layout), VK_SUCCESS);
            const VkPipelineVertexInputStateCreateInfo   input    = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            const VkPipelineInputAssemblyStateCreateInfo assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
            const VkViewport                             viewport = {0, 0, 8, 8, 0, 1};
            const VkRect2D                               scissor  = {
                {0, 0},
                {8, 8}
            };
            const VkPipelineViewportStateCreateInfo      viewport_state = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor};
            const VkPipelineRasterizationStateCreateInfo raster         = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1};
            const VkPipelineMultisampleStateCreateInfo   multisample    = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
            for (uint32_t i = 0; i < 2; ++i)
            {
                const VkPipelineDepthStencilStateCreateInfo depth_state = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_TRUE, .depthWriteEnable = i ? VK_FALSE : VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};
                const VkPipelineColorBlendAttachmentState   blend       = {.blendEnable = i ? VK_TRUE : VK_FALSE, .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .alphaBlendOp = VK_BLEND_OP_ADD, .colorWriteMask = 15};
                const VkPipelineColorBlendStateCreateInfo   blending    = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blend};
                const VkGraphicsPipelineCreateInfo          pipeline    = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = stages, .pVertexInputState = &input, .pInputAssemblyState = &assembly, .pViewportState = &viewport_state, .pRasterizationState = &raster, .pMultisampleState = &multisample, .pDepthStencilState = &depth_state, .pColorBlendState = &blending, .layout = Layout, .renderPass = RenderPass};
                ASSERT_EQ(vkCreateGraphicsPipelines(Vulkan.Device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &Pipelines[i]), VK_SUCCESS);
            }
            const VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = Vulkan.QueueFamily};
            ASSERT_EQ(vkCreateCommandPool(Vulkan.Device, &pool, nullptr, &Pool), VK_SUCCESS);
            const VkCommandBufferAllocateInfo commands = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = Pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
            ASSERT_EQ(vkAllocateCommandBuffers(Vulkan.Device, &commands, &Commands), VK_SUCCESS);
            vkGetDeviceQueue(Vulkan.Device, Vulkan.QueueFamily, 0, &Queue);
        }

        std::array<uint8_t, 4> Render(std::initializer_list<Draw> draws, bool preview = false)
        {
            EXPECT_EQ(vkResetCommandBuffer(Commands, 0), VK_SUCCESS);
            const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            EXPECT_EQ(vkBeginCommandBuffer(Commands, &begin), VK_SUCCESS);
            const VkClearValue          clears[] = {{.color = {.float32 = {0, 0, preview ? 0.f : 1.f, preview ? 0.f : 1.f}}}, {.depthStencil = {1, 0}}};
            const VkRenderPassBeginInfo pass     = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = RenderPass, .framebuffer = Framebuffer, .renderArea = {{0, 0}, {8, 8}},
                           .clearValueCount = 2, .pClearValues = clears
            };
            vkCmdBeginRenderPass(Commands, &pass, VK_SUBPASS_CONTENTS_INLINE);
            for (const auto& draw : draws)
            {
                vkCmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipelines[!preview && draw.Mode == 3 ? 1 : 0]);
                vkCmdPushConstants(Commands, Layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(draw), &draw);
                vkCmdDraw(Commands, 3, 1, 0, 0);
            }
            vkCmdEndRenderPass(Commands);
            const VkBufferImageCopy copy = {
                .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
                  .imageExtent = {8, 8, 1}
            };
            vkCmdCopyImageToBuffer(Commands, Images[0], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Readback.Handle, 1, &copy);
            const VkMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
            vkCmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0, nullptr);
            EXPECT_EQ(vkEndCommandBuffer(Commands), VK_SUCCESS);
            const VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &Commands};
            EXPECT_EQ(vkQueueSubmit(Queue, 1, &submit, VK_NULL_HANDLE), VK_SUCCESS);
            EXPECT_EQ(vkQueueWaitIdle(Queue), VK_SUCCESS);
            EXPECT_EQ(vmaInvalidateAllocation(Allocator.Allocator, Readback.Allocation, 0, VK_WHOLE_SIZE), VK_SUCCESS);
            const auto* pixels = static_cast<const uint8_t*>(Readback.MappedData) + (4 * 8 + 4) * 4;
            return {pixels[0], pixels[1], pixels[2], pixels[3]};
        }

        void TearDown() override
        {
            if (Vulkan.Device)
                vkDeviceWaitIdle(Vulkan.Device);
            if (Pool)
                vkDestroyCommandPool(Vulkan.Device, Pool, nullptr);
            for (auto pipeline : Pipelines)
                if (pipeline)
                    vkDestroyPipeline(Vulkan.Device, pipeline, nullptr);
            if (Layout)
                vkDestroyPipelineLayout(Vulkan.Device, Layout, nullptr);
            for (auto module : Modules)
                if (module)
                    vkDestroyShaderModule(Vulkan.Device, module, nullptr);
            if (Framebuffer)
                vkDestroyFramebuffer(Vulkan.Device, Framebuffer, nullptr);
            if (RenderPass)
                vkDestroyRenderPass(Vulkan.Device, RenderPass, nullptr);
            for (uint32_t i = 0; i < 2; ++i)
            {
                if (Views[i])
                    vkDestroyImageView(Vulkan.Device, Views[i], nullptr);
                if (Images[i])
                    vmaDestroyImage(Allocator.Allocator, Images[i], Allocations[i]);
            }
            if (Readback)
                Allocator.FreeBuffer(Readback);
            if (AllocatorReady)
                Allocator.Shutdown();
            RecordProperty("validation_enabled", Vulkan.ValidationEnabled ? 1 : 0);
            EXPECT_EQ(Vulkan.ValidationErrors.load(), 0u);
            Vulkan.Destroy();
        }
    };

    TEST_F(MaterialAlphaGpuFixture, OpaqueZeroAlphaAndMaskThresholdIncludingZero)
    {
        EXPECT_EQ(
            Render({
            {.Factor = {1, 0, 0, 0}, .Mode = 1}
        }),
            (std::array<uint8_t, 4>{255, 0, 0, 255}));
        EXPECT_EQ(
            Render({
            {.Factor = {1, 0, 0, .49f}, .Mode = 2}
        }),
            (std::array<uint8_t, 4>{0, 0, 255, 255}));
        EXPECT_EQ(
            Render({
            {.Factor = {1, 0, 0, .5f}, .Mode = 2}
        }),
            (std::array<uint8_t, 4>{255, 0, 0, 255}));
        EXPECT_EQ(
            Render({
            {.Factor = {1, 0, 0, 0}, .Mode = 2, .Cutoff = 0}
        }),
            (std::array<uint8_t, 4>{255, 0, 0, 255}));
        // A clipped foreground must not poison depth for the surface behind it.
        EXPECT_EQ(
            Render({
            {.Factor = {1, 0, 0, 0}, .Mode = 2, .Depth = .1f},
            {.Factor = {0, 1, 0, 1}, .Depth = .8f}
        }),
            (std::array<uint8_t, 4>{0, 255, 0, 255}));
    }

    TEST_F(MaterialAlphaGpuFixture, PartialBlendCoverageAndStraightAlphaPreview)
    {
        for (float opacity : {0.f, .25f, .5f, 1.f})
        {
            const auto pixel = Render({
                {.Factor = {1, 0, 0, opacity}, .Mode = 3}
            });
            EXPECT_NEAR(pixel[0], opacity * 255, 1);
            EXPECT_NEAR(pixel[2], (1 - opacity) * 255, 1);
            const auto preview = Render(
                {
                {.Factor = {1, 0, 0, opacity}, .Mode = 3}
            },
                true);
            EXPECT_EQ(preview[0], opacity > 0 ? 255 : 0);
            EXPECT_NEAR(preview[3], opacity * 255, 1);
        }
    }

    TEST_F(MaterialAlphaGpuFixture, SampledBaseAlphaMultipliesFactorButLegacyRetainsReplacement)
    {
        const auto pixel = Render({
            {.Factor = {1, 1, 1, .5f}, .Sampled = {1, 0, 0, .5f}, .Mode = 3, .HasMap = 1}
        });
        EXPECT_NEAR(pixel[0], 64, 1);
        EXPECT_NEAR(pixel[2], 191, 1);
        EXPECT_EQ(
            Render({
            {.Factor = {0, 1, 0, 0}, .Sampled = {1, 0, 0, 1}, .Mode = 0, .HasMap = 1}
        }),
            (std::array<uint8_t, 4>{255, 0, 0, 255}));
    }

    TEST_F(MaterialAlphaGpuFixture, BlackFactorTimesWhitePartialAlphaRemainsBlackAndSmoothlyBlended)
    {
        for (float opacity : {33.f / 255.f, .25f, .5f, 1.f})
        {
            const Draw eye = {
                .Factor = {0, 0, 0,       1},
                  .Sampled = {1, 1, 1, opacity},
                  .Mode = 3, .HasMap = 1
            };
            const auto scene = Render({eye});
            EXPECT_EQ(scene[0], 0);
            EXPECT_EQ(scene[1], 0);
            EXPECT_NEAR(scene[2], (1 - opacity) * 255, 1);
            const auto preview = Render({eye}, true);
            EXPECT_EQ(preview[0], 0);
            EXPECT_EQ(preview[1], 0);
            EXPECT_EQ(preview[2], 0);
            EXPECT_NEAR(preview[3], opacity * 255, 1);
        }
    }

    TEST_F(MaterialAlphaGpuFixture, TransparentLayersRespectOpaqueDepthAndDoNotWriteDepth)
    {
        EXPECT_EQ(
            Render({
            {  .Factor = {0, 1, 0, 0}, .Mode = 1, .Depth = .2f},
            {.Factor = {1, 0, 0, .5f}, .Mode = 3, .Depth = .8f}
        }),
            (std::array<uint8_t, 4>{0, 255, 0, 255}));
        const auto far_to_near = Render({
            {.Factor = {1, 0, 0, .5f}, .Mode = 3, .Depth = .8f},
            {.Factor = {0, 1, 0, .5f}, .Mode = 3, .Depth = .2f}
        });
        EXPECT_NEAR(far_to_near[0], 64, 1);
        EXPECT_NEAR(far_to_near[1], 128, 1);
        EXPECT_NEAR(far_to_near[2], 64, 1);
        const auto near_to_far = Render({
            {.Factor = {0, 1, 0, .5f}, .Mode = 3, .Depth = .2f},
            {.Factor = {1, 0, 0, .5f}, .Mode = 3, .Depth = .8f}
        });
        EXPECT_NEAR(near_to_far[0], 128, 1); // farther draw still passes depth
        EXPECT_NEAR(near_to_far[1], 64, 1);
    }
} // namespace
