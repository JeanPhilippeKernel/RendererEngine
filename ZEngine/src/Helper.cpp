#include <pch.h>
#include <ZEngineDef.h>
#include <Helpers/BufferHelper.h>
#include <Helpers/RendererHelper.h>
#include <Helpers/MeshHelper.h>
#include <Rendering/Renderers/Storages/IVertex.h>
#include <Rendering/Shaders/Compilers/ShaderCompiler.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

#include <Rendering/Buffers/FrameBuffers/FrameBufferSpecification.h>

using namespace ZEngine::Rendering::Renderers;

namespace ZEngine::Helpers
{
    void CreateBuffer(
        Hardwares::VulkanDevice&                device,
        VkBuffer&                               buffer,
        VkDeviceMemory&                         device_memory,
        VkDeviceSize                            byte_size,
        VkBufferUsageFlags                      buffer_usage,
        const VkPhysicalDeviceMemoryProperties& memory_properties,
        VkMemoryPropertyFlags                   requested_properties)
    {
        VkBufferCreateInfo buffer_create_info = {};
        buffer_create_info.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_create_info.size               = byte_size;
        buffer_create_info.usage              = buffer_usage;
        buffer_create_info.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

        ZENGINE_VALIDATE_ASSERT(vkCreateBuffer(device.GetNativeDeviceHandle(), &buffer_create_info, nullptr, &buffer) == VK_SUCCESS, "Failed to create vertex buffer")

        VkMemoryRequirements memory_requirement;
        vkGetBufferMemoryRequirements(device.GetNativeDeviceHandle(), buffer, &memory_requirement);

        bool     memory_type_index_found{false};
        uint32_t memory_type_index{0};
        for (; memory_type_index < memory_properties.memoryTypeCount; ++memory_type_index)
        {
            if ((memory_requirement.memoryTypeBits & (1 << memory_type_index)) &&
                (memory_properties.memoryTypes[memory_type_index].propertyFlags & requested_properties) == requested_properties)
            {
                memory_type_index_found = true;
                break;
            }
        }

        if (!memory_type_index_found)
        {
            ZENGINE_CORE_ERROR("Failed to find a valid memory type")
        }

        VkMemoryAllocateInfo memory_allocate_info = {};
        memory_allocate_info.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memory_allocate_info.allocationSize       = memory_requirement.size;
        memory_allocate_info.memoryTypeIndex      = memory_type_index;

        // ToDo : Call of vkAllocateMemory(...) isn't optimal because it is limited by maxMemoryAllocationCount
        // It should be replaced by VulkanMemoryAllocator SDK (see :  https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
        ZENGINE_VALIDATE_ASSERT(
            vkAllocateMemory(device.GetNativeDeviceHandle(), &memory_allocate_info, nullptr, &device_memory) == VK_SUCCESS, "Failed to allocate memory for vertex buffer")
        ZENGINE_VALIDATE_ASSERT(vkBindBufferMemory(device.GetNativeDeviceHandle(), buffer, device_memory, 0) == VK_SUCCESS, "Failed to bind the memory to the vertex buffer")
    }

    void CopyBuffer(Hardwares::VulkanDevice& device, VkBuffer& source_buffer, VkBuffer& destination_buffer, VkDeviceSize byte_size)
    {
        auto            transfer_queue_view = device.GetCurrentTransferQueue();
        auto            command_pool        = device.GetTransferCommandPool(transfer_queue_view.FamilyIndex);
        VkCommandBuffer command_buffer      = BeginOneTimeCommandBuffer(device, command_pool);

        VkBufferCopy buffer_copy = {};
        buffer_copy.srcOffset    = 0;
        buffer_copy.dstOffset    = 0;
        buffer_copy.size         = byte_size;

        vkCmdCopyBuffer(command_buffer, source_buffer, destination_buffer, 1, &buffer_copy);

        EndOneTimeCommandBuffer(command_buffer, device, transfer_queue_view.Queue, command_pool);
    }

    VkCommandBuffer BeginOneTimeCommandBuffer(Hardwares::VulkanDevice& device, VkCommandPool pool)
    {
        VkCommandBufferAllocateInfo command_buffer_allocate_info = {};
        command_buffer_allocate_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_buffer_allocate_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_buffer_allocate_info.commandBufferCount          = 1;
        command_buffer_allocate_info.commandPool                 = pool;

        VkCommandBuffer command_buffer;
        ZENGINE_VALIDATE_ASSERT(vkAllocateCommandBuffers(device.GetNativeDeviceHandle(), &command_buffer_allocate_info, &command_buffer) == VK_SUCCESS, "")

        VkCommandBufferBeginInfo command_buffer_begin_info = {};
        command_buffer_begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_buffer_begin_info.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        ZENGINE_VALIDATE_ASSERT(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) == VK_SUCCESS, "Failed to begin the Command Buffer")
        return command_buffer;
    }

    void EndOneTimeCommandBuffer(VkCommandBuffer& command_buffer, Hardwares::VulkanDevice& device, VkQueue queue, VkCommandPool pool)
    {
        ZENGINE_VALIDATE_ASSERT(vkEndCommandBuffer(command_buffer) == VK_SUCCESS, "Failed to end the Command Buffer")

        VkSubmitInfo submit_info       = {};
        submit_info.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers    = &command_buffer;

        vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);

        vkFreeCommandBuffers(device.GetNativeDeviceHandle(), pool, 1, &command_buffer);
    }

    void CopyBufferToImage(Hardwares::VulkanDevice& device, VkBuffer source_buffer, VkImage destination_image, uint32_t width, uint32_t height)
    {
        auto            transfer_queue_view = device.GetCurrentTransferQueue();
        auto            command_pool        = device.GetTransferCommandPool(transfer_queue_view.FamilyIndex);
        VkCommandBuffer command_buffer      = BeginOneTimeCommandBuffer(device, command_pool);

        VkBufferImageCopy buffer_image_copy = {};
        buffer_image_copy.bufferOffset      = 0;
        buffer_image_copy.bufferRowLength   = 0;
        buffer_image_copy.bufferImageHeight = 0;

        buffer_image_copy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        buffer_image_copy.imageSubresource.mipLevel       = 0;
        buffer_image_copy.imageSubresource.baseArrayLayer = 0;
        buffer_image_copy.imageSubresource.layerCount     = 1;

        buffer_image_copy.imageOffset = {0, 0, 0};
        buffer_image_copy.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(command_buffer, source_buffer, destination_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffer_image_copy);

        EndOneTimeCommandBuffer(command_buffer, device, transfer_queue_view.Queue, command_pool);
    }

    VkRenderPass CreateRenderPass(Hardwares::VulkanDevice& device, RenderPasses::RenderPassSpecification& specification)
    {
        VkRenderPass out_render_pass{VK_NULL_HANDLE};

        /*Ensure Subpass is updated with its associated color attachment reference*/
        for (RenderPasses::SubPassSpecification& subpass_specification : specification.SubpassSpecifications)
        {
            subpass_specification.SubpassDescription.colorAttachmentCount = subpass_specification.ColorAttachementReferences.size();
            subpass_specification.SubpassDescription.pColorAttachments    = subpass_specification.ColorAttachementReferences.data();
            if (subpass_specification.DepthStencilAttachementReference.layout != VK_IMAGE_LAYOUT_UNDEFINED)
            {
                subpass_specification.SubpassDescription.pDepthStencilAttachment = &(subpass_specification.DepthStencilAttachementReference);
            }
        }

        VkRenderPassCreateInfo render_pass_create_info = {};
        render_pass_create_info.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_create_info.attachmentCount        = (uint32_t) specification.ColorAttachements.size();
        render_pass_create_info.pAttachments           = specification.ColorAttachements.data();

        std::vector<VkSubpassDescription> sub_pass_descriptions;
        std::transform(
            std::begin(specification.SubpassSpecifications),
            std::end(specification.SubpassSpecifications),
            std::back_inserter(sub_pass_descriptions),
            [](const auto& sub_pass_specification) {
                return sub_pass_specification.SubpassDescription;
            });

        render_pass_create_info.subpassCount    = (uint32_t) sub_pass_descriptions.size();
        render_pass_create_info.pSubpasses      = sub_pass_descriptions.data();
        render_pass_create_info.dependencyCount = (uint32_t) specification.SubpassDependencies.size();
        render_pass_create_info.pDependencies   = specification.SubpassDependencies.data();

        ZENGINE_VALIDATE_ASSERT(
            vkCreateRenderPass(device.GetNativeDeviceHandle(), &render_pass_create_info, nullptr, &out_render_pass) == VK_SUCCESS, "Failed to create render pass")

        return out_render_pass;
    }

    VkPipelineLayout CreatePipelineLayout(Hardwares::VulkanDevice& device, const VkPipelineLayoutCreateInfo& pipeline_layout_create_info)
    {
        VkPipelineLayout out_pipeline_layout{VK_NULL_HANDLE};
        ZENGINE_VALIDATE_ASSERT(
            vkCreatePipelineLayout(device.GetNativeDeviceHandle(), &pipeline_layout_create_info, nullptr, &out_pipeline_layout) == VK_SUCCESS, "Failed to create pipeline layout")

        return out_pipeline_layout;
    }

    VkPipeline CreateGraphicPipeline(
        Hardwares::VulkanDevice&                                    device,
        VkPipelineLayout&                                           pipeline_layout,
        VkRenderPass&                                               render_pass,
        const Pipelines::GraphicRendererPipelineStateSpecification& specification)
    {
        VkPipeline out_graphic_pipeline{VK_NULL_HANDLE};

        VkGraphicsPipelineCreateInfo graphic_pipeline_create_info = {};
        graphic_pipeline_create_info.sType                        = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

        std::vector<VkPipelineShaderStageCreateInfo> shader_create_info_collection;
        std::vector<VkShaderModule>                  shader_module_collection;

        for (const auto& shader_information : specification.ShaderInfo)
        {
            shader_create_info_collection.push_back(shader_information.ShaderStageCreateInfo);
            shader_module_collection.push_back(shader_information.ShaderModule);
        }

        graphic_pipeline_create_info.stageCount = shader_create_info_collection.size();
        graphic_pipeline_create_info.pStages    = shader_create_info_collection.data();

        graphic_pipeline_create_info.pVertexInputState   = &(specification.VertexInputStateCreateInfo);
        graphic_pipeline_create_info.pInputAssemblyState = &(specification.InputAssemblyStateCreateInfo);
        graphic_pipeline_create_info.pViewportState      = &(specification.ViewportStateCreateInfo);
        graphic_pipeline_create_info.pRasterizationState = &(specification.RasterizationStateCreateInfo);
        graphic_pipeline_create_info.pMultisampleState   = &(specification.MultisampleStateCreateInfo);
        graphic_pipeline_create_info.pDepthStencilState  = &(specification.DepthStencilStateCreateInfo);
        graphic_pipeline_create_info.pColorBlendState    = &(specification.ColorBlendStateCreateInfo);
        graphic_pipeline_create_info.pDynamicState       = &(specification.DynamicStateCreateInfo);
        graphic_pipeline_create_info.layout              = pipeline_layout;
        graphic_pipeline_create_info.renderPass          = render_pass;
        graphic_pipeline_create_info.subpass             = 0;
        graphic_pipeline_create_info.basePipelineHandle  = VK_NULL_HANDLE; // Optional
        graphic_pipeline_create_info.basePipelineIndex   = -1;             // Optional
        graphic_pipeline_create_info.flags               = 0;              // Optional
        graphic_pipeline_create_info.pNext               = nullptr;        // Optional

        ZENGINE_VALIDATE_ASSERT(
            vkCreateGraphicsPipelines(device.GetNativeDeviceHandle(), VK_NULL_HANDLE, 1, &graphic_pipeline_create_info, nullptr, &out_graphic_pipeline) == VK_SUCCESS,
            "Failed to create Graphics Pipeline")

        // Cleanup ShaderModules
        for (auto& shader_module : shader_module_collection)
        {
            vkDestroyShaderModule(device.GetNativeDeviceHandle(), shader_module, nullptr);
        }

        return out_graphic_pipeline;
    }

    VkFramebuffer CreateFramebuffer(
        Hardwares::VulkanDevice& device,
        std::vector<VkImageView> image_view_attachments,
        const VkRenderPass&      render_pass,
        const VkExtent2D&        swapchain_extent,
        uint32_t                 layer_number)
    {
        VkFramebuffer m_framebuffer{VK_NULL_HANDLE};

        VkFramebufferCreateInfo framebuffer_create_info = {};
        framebuffer_create_info.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_create_info.renderPass              = render_pass;
        framebuffer_create_info.attachmentCount         = image_view_attachments.size();
        framebuffer_create_info.pAttachments            = image_view_attachments.data();
        framebuffer_create_info.width                   = swapchain_extent.width;
        framebuffer_create_info.height                  = swapchain_extent.height;
        framebuffer_create_info.layers                  = layer_number;

        ZENGINE_VALIDATE_ASSERT(
            vkCreateFramebuffer(device.GetNativeDeviceHandle(), &framebuffer_create_info, nullptr, &m_framebuffer) == VK_SUCCESS, "Failed to create Framebuffer")

        return m_framebuffer;
    }

    void FillDefaultPipelineFixedStates(Rendering::Renderers::Pipelines::GraphicRendererPipelineStateSpecification& specification, const VkExtent2D& swapchain_extent)
    {
        /*Dynamic State*/
        specification.DynamicStates                            = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        specification.DynamicStateCreateInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        specification.DynamicStateCreateInfo.dynamicStateCount = specification.DynamicStates.size();
        specification.DynamicStateCreateInfo.pDynamicStates    = specification.DynamicStates.data();
        specification.DynamicStateCreateInfo.flags             = 0;
        specification.DynamicStateCreateInfo.pNext             = nullptr;

        /*Input Assembly*/
        specification.InputAssemblyStateCreateInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        specification.InputAssemblyStateCreateInfo.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        specification.InputAssemblyStateCreateInfo.primitiveRestartEnable = VK_FALSE;
        specification.InputAssemblyStateCreateInfo.flags                  = 0;
        specification.InputAssemblyStateCreateInfo.pNext                  = nullptr;

        /*Viewports and scissors*/
        specification.Viewport.x = 0.0f;
        specification.Viewport.y = 0.0f;

        specification.Viewport.width    = swapchain_extent.width;
        specification.Viewport.height   = swapchain_extent.height;
        specification.Viewport.minDepth = 0.0f;
        specification.Viewport.maxDepth = 1.0f;

        specification.Scissor.offset = {0, 0};
        specification.Scissor.extent = swapchain_extent;

        specification.ViewportStateCreateInfo.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        specification.ViewportStateCreateInfo.viewportCount = 1;
        specification.ViewportStateCreateInfo.scissorCount  = 1;
        specification.ViewportStateCreateInfo.pViewports    = nullptr;
        specification.ViewportStateCreateInfo.pScissors     = nullptr;
        specification.ViewportStateCreateInfo.pNext         = nullptr;
        specification.ViewportStateCreateInfo.flags         = 0;
        // information.ViewportStateCreateInfo.pViewports    = &(information.Viewport);
        // information.ViewportStateCreateInfo.pScissors     = &(information.Scissor);

        /*Rasterizer*/
        specification.RasterizationStateCreateInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        specification.RasterizationStateCreateInfo.depthClampEnable        = VK_FALSE;
        specification.RasterizationStateCreateInfo.rasterizerDiscardEnable = VK_FALSE;
        specification.RasterizationStateCreateInfo.polygonMode             = VK_POLYGON_MODE_FILL;
        specification.RasterizationStateCreateInfo.lineWidth               = 1.0f;
        specification.RasterizationStateCreateInfo.cullMode                = VK_CULL_MODE_BACK_BIT;
        specification.RasterizationStateCreateInfo.frontFace               = VK_FRONT_FACE_CLOCKWISE;
        specification.RasterizationStateCreateInfo.depthBiasEnable         = VK_FALSE;
        specification.RasterizationStateCreateInfo.depthBiasConstantFactor = 0.0f; // Optional
        specification.RasterizationStateCreateInfo.depthBiasClamp          = 0.0f; // Optional
        specification.RasterizationStateCreateInfo.depthBiasSlopeFactor    = 0.0f; // Optional
        specification.RasterizationStateCreateInfo.pNext                   = nullptr;

        /*Multisampling*/
        specification.MultisampleStateCreateInfo.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        specification.MultisampleStateCreateInfo.sampleShadingEnable   = VK_FALSE;
        specification.MultisampleStateCreateInfo.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
        specification.MultisampleStateCreateInfo.minSampleShading      = 1.0f;     // Optional
        specification.MultisampleStateCreateInfo.pSampleMask           = nullptr;  // Optional
        specification.MultisampleStateCreateInfo.alphaToCoverageEnable = VK_FALSE; // Optional
        specification.MultisampleStateCreateInfo.alphaToOneEnable      = VK_FALSE; // Optional
        specification.MultisampleStateCreateInfo.flags                 = 0;
        specification.MultisampleStateCreateInfo.pNext                 = nullptr;

        /*Depth and Stencil testing*/
        specification.DepthStencilStateCreateInfo.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        specification.DepthStencilStateCreateInfo.depthTestEnable       = VK_TRUE;
        specification.DepthStencilStateCreateInfo.depthWriteEnable      = VK_TRUE;
        specification.DepthStencilStateCreateInfo.depthCompareOp        = VK_COMPARE_OP_LESS;
        specification.DepthStencilStateCreateInfo.depthBoundsTestEnable = VK_FALSE;
        specification.DepthStencilStateCreateInfo.minDepthBounds        = 0.0f; // Optional
        specification.DepthStencilStateCreateInfo.maxDepthBounds        = 1.0f; // Optional
        specification.DepthStencilStateCreateInfo.stencilTestEnable     = VK_FALSE;
        specification.DepthStencilStateCreateInfo.front                 = {}; // Optional
        specification.DepthStencilStateCreateInfo.back                  = {}; // Optiona

        /*Color blending*/
        // This configuration is per-framebuffer definition
        specification.ColorBlendAttachmentState.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        specification.ColorBlendAttachmentState.blendEnable         = VK_TRUE;
        specification.ColorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        specification.ColorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        specification.ColorBlendAttachmentState.colorBlendOp        = VK_BLEND_OP_ADD;
        specification.ColorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        specification.ColorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        specification.ColorBlendAttachmentState.alphaBlendOp        = VK_BLEND_OP_ADD;

        // this configuration is for global color blending settings
        specification.ColorBlendStateCreateInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        specification.ColorBlendStateCreateInfo.logicOpEnable     = VK_FALSE;
        specification.ColorBlendStateCreateInfo.logicOp           = VK_LOGIC_OP_COPY; // Optional
        specification.ColorBlendStateCreateInfo.attachmentCount   = 1;
        specification.ColorBlendStateCreateInfo.pAttachments      = &specification.ColorBlendAttachmentState;
        specification.ColorBlendStateCreateInfo.blendConstants[0] = 0.0f; // Optional
        specification.ColorBlendStateCreateInfo.blendConstants[1] = 0.0f; // Optional
        specification.ColorBlendStateCreateInfo.blendConstants[2] = 0.0f; // Optional
        specification.ColorBlendStateCreateInfo.blendConstants[3] = 0.0f; // Optional
        specification.ColorBlendStateCreateInfo.flags             = 0;
        specification.ColorBlendStateCreateInfo.pNext             = nullptr;

        /*Pipeline layout*/
        // Uniform Shader configuration
        specification.LayoutCreateInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        specification.LayoutCreateInfo.setLayoutCount         = 0;       // Optional
        specification.LayoutCreateInfo.pSetLayouts            = nullptr; // Optional
        specification.LayoutCreateInfo.pushConstantRangeCount = 0;       // Optional
        specification.LayoutCreateInfo.pPushConstantRanges    = nullptr; // Optional
        specification.LayoutCreateInfo.flags                  = VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT;
        specification.LayoutCreateInfo.pNext                  = nullptr;

        /*Vertex Input*/
        specification.VertexInputStateCreateInfo.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        specification.VertexInputStateCreateInfo.vertexAttributeDescriptionCount = 0;
        specification.VertexInputStateCreateInfo.pVertexAttributeDescriptions    = nullptr;
        specification.VertexInputStateCreateInfo.vertexBindingDescriptionCount   = 0;
        specification.VertexInputStateCreateInfo.pVertexBindingDescriptions      = nullptr;
        specification.VertexInputStateCreateInfo.flags                           = 0;
        specification.VertexInputStateCreateInfo.pNext                           = nullptr;
    }

    void TransitionImageLayout(Hardwares::VulkanDevice& device, VkImage image, VkFormat image_format, VkImageLayout old_image_layout, VkImageLayout new_image_layout)
    {
        auto                 graphic_queue  = device.GetCurrentGraphicQueueWithPresentSupport();
        auto                 command_pool   = device.GetGraphicCommandPool(graphic_queue.FamilyIndex);
        VkCommandBuffer      command_buffer = BeginOneTimeCommandBuffer(device, command_pool);
        VkImageMemoryBarrier memory_barrier = {};
        memory_barrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        memory_barrier.oldLayout            = old_image_layout;
        memory_barrier.newLayout            = new_image_layout;
        memory_barrier.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
        memory_barrier.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;

        memory_barrier.image                           = image;
        memory_barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        memory_barrier.subresourceRange.baseMipLevel   = 0;
        memory_barrier.subresourceRange.baseArrayLayer = 0;
        memory_barrier.subresourceRange.layerCount     = 1;
        memory_barrier.subresourceRange.levelCount     = 1;

        VkPipelineStageFlagBits source_stage_mask      = VK_PIPELINE_STAGE_NONE;
        VkPipelineStageFlagBits destination_stage_mask = VK_PIPELINE_STAGE_NONE;

        if (old_image_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_image_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            memory_barrier.srcAccessMask = 0;
            memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            source_stage_mask      = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destination_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (old_image_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_image_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            source_stage_mask      = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destination_stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else
        {
            // ToDo : Should be refactored for better exception handling
            throw std::invalid_argument("unsupported layout transition!");
        }

        vkCmdPipelineBarrier(command_buffer, source_stage_mask, destination_stage_mask, 0, 0, nullptr, 0, nullptr, 1, &memory_barrier);

        EndOneTimeCommandBuffer(command_buffer, device, graphic_queue.Queue, command_pool);
    }

    VkFormat FindSupportedFormat(Hardwares::VulkanDevice& device, const std::vector<VkFormat>& format_collection, VkImageTiling image_tiling, VkFormatFeatureFlags feature_flags)
    {
        VkFormat supported_format = VK_FORMAT_UNDEFINED;

        for (uint32_t i = 0; i < format_collection.size(); ++i)
        {
            VkFormatProperties format_properties;
            vkGetPhysicalDeviceFormatProperties(device.GetNativePhysicalDeviceHandle(), format_collection[i], &format_properties);

            if (image_tiling == VK_IMAGE_TILING_LINEAR && (format_properties.linearTilingFeatures & feature_flags) == feature_flags)
            {
                supported_format = format_collection[i];
            }
            else if (image_tiling == VK_IMAGE_TILING_OPTIMAL && (format_properties.optimalTilingFeatures & feature_flags) == feature_flags)
            {
                supported_format = format_collection[i];
            }
        }

        ZENGINE_VALIDATE_ASSERT(supported_format != VK_FORMAT_UNDEFINED, "Failed to find supported Image format")

        return supported_format;
    }

    VkFormat FindDepthFormat(Hardwares::VulkanDevice& device)
    {
        return FindSupportedFormat(
            device, {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    }

    Pipelines::StandardGraphicPipeline CreateStandardGraphicPipeline(Hardwares::VulkanDevice& device, const VkExtent2D& extent, VkRenderPass render_pass, uint32_t frame_count)
    {
        std::string_view                                         shader_file       = "Resources/Windows/Shaders/standard_shader_light.glsl";
        Rendering::Renderers::Pipelines::StandardGraphicPipeline standard_pipeline = {};

        standard_pipeline.FrameCount = frame_count;
        standard_pipeline.UBOCameraPropertiesCollection.resize(frame_count);
        standard_pipeline.UBOModelPropertiesCollection.resize(frame_count);

        Rendering::Renderers::Pipelines::GraphicRendererPipelineSpecification pipeline_specification = {};

        /*Pipeline fixed states*/
        Helpers::FillDefaultPipelineFixedStates(pipeline_specification.StateSpecification, extent);

        auto vertex_attribute_description_collection                                                         = Storages::IVertex::GetVertexAttributeDescription();
        pipeline_specification.StateSpecification.VertexInputStateCreateInfo.vertexAttributeDescriptionCount = vertex_attribute_description_collection.size();
        pipeline_specification.StateSpecification.VertexInputStateCreateInfo.pVertexAttributeDescriptions    = vertex_attribute_description_collection.data();

        auto vertex_input_binding_description_collection                                                   = Storages::IVertex::GetVertexInputBindingDescription();
        pipeline_specification.StateSpecification.VertexInputStateCreateInfo.vertexBindingDescriptionCount = vertex_input_binding_description_collection.size();
        pipeline_specification.StateSpecification.VertexInputStateCreateInfo.pVertexBindingDescriptions    = vertex_input_binding_description_collection.data();

        /*Shader definition*/
        // Todo : we should rethink the whole shader compiler...
        Rendering::Shaders::Compilers::ShaderCompiler shader_compiler = {};
        shader_compiler.SetSource(shader_file);
        auto shader_compile_result = shader_compiler.CompileAsync2().get();

        ZENGINE_VALIDATE_ASSERT(std::get<0>(shader_compile_result) == Rendering::Shaders::ShaderOperationResult::SUCCESS, "Failed to compile shader")

        auto& shader_information                             = std::get<1>(shader_compile_result);
        pipeline_specification.StateSpecification.ShaderInfo = std::move(shader_information);

        /* VkDescriptorSetLayoutBinding definition */
        VkDescriptorSetLayoutBinding ubo_camera_layout_binding = {};
        ubo_camera_layout_binding.binding                      = 0;
        ubo_camera_layout_binding.descriptorCount              = 1;
        ubo_camera_layout_binding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubo_camera_layout_binding.stageFlags                   = VK_SHADER_STAGE_VERTEX_BIT;
        ubo_camera_layout_binding.pImmutableSamplers           = nullptr;

        VkDescriptorSetLayoutBinding ubo_model_layout_binding = {};
        ubo_model_layout_binding.binding                      = 1;
        ubo_model_layout_binding.descriptorCount              = 1;
        ubo_model_layout_binding.descriptorType               = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubo_model_layout_binding.stageFlags                   = VK_SHADER_STAGE_VERTEX_BIT;
        ubo_model_layout_binding.pImmutableSamplers           = nullptr;

        VkDescriptorSetLayoutBinding fragment_sampler_binding = {};
        fragment_sampler_binding.binding                      = 0;
        fragment_sampler_binding.descriptorCount              = 1;
        fragment_sampler_binding.descriptorType               = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        fragment_sampler_binding.stageFlags                   = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragment_sampler_binding.pImmutableSamplers           = nullptr;

        standard_pipeline.DescriptorSetLayoutBindingCollection         = {ubo_camera_layout_binding, ubo_model_layout_binding};
        standard_pipeline.FragmentDescriptorSetLayoutBindingCollection = {fragment_sampler_binding};

        VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {};
        descriptor_set_layout_create_info.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptor_set_layout_create_info.bindingCount                    = standard_pipeline.DescriptorSetLayoutBindingCollection.size();
        descriptor_set_layout_create_info.pBindings                       = standard_pipeline.DescriptorSetLayoutBindingCollection.data();

        VkDescriptorSetLayoutCreateInfo fragment_descriptor_set_layout_create_info = {};
        fragment_descriptor_set_layout_create_info.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        fragment_descriptor_set_layout_create_info.bindingCount                    = standard_pipeline.FragmentDescriptorSetLayoutBindingCollection.size();
        fragment_descriptor_set_layout_create_info.pBindings                       = standard_pipeline.FragmentDescriptorSetLayoutBindingCollection.data();

        ZENGINE_VALIDATE_ASSERT(
            vkCreateDescriptorSetLayout(device.GetNativeDeviceHandle(), &descriptor_set_layout_create_info, nullptr, &standard_pipeline.DescriptorSetLayout) == VK_SUCCESS,
            "Failed to create DescriptorSetLayout")

        ZENGINE_VALIDATE_ASSERT(
            vkCreateDescriptorSetLayout(device.GetNativeDeviceHandle(), &fragment_descriptor_set_layout_create_info, nullptr, &standard_pipeline.FragmentDescriptorSetLayout) ==
                VK_SUCCESS,
            "Failed to create DescriptorSetLayout")

        std::array<VkDescriptorSetLayout, 2> descriptor_set_layout_collection     = {standard_pipeline.DescriptorSetLayout, standard_pipeline.FragmentDescriptorSetLayout};
        pipeline_specification.StateSpecification.LayoutCreateInfo.setLayoutCount = descriptor_set_layout_collection.size();
        pipeline_specification.StateSpecification.LayoutCreateInfo.pSetLayouts    = descriptor_set_layout_collection.data();

        /*Pipeline layout Creation*/
        standard_pipeline.PipelineLayout = Helpers::CreatePipelineLayout(device, pipeline_specification.StateSpecification.LayoutCreateInfo);

        /*Graphic Pipeline Creation */
        standard_pipeline.Pipeline = Helpers::CreateGraphicPipeline(device, standard_pipeline.PipelineLayout, render_pass, pipeline_specification.StateSpecification);

        /*Framebuffer Creation*/
        standard_pipeline.FramebufferSpecification                          = {};
        standard_pipeline.FramebufferSpecification.RenderPass               = render_pass;
        standard_pipeline.FramebufferSpecification.AttachmentSpecifications = {
            Rendering::Buffers::FrameBuffers::FrameBufferAttachmentSpecificationVNext{
                {.ImageType     = VK_IMAGE_TYPE_2D,
                 .Format        = VK_FORMAT_R8G8B8A8_UNORM,
                 .Tiling        = VK_IMAGE_TILING_OPTIMAL,
                 .InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                 .SampleCount   = VK_SAMPLE_COUNT_1_BIT}},
            Rendering::Buffers::FrameBuffers::FrameBufferAttachmentSpecificationVNext{
                {.ImageType     = VK_IMAGE_TYPE_2D,
                 .Format        = (uint32_t) Helpers::FindDepthFormat(device),
                 .Tiling        = VK_IMAGE_TILING_OPTIMAL,
                 .InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                 .SampleCount   = VK_SAMPLE_COUNT_1_BIT}}};

        standard_pipeline.FramebufferCollection.resize(frame_count);
        ZENGINE_VALIDATE_ASSERT(!standard_pipeline.FramebufferCollection.empty(), "Framebuffer collection can't be empty")
        standard_pipeline.CreateOrResizeFramebuffer(extent.width, extent.height);

        /*DescriptorSet Creation*/
        {
            std::array<VkDescriptorPoolSize, 1> descriptor_pool_size = {};
            descriptor_pool_size[0].type                             = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptor_pool_size[0].descriptorCount                  = frame_count * 2; // Each frame has 2 Descriptors that ref 2 Uniform Buffers

            VkDescriptorPoolCreateInfo descriptor_pool_create_info = {};
            descriptor_pool_create_info.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            descriptor_pool_create_info.poolSizeCount              = descriptor_pool_size.size();
            descriptor_pool_create_info.pPoolSizes                 = descriptor_pool_size.data();
            descriptor_pool_create_info.maxSets                    = frame_count; // A Set is a collection of 3 Descriptors, so a Set per Frame

            ZENGINE_VALIDATE_ASSERT(
                vkCreateDescriptorPool(device.GetNativeDeviceHandle(), &descriptor_pool_create_info, nullptr, &standard_pipeline.DescriptorPool) == VK_SUCCESS,
                "Failed to create DescriptorPool")

            VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {};
            descriptor_set_allocate_info.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptor_set_allocate_info.descriptorPool              = standard_pipeline.DescriptorPool;

            std::vector<VkDescriptorSetLayout> set_layout_collection;
            set_layout_collection.resize(frame_count);
            for (uint32_t i = 0; i < frame_count; ++i)
            {
                set_layout_collection[i] = standard_pipeline.DescriptorSetLayout;
            }
            descriptor_set_allocate_info.descriptorSetCount = set_layout_collection.size();
            descriptor_set_allocate_info.pSetLayouts        = set_layout_collection.data();

            standard_pipeline.DescriptorSetCollection.resize(frame_count, VK_NULL_HANDLE);
            ZENGINE_VALIDATE_ASSERT(
                vkAllocateDescriptorSets(device.GetNativeDeviceHandle(), &descriptor_set_allocate_info, standard_pipeline.DescriptorSetCollection.data()) == VK_SUCCESS,
                "Failed to create DescriptorSet")
        }

        {
            std::array<VkDescriptorPoolSize, 1> fragment_descriptor_pool_size = {};
            fragment_descriptor_pool_size[0].type                             = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            fragment_descriptor_pool_size[0].descriptorCount                  = frame_count * 1; // Each frame has 1 Descriptors that ref 1 Sampler

            VkDescriptorPoolCreateInfo descriptor_pool_create_info = {};
            descriptor_pool_create_info.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            descriptor_pool_create_info.poolSizeCount              = fragment_descriptor_pool_size.size();
            descriptor_pool_create_info.pPoolSizes                 = fragment_descriptor_pool_size.data();
            descriptor_pool_create_info.maxSets                    = frame_count; // A Set is a collection of 3 Descriptors, so a Set per Frame

            ZENGINE_VALIDATE_ASSERT(
                vkCreateDescriptorPool(device.GetNativeDeviceHandle(), &descriptor_pool_create_info, nullptr, &standard_pipeline.FragmentDescriptorPool) == VK_SUCCESS,
                "Failed to create DescriptorPool")

            VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {};
            descriptor_set_allocate_info.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptor_set_allocate_info.descriptorPool              = standard_pipeline.FragmentDescriptorPool;

            std::vector<VkDescriptorSetLayout> fragmen_set_layout_collection;
            fragmen_set_layout_collection.resize(frame_count);
            for (uint32_t i = 0; i < frame_count; ++i)
            {
                fragmen_set_layout_collection[i] = standard_pipeline.FragmentDescriptorSetLayout;
            }
            descriptor_set_allocate_info.descriptorSetCount = fragmen_set_layout_collection.size();
            descriptor_set_allocate_info.pSetLayouts        = fragmen_set_layout_collection.data();

            standard_pipeline.FragmentDescriptorSetCollection.resize(frame_count, VK_NULL_HANDLE);
            ZENGINE_VALIDATE_ASSERT(
                vkAllocateDescriptorSets(device.GetNativeDeviceHandle(), &descriptor_set_allocate_info, standard_pipeline.FragmentDescriptorSetCollection.data()) == VK_SUCCESS,
                "Failed to create DescriptorSet")
        }

        return standard_pipeline;
    }

    VkImage CreateImage(
        Hardwares::VulkanDevice&                device,
        uint32_t                                width,
        uint32_t                                height,
        VkImageType                             image_type,
        VkFormat                                image_format,
        VkImageTiling                           image_tiling,
        VkImageLayout                           image_initial_layout,
        VkImageUsageFlags                       image_usage,
        VkSharingMode                           image_sharing_mode,
        VkSampleCountFlagBits                   image_sample_count,
        VkDeviceMemory&                         device_memory,
        const VkPhysicalDeviceMemoryProperties& memory_properties,
        VkMemoryPropertyFlags                   requested_properties)
    {
        VkImage           image{VK_NULL_HANDLE};
        VkImageCreateInfo image_create_info = {};
        image_create_info.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_create_info.flags             = 0;
        image_create_info.imageType         = image_type;
        image_create_info.extent.width      = width;
        image_create_info.extent.height     = height;
        image_create_info.extent.depth      = 1;
        image_create_info.mipLevels         = 1;
        image_create_info.arrayLayers       = 1;
        image_create_info.format            = image_format;
        image_create_info.tiling            = image_tiling;
        image_create_info.initialLayout     = image_initial_layout;
        image_create_info.usage             = image_usage;
        image_create_info.sharingMode       = image_sharing_mode;
        image_create_info.samples           = image_sample_count;

        ZENGINE_VALIDATE_ASSERT(vkCreateImage(device.GetNativeDeviceHandle(), &image_create_info, nullptr, &image) == VK_SUCCESS, "Failed to create image")

        VkMemoryRequirements memory_requirement;
        vkGetImageMemoryRequirements(device.GetNativeDeviceHandle(), image, &memory_requirement);

        bool     memory_type_index_found{false};
        uint32_t memory_type_index{0};
        for (; memory_type_index < memory_properties.memoryTypeCount; ++memory_type_index)
        {
            if ((memory_requirement.memoryTypeBits & (1 << memory_type_index)) &&
                (memory_properties.memoryTypes[memory_type_index].propertyFlags & requested_properties) == requested_properties)
            {
                memory_type_index_found = true;
                break;
            }
        }

        if (!memory_type_index_found)
        {
            ZENGINE_CORE_ERROR("Failed to find a valid memory type")
        }

        VkMemoryAllocateInfo memory_allocate_info = {};
        memory_allocate_info.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memory_allocate_info.allocationSize       = memory_requirement.size;
        memory_allocate_info.memoryTypeIndex      = memory_type_index;

        // ToDo : Call of vkAllocateMemory(...) isn't optimal because it is limited by maxMemoryAllocationCount
        // It should be replaced by VulkanMemoryAllocator SDK (see :  https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
        ZENGINE_VALIDATE_ASSERT(
            vkAllocateMemory(device.GetNativeDeviceHandle(), &memory_allocate_info, nullptr, &device_memory) == VK_SUCCESS, "Failed to allocate memory for image")
        ZENGINE_VALIDATE_ASSERT(vkBindImageMemory(device.GetNativeDeviceHandle(), image, device_memory, 0) == VK_SUCCESS, "Failed to bind the memory to image")

        return image;
    }

    VkImageView CreateImageView(Hardwares::VulkanDevice& device, VkImage image, VkFormat image_format, VkImageAspectFlagBits image_aspect_flag)
    {
        VkImageView           image_view{VK_NULL_HANDLE};
        VkImageViewCreateInfo image_view_create_info           = {};
        image_view_create_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        image_view_create_info.format                          = image_format;
        image_view_create_info.image                           = image;
        image_view_create_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        image_view_create_info.subresourceRange.aspectMask     = image_aspect_flag /*VK_IMAGE_ASPECT_COLOR_BIT*/;
        image_view_create_info.components.r                    = VK_COMPONENT_SWIZZLE_R;
        image_view_create_info.components.g                    = VK_COMPONENT_SWIZZLE_G;
        image_view_create_info.components.b                    = VK_COMPONENT_SWIZZLE_B;
        image_view_create_info.components.a                    = VK_COMPONENT_SWIZZLE_A;
        image_view_create_info.subresourceRange.baseMipLevel   = 0;
        image_view_create_info.subresourceRange.levelCount     = 1;
        image_view_create_info.subresourceRange.baseArrayLayer = 0;
        image_view_create_info.subresourceRange.layerCount     = 1;

        ZENGINE_VALIDATE_ASSERT(vkCreateImageView(device.GetNativeDeviceHandle(), &image_view_create_info, nullptr, &image_view) == VK_SUCCESS, "Failed to create image view")

        return image_view;
    }

    VkSampler CreateTextureSampler(Hardwares::VulkanDevice& device)
    {
        VkSampler sampler{VK_NULL_HANDLE};

        VkSamplerCreateInfo sampler_create_info = {};
        sampler_create_info.sType               = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_create_info.minFilter           = VK_FILTER_LINEAR;
        sampler_create_info.magFilter           = VK_FILTER_NEAREST;
        sampler_create_info.addressModeU        = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_create_info.addressModeV        = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_create_info.addressModeW        = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        {
            const auto& device_properties     = device.GetPhysicalDeviceProperties();
            sampler_create_info.maxAnisotropy = device_properties.limits.maxSamplerAnisotropy;
        }
        sampler_create_info.borderColor             = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        sampler_create_info.unnormalizedCoordinates = VK_FALSE;
        sampler_create_info.compareEnable           = VK_FALSE;
        sampler_create_info.compareOp               = VK_COMPARE_OP_ALWAYS;
        sampler_create_info.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_create_info.mipLodBias              = 0.0f;
        sampler_create_info.minLod                  = -1000.0f;
        sampler_create_info.maxLod                  = 1000.0f;

        ZENGINE_VALIDATE_ASSERT(vkCreateSampler(device.GetNativeDeviceHandle(), &sampler_create_info, nullptr, &sampler) == VK_SUCCESS, "Failed to create Texture Sampler")

        return sampler;
    }

    Rendering::Meshes::MeshVNext CreateBuiltInMesh(Rendering::Meshes::MeshType mesh_type)
    {
        Rendering::Meshes::MeshVNext custom_mesh = {std::vector<float>{}, std::vector<uint32_t>{}, 0};
        // std::string_view             custom_mesh = "Assets/Meshes/duck.obj";
         //std::string_view             custom_mesh = "Assets/Meshes/cube.obj";
         std::string_view mesh_file = "Assets/Meshes/viking_room.obj";

        Assimp::Importer importer = {};

        const aiScene* scene_ptr = importer.ReadFile(mesh_file.data(), aiProcess_Triangulate | aiProcess_FlipUVs);

        if ((!scene_ptr) || scene_ptr->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene_ptr->mRootNode)
        {
            custom_mesh.VertexCount = 0xFFFFFFFF;
        }
        else
        {
            std::vector<uint32_t> mesh_id_collection;
            bool                  successful = ExtractMeshFromAssimpSceneNode(scene_ptr->mRootNode, &mesh_id_collection);
            if (successful)
            {
                auto meshes = ConvertAssimpMeshToZEngineMeshModel(scene_ptr, mesh_id_collection);

                /*The cube obj model is actually one Mesh, so we are safe */
                if (!meshes.empty())
                {
                    custom_mesh = std::move(meshes.front());
                }
            }
        }

        importer.FreeScene();

        custom_mesh.Type = mesh_type;
        return custom_mesh;
    }

    bool ExtractMeshFromAssimpSceneNode(aiNode* const root_node, std::vector<uint32_t>* const mesh_id_collection_ptr)
    {
        if (!root_node || !mesh_id_collection_ptr)
        {
            return false;
        }

        for (int i = 0; i < root_node->mNumMeshes; ++i)
        {
            mesh_id_collection_ptr->push_back(root_node->mMeshes[i]);
        }

        if (root_node->mNumChildren > 0)
        {
            for (int i = 0; i < root_node->mNumChildren; ++i)
            {
                ExtractMeshFromAssimpSceneNode(root_node->mChildren[i], mesh_id_collection_ptr);
            }
        }

        return true;
    }

    std::vector<Rendering::Meshes::MeshVNext> ConvertAssimpMeshToZEngineMeshModel(const aiScene* assimp_scene, const std::vector<uint32_t>& assimp_mesh_ids)
    {
        std::vector<Rendering::Meshes::MeshVNext> meshes = {};

        for (int i = 0; i < assimp_mesh_ids.size(); ++i)
        {
            aiMesh* assimp_mesh = assimp_scene->mMeshes[assimp_mesh_ids[i]];

            uint32_t              vertex_count{0};
            std::vector<float>    vertices = {};
            std::vector<uint32_t> indices  = {};

            /* Vertice processing */
            for (int x = 0; x < assimp_mesh->mNumVertices; ++x)
            {
                vertices.push_back(assimp_mesh->mVertices[x].x);
                vertices.push_back(assimp_mesh->mVertices[x].y);
                vertices.push_back(assimp_mesh->mVertices[x].z);

                vertices.push_back(assimp_mesh->mNormals[x].x);
                vertices.push_back(assimp_mesh->mNormals[x].y);
                vertices.push_back(assimp_mesh->mNormals[x].z);

                if (assimp_mesh->mTextureCoords[0])
                {
                    vertices.push_back(assimp_mesh->mTextureCoords[0][x].x);
                    vertices.push_back(assimp_mesh->mTextureCoords[0][x].y);
                }
                else
                {
                    vertices.push_back(0.0f);
                    vertices.push_back(0.0f);
                }

                vertex_count++;
            }

            /* Face and Indices processing */
            for (int ix = 0; ix < assimp_mesh->mNumFaces; ++ix)
            {
                aiFace assimp_mesh_face = assimp_mesh->mFaces[ix];

                for (int j = 0; j < assimp_mesh_face.mNumIndices; ++j)
                {
                    indices.push_back(assimp_mesh_face.mIndices[j]);
                }
            }

            Rendering::Meshes::MeshVNext zengine_mesh = {std::move(vertices), std::move(indices), vertex_count};
            meshes.push_back(std::move(zengine_mesh));
        }

        return meshes;
    }
} // namespace ZEngine::Helpers
