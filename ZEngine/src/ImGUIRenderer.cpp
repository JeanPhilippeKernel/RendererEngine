#include <Rendering/Renderers/ImGUIRenderer.h>
#include <backends/imgui_impl_vulkan.h>
#include <ImGuizmo/ImGuizmo.h>
#include <Hardwares/VulkanDevice.h>
#include <backends/imgui_impl_glfw.h>

#include <imgui_impl_vulkan.cpp>

using namespace ZEngine::Hardwares;
using namespace ZEngine::Rendering;

namespace ZEngine::Rendering::Renderers
{
    Pools::CommandPool*                ImGUIRenderer::s_ui_command_pool = nullptr;
    Rendering::Buffers::CommandBuffer* ImGUIRenderer::s_command_buffer  = nullptr;
    VkDescriptorPool                   ImGUIRenderer::s_descriptor_pool  = VK_NULL_HANDLE;

    void ImGUIRenderer::Initialize(void* window, const Ref<Swapchain>& swapchain)
    {
        s_ui_command_pool       = VulkanDevice::CreateCommandPool(QueueType::GRAPHIC_QUEUE, swapchain->GetIdentifier(), true);
        ImGui_ImplGlfw_InitForVulkan(reinterpret_cast<GLFWwindow*>(window), false);

        /*Create DescriptorPool*/
        auto                              device     = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        std::vector<VkDescriptorPoolSize> pool_sizes = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, swapchain->GetImageCount()}};
        VkDescriptorPoolCreateInfo        pool_info  = {};
        pool_info.sType                              = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags                              = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets                            = swapchain->GetImageCount();
        pool_info.poolSizeCount                      = 1;
        pool_info.pPoolSizes                         = pool_sizes.data();
        ZENGINE_VALIDATE_ASSERT(vkCreateDescriptorPool(device, &pool_info, nullptr, &s_descriptor_pool) == VK_SUCCESS, "Failed to create DescriptorPool -- ImGuiLayer")

        ImGui_ImplVulkan_InitInfo imgui_vulkan_init_info = {};
        imgui_vulkan_init_info.Instance                  = Hardwares::VulkanDevice::GetNativeInstanceHandle();
        imgui_vulkan_init_info.PhysicalDevice            = Hardwares::VulkanDevice::GetNativePhysicalDeviceHandle();
        imgui_vulkan_init_info.Device                    = device;
        auto queue_view                                  = Hardwares::VulkanDevice::GetQueue(Rendering::QueueType::GRAPHIC_QUEUE);
        imgui_vulkan_init_info.QueueFamily               = queue_view.FamilyIndex;
        imgui_vulkan_init_info.Queue                     = queue_view.Handle;
        imgui_vulkan_init_info.PipelineCache             = nullptr;
        imgui_vulkan_init_info.DescriptorPool            = s_descriptor_pool;
        imgui_vulkan_init_info.Allocator                 = nullptr;
        imgui_vulkan_init_info.MinImageCount             = swapchain->GetMinImageCount();
        imgui_vulkan_init_info.ImageCount                = swapchain->GetImageCount();
        imgui_vulkan_init_info.CheckVkResultFn           = nullptr;

        ImGui_ImplVulkan_Init(&imgui_vulkan_init_info, swapchain->GetRenderPass());

        // Upload Fonts
        auto command_buffer = Hardwares::VulkanDevice::BeginInstantCommandBuffer(Rendering::QueueType::GRAPHIC_QUEUE);
        {
            ImGui_ImplVulkan_CreateFontsTexture(command_buffer->GetHandle());
        }
        Hardwares::VulkanDevice::EndInstantCommandBuffer(command_buffer);
        __ImGUIDestroyFontUploadObjects();
    }

    void ImGUIRenderer::Deinitialize()
    {
        VulkanDevice::DisposeCommandPool(s_ui_command_pool);
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        ZENGINE_DESTROY_VULKAN_HANDLE(device, vkDestroyDescriptorPool, s_descriptor_pool, nullptr)
    }

    void ImGUIRenderer::BeginFrame()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
    }

    void ImGUIRenderer::Draw(uint32_t window_width, uint32_t window_height, const Ref<Swapchain>& swapchain, void** user_data, void* draw_data)
    {
        ZENGINE_VALIDATE_ASSERT(s_ui_command_pool != nullptr, "UI Command pool can't be null")

        s_command_buffer = s_ui_command_pool->GetCurrentCommmandBuffer();
        s_command_buffer->Begin();
        {
            // Begin RenderPass
            VkRenderPassBeginInfo render_pass_begin_info    = {};
            render_pass_begin_info.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            render_pass_begin_info.renderPass               = swapchain->GetRenderPass();
            render_pass_begin_info.framebuffer              = swapchain->GetCurrentFramebuffer();
            render_pass_begin_info.renderArea.extent.width  = window_width;
            render_pass_begin_info.renderArea.extent.height = window_height;

            std::array<VkClearValue, 1> clear_values = {};
            clear_values[0].color                    = {{0.0f, 0.0f, 0.0f, 1.0f}};
            render_pass_begin_info.clearValueCount   = clear_values.size();
            render_pass_begin_info.pClearValues      = clear_values.data();
            vkCmdBeginRenderPass(s_command_buffer->GetHandle(), &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

            // Record dear imgui primitives into command buffer
            if (user_data)
            {
                __ImGUIRenderDataChildViewport(draw_data, user_data, s_command_buffer->GetHandle(), nullptr);
            }
            else
            {
                __ImGUIRenderData(draw_data, s_command_buffer->GetHandle(), nullptr);
            }

            vkCmdEndRenderPass(s_command_buffer->GetHandle());
        }
        s_command_buffer->End();
        s_command_buffer->Submit();
    }

    void ImGUIRenderer::DrawChildWindow(uint32_t width, uint32_t height, ImguiViewPortWindowChild** window_child, void* draw_data)
    {
        ZENGINE_VALIDATE_ASSERT((*window_child)->CommandPool != nullptr, "UI Command pool can't be null")

        auto command_buffer = (*window_child)->CommandPool->GetCurrentCommmandBuffer();
        command_buffer->Begin();
        {
            // Begin RenderPass
            VkRenderPassBeginInfo render_pass_begin_info    = {};
            render_pass_begin_info.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            render_pass_begin_info.renderPass               = (*window_child)->Swapchain->GetRenderPass();
            render_pass_begin_info.framebuffer              = (*window_child)->Swapchain->GetCurrentFramebuffer();
            render_pass_begin_info.renderArea.extent.width  = width;
            render_pass_begin_info.renderArea.extent.height = height;

            std::array<VkClearValue, 1> clear_values = {};
            clear_values[0].color                    = {{0.0f, 0.0f, 0.0f, 1.0f}};
            render_pass_begin_info.clearValueCount   = clear_values.size();
            render_pass_begin_info.pClearValues      = clear_values.data();
            vkCmdBeginRenderPass(command_buffer->GetHandle(), &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

            // Record dear imgui primitives into command buffer
            __ImGUIRenderDataChildViewport(draw_data, &(*window_child)->RendererUserData, command_buffer->GetHandle(), nullptr);

            vkCmdEndRenderPass(command_buffer->GetHandle());
        }
        command_buffer->End();
        command_buffer->Submit();
    }

    void ImGUIRenderer::EndFrame()
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

    /*
     *  Note for Developers :
     *  Implementing this function has extracted and adapted from **imgui impl vulkan.cpp** to make it compliant with our strategy of enqueuing resources that need disposal.
     *  see : ImGui_ImplVulkan_DestroyFontUploadObjects(...)
     */
    void ImGUIRenderer::__ImGUIDestroyFontUploadObjects()
    {
        ImGui_ImplVulkan_Data*     bd = ImGui_ImplVulkan_GetBackendData();
        ImGui_ImplVulkan_InitInfo* v  = &bd->VulkanInitInfo;
        if (bd->UploadBuffer)
        {
            VulkanDevice::EnqueueForDeletion(DeviceResourceType::BUFFER, bd->UploadBuffer);
            bd->UploadBuffer = VK_NULL_HANDLE;
        }
        if (bd->UploadBufferMemory)
        {
            VulkanDevice::EnqueueForDeletion(DeviceResourceType::BUFFERMEMORY, bd->UploadBufferMemory);
            bd->UploadBufferMemory = VK_NULL_HANDLE;
        }
    }

    /*
     *  Note for Developers :
     *  Implementing this function has extracted and adapted from **imgui impl vulkan.cpp** to make it compliant with our strategy of enqueuing resources that need disposal.
     *  see : ImGui_ImplVulkan_RenderDrawData(...)
     */
    void ImGUIRenderer::__ImGUIRenderData(void* data, VkCommandBuffer command_buffer, VkPipeline pipeline)
    {
        auto draw_data = reinterpret_cast<ImDrawData*>(data);

        // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
        int fb_width  = (int) (draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
        int fb_height = (int) (draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
        if (fb_width <= 0 || fb_height <= 0)
            return;

        ImGui_ImplVulkan_Data*     bd = ImGui_ImplVulkan_GetBackendData();
        ImGui_ImplVulkan_InitInfo* v  = &bd->VulkanInitInfo;
        if (pipeline == VK_NULL_HANDLE)
            pipeline = bd->Pipeline;

        // Allocate array to store enough vertex/index buffers. Each unique viewport gets its own storage.
        ImGui_ImplVulkan_ViewportData* viewport_renderer_data = (ImGui_ImplVulkan_ViewportData*) draw_data->OwnerViewport->RendererUserData;
        IM_ASSERT(viewport_renderer_data != nullptr);
        ImGui_ImplVulkanH_WindowRenderBuffers* wrb = &viewport_renderer_data->RenderBuffers;
        if (wrb->FrameRenderBuffers == nullptr)
        {
            wrb->Index              = 0;
            wrb->Count              = v->ImageCount;
            wrb->FrameRenderBuffers = (ImGui_ImplVulkanH_FrameRenderBuffers*) IM_ALLOC(sizeof(ImGui_ImplVulkanH_FrameRenderBuffers) * wrb->Count);
            memset(wrb->FrameRenderBuffers, 0, sizeof(ImGui_ImplVulkanH_FrameRenderBuffers) * wrb->Count);
        }
        IM_ASSERT(wrb->Count == v->ImageCount);
        wrb->Index                               = (wrb->Index + 1) % wrb->Count;
        ImGui_ImplVulkanH_FrameRenderBuffers* rb = &wrb->FrameRenderBuffers[wrb->Index];

        if (draw_data->TotalVtxCount > 0)
        {
            // Create or resize the vertex/index buffers
            size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
            size_t index_size  = draw_data->TotalIdxCount * sizeof(ImDrawIdx);
            if (rb->VertexBuffer == VK_NULL_HANDLE || rb->VertexBufferSize < vertex_size)
                __ImGUICreateOrResizeBuffer(rb->VertexBuffer, rb->VertexBufferMemory, rb->VertexBufferSize, vertex_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            if (rb->IndexBuffer == VK_NULL_HANDLE || rb->IndexBufferSize < index_size)
                __ImGUICreateOrResizeBuffer(rb->IndexBuffer, rb->IndexBufferMemory, rb->IndexBufferSize, index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

            // Upload vertex/index data into a single contiguous GPU buffer
            ImDrawVert* vtx_dst = nullptr;
            ImDrawIdx*  idx_dst = nullptr;
            VkResult    err     = vkMapMemory(v->Device, rb->VertexBufferMemory, 0, rb->VertexBufferSize, 0, (void**) (&vtx_dst));
            check_vk_result(err);
            err = vkMapMemory(v->Device, rb->IndexBufferMemory, 0, rb->IndexBufferSize, 0, (void**) (&idx_dst));
            check_vk_result(err);
            for (int n = 0; n < draw_data->CmdListsCount; n++)
            {
                const ImDrawList* cmd_list = draw_data->CmdLists[n];
                memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
                memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
                vtx_dst += cmd_list->VtxBuffer.Size;
                idx_dst += cmd_list->IdxBuffer.Size;
            }
            VkMappedMemoryRange range[2] = {};
            range[0].sType               = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range[0].memory              = rb->VertexBufferMemory;
            range[0].size                = VK_WHOLE_SIZE;
            range[1].sType               = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range[1].memory              = rb->IndexBufferMemory;
            range[1].size                = VK_WHOLE_SIZE;
            err                          = vkFlushMappedMemoryRanges(v->Device, 2, range);
            check_vk_result(err);
            vkUnmapMemory(v->Device, rb->VertexBufferMemory);
            vkUnmapMemory(v->Device, rb->IndexBufferMemory);
        }

        // Setup desired Vulkan state
        ImGui_ImplVulkan_SetupRenderState(draw_data, pipeline, command_buffer, rb, fb_width, fb_height);

        // Will project scissor/clipping rectangles into framebuffer space
        ImVec2 clip_off   = draw_data->DisplayPos;       // (0,0) unless using multi-viewports
        ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

        // Render command lists
        // (Because we merged all buffers into a single one, we maintain our own offset into them)
        int global_vtx_offset = 0;
        int global_idx_offset = 0;
        for (int n = 0; n < draw_data->CmdListsCount; n++)
        {
            const ImDrawList* cmd_list = draw_data->CmdLists[n];
            for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
            {
                const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
                if (pcmd->UserCallback != nullptr)
                {
                    // User callback, registered via ImDrawList::AddCallback()
                    // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                    if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                        ImGui_ImplVulkan_SetupRenderState(draw_data, pipeline, command_buffer, rb, fb_width, fb_height);
                    else
                        pcmd->UserCallback(cmd_list, pcmd);
                }
                else
                {
                    // Project scissor/clipping rectangles into framebuffer space
                    ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                    ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

                    // Clamp to viewport as vkCmdSetScissor() won't accept values that are off bounds
                    if (clip_min.x < 0.0f)
                    {
                        clip_min.x = 0.0f;
                    }
                    if (clip_min.y < 0.0f)
                    {
                        clip_min.y = 0.0f;
                    }
                    if (clip_max.x > fb_width)
                    {
                        clip_max.x = (float) fb_width;
                    }
                    if (clip_max.y > fb_height)
                    {
                        clip_max.y = (float) fb_height;
                    }
                    if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                        continue;

                    // Apply scissor/clipping rectangle
                    VkRect2D scissor;
                    scissor.offset.x      = (int32_t) (clip_min.x);
                    scissor.offset.y      = (int32_t) (clip_min.y);
                    scissor.extent.width  = (uint32_t) (clip_max.x - clip_min.x);
                    scissor.extent.height = (uint32_t) (clip_max.y - clip_min.y);
                    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

                    // Bind DescriptorSet with font or user texture
                    VkDescriptorSet desc_set[1] = {(VkDescriptorSet) pcmd->TextureId};
                    if (sizeof(ImTextureID) < sizeof(ImU64))
                    {
                        // We don't support texture switches if ImTextureID hasn't been redefined to be 64-bit. Do a flaky check that other textures haven't been used.
                        IM_ASSERT(pcmd->TextureId == (ImTextureID) bd->FontDescriptorSet);
                        desc_set[0] = bd->FontDescriptorSet;
                    }
                    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, bd->PipelineLayout, 0, 1, desc_set, 0, nullptr);

                    // Draw
                    vkCmdDrawIndexed(command_buffer, pcmd->ElemCount, 1, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset, 0);
                }
            }
            global_idx_offset += cmd_list->IdxBuffer.Size;
            global_vtx_offset += cmd_list->VtxBuffer.Size;
        }

        // Note: at this point both vkCmdSetViewport() and vkCmdSetScissor() have been called.
        // Our last values will leak into user/application rendering IF:
        // - Your app uses a pipeline with VK_DYNAMIC_STATE_VIEWPORT or VK_DYNAMIC_STATE_SCISSOR dynamic state
        // - And you forgot to call vkCmdSetViewport() and vkCmdSetScissor() yourself to explicitly set that state.
        // If you use VK_DYNAMIC_STATE_VIEWPORT or VK_DYNAMIC_STATE_SCISSOR you are responsible for setting the values before rendering.
        // In theory we should aim to backup/restore those values but I am not sure this is possible.
        // We perform a call to vkCmdSetScissor() to set back a full viewport which is likely to fix things for 99% users but technically this is not perfect. (See github #4644)
        VkRect2D scissor = {{0, 0}, {(uint32_t) fb_width, (uint32_t) fb_height}};
        vkCmdSetScissor(command_buffer, 0, 1, &scissor);
    }

    void ImGUIRenderer::__ImGUIRenderDataChildViewport(void* data, void** user_data, VkCommandBuffer command_buffer, VkPipeline pipeline)
    {
        auto draw_data = reinterpret_cast<ImDrawData*>(data);

        // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
        int fb_width  = (int) (draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
        int fb_height = (int) (draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
        if (fb_width <= 0 || fb_height <= 0)
            return;

        ImGui_ImplVulkan_Data*     bd = ImGui_ImplVulkan_GetBackendData();
        ImGui_ImplVulkan_InitInfo* v  = &bd->VulkanInitInfo;
        if (pipeline == VK_NULL_HANDLE)
            pipeline = bd->Pipeline;

        // Allocate array to store enough vertex/index buffers. Each unique viewport gets its own storage.
        if ((*user_data) == nullptr)
        {
            *user_data = IM_ALLOC(sizeof(ImGui_ImplVulkan_ViewportData));
            memset(*user_data, 0, sizeof(ImGui_ImplVulkan_ViewportData));
        }

        ImGui_ImplVulkan_ViewportData* viewport_renderer_data = (ImGui_ImplVulkan_ViewportData*) *user_data;
        IM_ASSERT(viewport_renderer_data != nullptr);
        ImGui_ImplVulkanH_WindowRenderBuffers* wrb = &viewport_renderer_data->RenderBuffers;
        if (wrb->FrameRenderBuffers == nullptr)
        {
            wrb->Index              = 0;
            wrb->Count              = v->ImageCount;
            wrb->FrameRenderBuffers = (ImGui_ImplVulkanH_FrameRenderBuffers*) IM_ALLOC(sizeof(ImGui_ImplVulkanH_FrameRenderBuffers) * wrb->Count);
            memset(wrb->FrameRenderBuffers, 0, sizeof(ImGui_ImplVulkanH_FrameRenderBuffers) * wrb->Count);
        }
        IM_ASSERT(wrb->Count == v->ImageCount);
        wrb->Index                               = (wrb->Index + 1) % wrb->Count;
        ImGui_ImplVulkanH_FrameRenderBuffers* rb = &wrb->FrameRenderBuffers[wrb->Index];

        if (draw_data->TotalVtxCount > 0)
        {
            // Create or resize the vertex/index buffers
            size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
            size_t index_size  = draw_data->TotalIdxCount * sizeof(ImDrawIdx);
            if (rb->VertexBuffer == VK_NULL_HANDLE || rb->VertexBufferSize < vertex_size)
                __ImGUICreateOrResizeBuffer(rb->VertexBuffer, rb->VertexBufferMemory, rb->VertexBufferSize, vertex_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            if (rb->IndexBuffer == VK_NULL_HANDLE || rb->IndexBufferSize < index_size)
                __ImGUICreateOrResizeBuffer(rb->IndexBuffer, rb->IndexBufferMemory, rb->IndexBufferSize, index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

            // Upload vertex/index data into a single contiguous GPU buffer
            ImDrawVert* vtx_dst = nullptr;
            ImDrawIdx*  idx_dst = nullptr;
            VkResult    err     = vkMapMemory(v->Device, rb->VertexBufferMemory, 0, rb->VertexBufferSize, 0, (void**) (&vtx_dst));
            check_vk_result(err);
            err = vkMapMemory(v->Device, rb->IndexBufferMemory, 0, rb->IndexBufferSize, 0, (void**) (&idx_dst));
            check_vk_result(err);
            for (int n = 0; n < draw_data->CmdListsCount; n++)
            {
                const ImDrawList* cmd_list = draw_data->CmdLists[n];
                memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
                memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
                vtx_dst += cmd_list->VtxBuffer.Size;
                idx_dst += cmd_list->IdxBuffer.Size;
            }
            VkMappedMemoryRange range[2] = {};
            range[0].sType               = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range[0].memory              = rb->VertexBufferMemory;
            range[0].size                = VK_WHOLE_SIZE;
            range[1].sType               = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range[1].memory              = rb->IndexBufferMemory;
            range[1].size                = VK_WHOLE_SIZE;
            err                          = vkFlushMappedMemoryRanges(v->Device, 2, range);
            check_vk_result(err);
            vkUnmapMemory(v->Device, rb->VertexBufferMemory);
            vkUnmapMemory(v->Device, rb->IndexBufferMemory);
        }

        // Setup desired Vulkan state
        ImGui_ImplVulkan_SetupRenderState(draw_data, pipeline, command_buffer, rb, fb_width, fb_height);

        // Will project scissor/clipping rectangles into framebuffer space
        ImVec2 clip_off   = draw_data->DisplayPos;       // (0,0) unless using multi-viewports
        ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

        // Render command lists
        // (Because we merged all buffers into a single one, we maintain our own offset into them)
        int global_vtx_offset = 0;
        int global_idx_offset = 0;
        for (int n = 0; n < draw_data->CmdListsCount; n++)
        {
            const ImDrawList* cmd_list = draw_data->CmdLists[n];
            for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
            {
                const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
                if (pcmd->UserCallback != nullptr)
                {
                    // User callback, registered via ImDrawList::AddCallback()
                    // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                    if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                        ImGui_ImplVulkan_SetupRenderState(draw_data, pipeline, command_buffer, rb, fb_width, fb_height);
                    else
                        pcmd->UserCallback(cmd_list, pcmd);
                }
                else
                {
                    // Project scissor/clipping rectangles into framebuffer space
                    ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                    ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

                    // Clamp to viewport as vkCmdSetScissor() won't accept values that are off bounds
                    if (clip_min.x < 0.0f)
                    {
                        clip_min.x = 0.0f;
                    }
                    if (clip_min.y < 0.0f)
                    {
                        clip_min.y = 0.0f;
                    }
                    if (clip_max.x > fb_width)
                    {
                        clip_max.x = (float) fb_width;
                    }
                    if (clip_max.y > fb_height)
                    {
                        clip_max.y = (float) fb_height;
                    }
                    if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                        continue;

                    // Apply scissor/clipping rectangle
                    VkRect2D scissor;
                    scissor.offset.x      = (int32_t) (clip_min.x);
                    scissor.offset.y      = (int32_t) (clip_min.y);
                    scissor.extent.width  = (uint32_t) (clip_max.x - clip_min.x);
                    scissor.extent.height = (uint32_t) (clip_max.y - clip_min.y);
                    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

                    // Bind DescriptorSet with font or user texture
                    VkDescriptorSet desc_set[1] = {(VkDescriptorSet) pcmd->TextureId};
                    if (sizeof(ImTextureID) < sizeof(ImU64))
                    {
                        // We don't support texture switches if ImTextureID hasn't been redefined to be 64-bit. Do a flaky check that other textures haven't been used.
                        IM_ASSERT(pcmd->TextureId == (ImTextureID) bd->FontDescriptorSet);
                        desc_set[0] = bd->FontDescriptorSet;
                    }
                    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, bd->PipelineLayout, 0, 1, desc_set, 0, nullptr);

                    // Draw
                    vkCmdDrawIndexed(command_buffer, pcmd->ElemCount, 1, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset, 0);
                }
            }
            global_idx_offset += cmd_list->IdxBuffer.Size;
            global_vtx_offset += cmd_list->VtxBuffer.Size;
        }

        // Note: at this point both vkCmdSetViewport() and vkCmdSetScissor() have been called.
        // Our last values will leak into user/application rendering IF:
        // - Your app uses a pipeline with VK_DYNAMIC_STATE_VIEWPORT or VK_DYNAMIC_STATE_SCISSOR dynamic state
        // - And you forgot to call vkCmdSetViewport() and vkCmdSetScissor() yourself to explicitly set that state.
        // If you use VK_DYNAMIC_STATE_VIEWPORT or VK_DYNAMIC_STATE_SCISSOR you are responsible for setting the values before rendering.
        // In theory we should aim to backup/restore those values but I am not sure this is possible.
        // We perform a call to vkCmdSetScissor() to set back a full viewport which is likely to fix things for 99% users but technically this is not perfect. (See github #4644)
        VkRect2D scissor = {{0, 0}, {(uint32_t) fb_width, (uint32_t) fb_height}};
        vkCmdSetScissor(command_buffer, 0, 1, &scissor);
    }

    /*
     *  Note for Developers :
     *  Implementing this function has extracted and adapted from **imgui impl vulkan.cpp** to make it compliant with our strategy of enqueuing resources that need disposal.
     *  see : CreateOrResizeBuffer(...)
     */
    void ImGUIRenderer::__ImGUICreateOrResizeBuffer(VkBuffer& buffer, VkDeviceMemory& buffer_memory, VkDeviceSize& p_buffer_size, size_t new_size, VkBufferUsageFlagBits usage)
    {
        ImGui_ImplVulkan_Data*     bd = ImGui_ImplVulkan_GetBackendData();
        ImGui_ImplVulkan_InitInfo* v  = &bd->VulkanInitInfo;
        VkResult                   err;
        if (buffer != VK_NULL_HANDLE)
        {
            // vkDestroyBuffer(v->Device, buffer, v->Allocator);
            VulkanDevice::EnqueueForDeletion(DeviceResourceType::BUFFER, buffer);
            buffer = VK_NULL_HANDLE;
        }
        if (buffer_memory != VK_NULL_HANDLE)
        {
            // vkFreeMemory(v->Device, buffer_memory, v->Allocator);
            VulkanDevice::EnqueueForDeletion(DeviceResourceType::BUFFERMEMORY, buffer_memory);
            buffer_memory = VK_NULL_HANDLE;
        }

        VkDeviceSize       vertex_buffer_size_aligned = ((new_size - 1) / bd->BufferMemoryAlignment + 1) * bd->BufferMemoryAlignment;
        VkBufferCreateInfo buffer_info                = {};
        buffer_info.sType                             = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size                              = vertex_buffer_size_aligned;
        buffer_info.usage                             = usage;
        buffer_info.sharingMode                       = VK_SHARING_MODE_EXCLUSIVE;
        err                                           = vkCreateBuffer(v->Device, &buffer_info, v->Allocator, &buffer);
        check_vk_result(err);

        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(v->Device, buffer, &req);
        bd->BufferMemoryAlignment       = (bd->BufferMemoryAlignment > req.alignment) ? bd->BufferMemoryAlignment : req.alignment;
        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize       = req.size;
        alloc_info.memoryTypeIndex      = ImGui_ImplVulkan_MemoryType(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, req.memoryTypeBits);
        err                             = vkAllocateMemory(v->Device, &alloc_info, v->Allocator, &buffer_memory);
        check_vk_result(err);

        err = vkBindBufferMemory(v->Device, buffer, buffer_memory, 0);
        check_vk_result(err);
        p_buffer_size = req.size;
    }

    ImguiViewPortWindowChild::ImguiViewPortWindowChild(void* viewport_handle)
    {
        Swapchain   = CreateRef<Rendering::Swapchain>(viewport_handle, false);
        CommandPool = VulkanDevice::CreateCommandPool(QueueType::GRAPHIC_QUEUE, this->Swapchain->GetIdentifier(), true);
    }
} // namespace ZEngine::Rendering::Renderers