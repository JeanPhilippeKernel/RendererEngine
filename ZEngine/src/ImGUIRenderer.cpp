#include <pch.h>
#include <Rendering/Renderers/ImGUIRenderer.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <backends/imgui_impl_vulkan.h>
#include <ImGuizmo/ImGuizmo.h>
#include <Hardwares/VulkanDevice.h>
#include <backends/imgui_impl_glfw.h>

#include <Engine.h>

using namespace ZEngine::Hardwares;
using namespace ZEngine::Rendering;

namespace ZEngine::Rendering::Renderers
{
    void ImGUIRenderer::Initialize(const Ref<Swapchain>& swapchain)
    {
        auto current_window = Engine::GetWindow();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        StyleDarkTheme();

        ImGuiIO& io                     = ImGui::GetIO();
        io.ConfigViewportsNoTaskBarIcon = true;
        io.ConfigViewportsNoDecoration  = true;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendFlags |= ImGuiBackendFlags_HasMouseHoveredViewport;
        io.BackendRendererName = "ZEngine";

        std::string_view default_layout_ini = "Settings/DefaultLayout.ini";
        const auto       current_directoy   = std::filesystem::current_path();
        auto             layout_file_path   = fmt::format("{0}/{1}", current_directoy.string(), default_layout_ini);
        if (std::filesystem::exists(std::filesystem::path(layout_file_path)))
        {
            io.IniFilename = default_layout_ini.data();
        }

        // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        auto& style            = ImGui::GetStyle();
        style.WindowBorderSize = 0.f;
        style.ChildBorderSize  = 0.f;
        style.FrameRounding    = 7.0f;

        auto window_property = current_window->GetWindowProperty();
        io.Fonts->AddFontFromFileTTF("Settings/Fonts/OpenSans/OpenSans-Bold.ttf", 17.f * window_property.DpiScale);
        io.FontDefault     = io.Fonts->AddFontFromFileTTF("Settings/Fonts/OpenSans/OpenSans-Regular.ttf", 17.f * window_property.DpiScale);
        io.FontGlobalScale = window_property.DpiScale;

        ImGui_ImplGlfw_InitForVulkan(reinterpret_cast<GLFWwindow*>(current_window->GetNativeWindow()), false);

        const auto& renderer_info = Renderers::GraphicRenderer::GetRendererInformation();

        m_vertex_buffer                                                       = CreateRef<Buffers::VertexBufferSet>(renderer_info.FrameCount);
        m_index_buffer                                                        = CreateRef<Buffers::IndexBufferSet>(renderer_info.FrameCount);
        Specifications::GraphicRendererPipelineSpecification ui_pipeline_spec = {};
        ui_pipeline_spec.DebugName                                            = "Imgui-pipeline";
        ui_pipeline_spec.SwapchainAsRenderTarget                              = true;
        ui_pipeline_spec.SwapchainRenderTarget                                = swapchain;
        ui_pipeline_spec.ShaderSpecification                                  = {};
        ui_pipeline_spec.ShaderSpecification.VertexFilename                   = "Shaders/Cache/imgui_vertex.spv";
        ui_pipeline_spec.ShaderSpecification.FragmentFilename                 = "Shaders/Cache/imgui_fragment.spv";
        ui_pipeline_spec.ShaderSpecification.OverloadMaxSet                   = 2000;

        ui_pipeline_spec.VertexInputBindingSpecifications.resize(1);
        ui_pipeline_spec.VertexInputBindingSpecifications[0].Stride = sizeof(ImDrawVert);
        ui_pipeline_spec.VertexInputBindingSpecifications[0].Rate   = VK_VERTEX_INPUT_RATE_VERTEX;

        ui_pipeline_spec.VertexInputAttributeSpecifications.resize(3);
        ui_pipeline_spec.VertexInputAttributeSpecifications[0].Location = 0;
        ui_pipeline_spec.VertexInputAttributeSpecifications[0].Binding  = ui_pipeline_spec.VertexInputBindingSpecifications[0].Binding;
        ui_pipeline_spec.VertexInputAttributeSpecifications[0].Format   = Specifications::ImageFormat::R32G32_SFLOAT;
        ui_pipeline_spec.VertexInputAttributeSpecifications[0].Offset   = IM_OFFSETOF(ImDrawVert, pos);
        ui_pipeline_spec.VertexInputAttributeSpecifications[1].Location = 1;
        ui_pipeline_spec.VertexInputAttributeSpecifications[1].Binding  = ui_pipeline_spec.VertexInputBindingSpecifications[0].Binding;
        ui_pipeline_spec.VertexInputAttributeSpecifications[1].Format   = Specifications::ImageFormat::R32G32_SFLOAT;
        ui_pipeline_spec.VertexInputAttributeSpecifications[1].Offset   = IM_OFFSETOF(ImDrawVert, uv);
        ui_pipeline_spec.VertexInputAttributeSpecifications[2].Location = 2;
        ui_pipeline_spec.VertexInputAttributeSpecifications[2].Binding  = ui_pipeline_spec.VertexInputBindingSpecifications[0].Binding;
        ui_pipeline_spec.VertexInputAttributeSpecifications[2].Format   = Specifications::ImageFormat::R8G8B8A8_UNORM;
        ui_pipeline_spec.VertexInputAttributeSpecifications[2].Offset   = IM_OFFSETOF(ImDrawVert, col);

        RenderPasses::RenderPassSpecification ui_pass_spec = {};
        ui_pass_spec.Pipeline                              = Pipelines::GraphicPipeline::Create(ui_pipeline_spec);
        m_ui_pass                                          = RenderPasses::RenderPass::Create(ui_pass_spec);
        m_ui_pass->Verify();
        m_ui_pass->Bake();

        auto  pipeline             = m_ui_pass->GetPipeline();
        auto  shader               = pipeline->GetShader();
        auto  descriptor_setlayout = shader->GetDescriptorSetLayout()[0];

        auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        /*
         * Font uploading
         */
        unsigned char* pixels;
        int            width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        size_t upload_size = width * height * 4 * sizeof(char);

        Specifications::TextureSpecification font_tex_spec = {};
        font_tex_spec.Width                                = width;
        font_tex_spec.Height                               = height;
        font_tex_spec.Data                                 = pixels;
        font_tex_spec.Format                               = Specifications::ImageFormat::R8G8B8A8_UNORM;

        Ref<Textures::Texture> font_texture = Textures::Texture2D::Create(font_tex_spec);

        VkDescriptorSetAllocateInfo font_alloc_info = {};
        font_alloc_info.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        font_alloc_info.descriptorPool              = shader->GetDescriptorPool();
        font_alloc_info.descriptorSetCount          = 1;
        font_alloc_info.pSetLayouts                 = &descriptor_setlayout;
        ZENGINE_VALIDATE_ASSERT(vkAllocateDescriptorSets(device, &font_alloc_info, &m_font_descriptor_set) == VK_SUCCESS, "Failed to create descriptor set")

        auto& font_image_info = font_texture->GetDescriptorImageInfo();
        VkWriteDescriptorSet write_desc[1] = {};
        write_desc[0].sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_desc[0].dstSet               = m_font_descriptor_set;
        write_desc[0].descriptorCount      = 1;
        write_desc[0].descriptorType       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write_desc[0].pImageInfo           = &font_image_info;
        vkUpdateDescriptorSets(device, 1, write_desc, 0, nullptr);

        io.Fonts->SetTexID((ImTextureID) m_font_descriptor_set);
        /*
         * Creating another DescriptorSet from the SetLayout that will serve has Image/Frame output
         */
        VkDescriptorSetAllocateInfo frame_alloc_info = {};
        frame_alloc_info.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        frame_alloc_info.descriptorPool              = shader->GetDescriptorPool();
        frame_alloc_info.descriptorSetCount          = 1;
        frame_alloc_info.pSetLayouts                 = &descriptor_setlayout;
        ZENGINE_VALIDATE_ASSERT(vkAllocateDescriptorSets(device, &frame_alloc_info, &m_frame_output) == VK_SUCCESS, "Failed to create descriptor set")
    }

    void ImGUIRenderer::Deinitialize()
    {
        m_ui_pass->Dispose();
        m_vertex_buffer->Dispose();
        m_index_buffer->Dispose();

        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void ImGUIRenderer::StyleDarkTheme()
    {
        auto& colors              = ImGui::GetStyle().Colors;
        colors[ImGuiCol_WindowBg] = ImVec4{0.1f, 0.105f, 0.11f, 1.0f};

        // Headers
        colors[ImGuiCol_Header]        = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
        colors[ImGuiCol_HeaderHovered] = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
        colors[ImGuiCol_HeaderActive]  = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

        // Buttons
        colors[ImGuiCol_Button]        = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
        colors[ImGuiCol_ButtonHovered] = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
        colors[ImGuiCol_ButtonActive]  = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

        // Frame BG
        colors[ImGuiCol_FrameBg]        = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
        colors[ImGuiCol_FrameBgHovered] = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
        colors[ImGuiCol_FrameBgActive]  = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

        // Tabs
        colors[ImGuiCol_Tab]                = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
        colors[ImGuiCol_TabHovered]         = ImVec4{0.38f, 0.3805f, 0.381f, 1.0f};
        colors[ImGuiCol_TabActive]          = ImVec4{0.28f, 0.2805f, 0.281f, 1.0f};
        colors[ImGuiCol_TabUnfocused]       = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};

        // Title
        colors[ImGuiCol_TitleBg]          = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
        colors[ImGuiCol_TitleBgActive]    = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

        colors[ImGuiCol_DockingPreview]   = ImVec4{0.2f, 0.205f, 0.21f, .5f};
        colors[ImGuiCol_SeparatorHovered] = ImVec4{1.f, 1.f, 1.0f, .5f};
        colors[ImGuiCol_SeparatorActive]  = ImVec4{1.f, 1.f, 1.0f, .5f};
        colors[ImGuiCol_CheckMark]        = ImVec4{1.0f, 1.f, 1.0f, 1.f};
    }

    void ImGUIRenderer::BeginFrame(Rendering::Buffers::CommandBuffer* const command_buffer)
    {
        ImGui_ImplGlfw_NewFrame();

        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        command_buffer->Begin();
    }

    void ImGUIRenderer::Draw(Rendering::Buffers::CommandBuffer* const command_buffer, uint32_t frame_index)
    {
        ImGui::Render();
        auto draw_data = ImGui::GetDrawData();
        if (draw_data && draw_data->TotalVtxCount > 0)
        {
            // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
            int fb_width  = (int) (draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
            int fb_height = (int) (draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
            if (fb_width <= 0 || fb_height <= 0)
                return;

            std::vector<ImDrawVert> vertex_data_collection = {};
            std::vector<ImDrawIdx>  index_data_collection  = {};
            for (int n = 0; n < draw_data->CmdListsCount; n++)
            {
                const ImDrawList* cmd_list = draw_data->CmdLists[n];
                std::copy(std::begin(cmd_list->VtxBuffer), std::end(cmd_list->VtxBuffer), std::back_inserter(vertex_data_collection));
                std::copy(std::begin(cmd_list->IdxBuffer), std::end(cmd_list->IdxBuffer), std::back_inserter(index_data_collection));
            }
            auto& vertex_buffer = *m_vertex_buffer;
            auto& index_buffer  = *m_index_buffer;

            vertex_buffer[frame_index].SetData(vertex_data_collection);
            index_buffer[frame_index].SetData(index_data_collection);
        }
    }

    void ImGUIRenderer::EndFrame(Rendering::Buffers::CommandBuffer* const command_buffer, uint32_t frame_index)
    {
        command_buffer->BeginRenderPass(m_ui_pass);
        {
            auto& vertex_buffer = *m_vertex_buffer;
            auto& index_buffer  = *m_index_buffer;
            command_buffer->BindVertexBuffer(vertex_buffer[frame_index]);
            command_buffer->BindIndexBuffer(index_buffer[frame_index], sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);

            // Setup scale and translation:
            // Our visible imgui space lies from draw_data->DisplayPps (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single
            // viewport apps.
            auto draw_data = ImGui::GetDrawData();

            if (draw_data)
            {
                float scale[2];
                scale[0] = 2.0f / draw_data->DisplaySize.x;
                scale[1] = 2.0f / draw_data->DisplaySize.y;
                float translate[2];
                translate[0] = -1.0f - draw_data->DisplayPos.x * scale[0];
                translate[1] = -1.0f - draw_data->DisplayPos.y * scale[1];
                command_buffer->PushConstants(VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 0, sizeof(float) * 2, scale);
                command_buffer->PushConstants(VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 2, sizeof(float) * 2, translate);

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
                            {
                                // ImGui_ImplVulkan_SetupRenderState(draw_data, pipeline, command_buffer, rb, fb_width, fb_height);
                            }
                            else
                                pcmd->UserCallback(cmd_list, pcmd);
                        }
                        else
                        {
                            auto current_window = Engine::GetWindow();
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
                            if (clip_max.x > current_window->GetWidth())
                            {
                                clip_max.x = (float) current_window->GetWidth();
                            }
                            if (clip_max.y > current_window->GetHeight())
                            {
                                clip_max.y = (float) current_window->GetHeight();
                            }
                            if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                                continue;

                            // Apply scissor/clipping rectangle
                            VkRect2D scissor;
                            scissor.offset.x      = (int32_t) (clip_min.x);
                            scissor.offset.y      = (int32_t) (clip_min.y);
                            scissor.extent.width  = (uint32_t) (clip_max.x - clip_min.x);
                            scissor.extent.height = (uint32_t) (clip_max.y - clip_min.y);
                            command_buffer->SetScissor(scissor);

                            // Bind DescriptorSet with font or user texture
                            VkDescriptorSet desc_set[1] = {(VkDescriptorSet) pcmd->TextureId};
                            if (sizeof(ImTextureID) < sizeof(ImU64))
                            {
                                // We don't support texture switches if ImTextureID hasn't been redefined to be 64-bit. Do a flaky check that other textures haven't been used.
                                IM_ASSERT(pcmd->TextureId == (ImTextureID) m_font_descriptor_set);
                                desc_set[0] = m_font_descriptor_set;
                            }

                            command_buffer->BindDescriptorSet(desc_set[0]);
                            command_buffer->DrawIndexed(pcmd->ElemCount, 1, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset, 0);
                        }
                    }
                    global_idx_offset += cmd_list->IdxBuffer.Size;
                    global_vtx_offset += cmd_list->VtxBuffer.Size;
                }
            }
        }
        command_buffer->EndRenderPass();
        command_buffer->End();

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

    VkDescriptorSet ImGUIRenderer::UpdateFrameOutput(const Ref<Buffers::Image2DBuffer>& buffer)
    {
        auto                  device        = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        VkDescriptorImageInfo desc_image[1] = {};
        desc_image[0].sampler               = buffer->GetSampler();
        desc_image[0].imageView             = buffer->GetImageViewHandle();
        desc_image[0].imageLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write_desc[1]  = {};
        write_desc[0].sType                 = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_desc[0].dstSet                = m_frame_output;
        write_desc[0].descriptorCount       = 1;
        write_desc[0].descriptorType        = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write_desc[0].pImageInfo            = desc_image;
        vkUpdateDescriptorSets(device, 1, write_desc, 0, nullptr);

        return m_frame_output;
    }
} // namespace ZEngine::Rendering::Renderers