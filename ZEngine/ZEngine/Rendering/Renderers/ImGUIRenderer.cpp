#include <pch.h>
#include <GraphicRenderer.h>
#include <Hardwares/VulkanDevice.h>
#include <ImGuizmo/ImGuizmo.h>
#include <Rendering/Renderers/ImGUIRenderer.h>
#include <Windows/CoreWindow.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

using namespace ZEngine::Hardwares;
using namespace ZEngine::Rendering;
using namespace ZEngine::Rendering::Textures;
using namespace ZEngine::Helpers;

namespace ZEngine::Rendering::Renderers
{
    void ImGUIRenderer::Initialize(GraphicRenderer* renderer)
    {
        m_renderer          = renderer;
        auto current_window = renderer->Device->CurrentWindow;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        StyleDarkTheme();

        ImGuiIO& io                          = ImGui::GetIO();
        io.ConfigViewportsNoTaskBarIcon      = true;
        io.ConfigViewportsNoDecoration       = true;
        io.BackendFlags                     |= ImGuiBackendFlags_RendererHasViewports;
        io.BackendFlags                     |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendFlags                     |= ImGuiBackendFlags_HasMouseHoveredViewport;
        io.BackendRendererName               = "ZEngine-Imgui";

        std::string_view default_layout_ini  = "Settings/DefaultLayout.ini";
        const auto       current_directoy    = std::filesystem::current_path();
        auto             layout_file_path    = fmt::format("{0}/{1}", current_directoy.string(), default_layout_ini);
        if (std::filesystem::exists(std::filesystem::path(layout_file_path)))
        {
            io.IniFilename = default_layout_ini.data();
        }

        // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags         |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags         |= ImGuiConfigFlags_ViewportsEnable;

        auto& style             = ImGui::GetStyle();
        style.WindowBorderSize  = 0.f;
        style.ChildBorderSize   = 0.f;
        style.FrameRounding     = 7.0f;

        io.FontDefault          = io.Fonts->AddFontFromFileTTF("Settings/Fonts/OpenSans/OpenSans-Regular.ttf", 17.f);

        ImGui_ImplGlfw_InitForVulkan(reinterpret_cast<GLFWwindow*>(current_window->GetNativeWindow()), false);

        m_vertex_buffer_handle = renderer->Device->CreateVertexBufferSet();
        m_index_buffer_handle  = renderer->Device->CreateIndexBufferSet();
        auto& builder          = renderer->RenderGraph->RenderPassBuilder;
        builder->SetName("Imgui Pass")
            .SetPipelineName("Imgui-Pipeline")
            .EnablePipelineBlending(true)
            .SetInputBindingCount(1)
            .SetStride(0, sizeof(ImDrawVert))
            .SetRate(0, VK_VERTEX_INPUT_RATE_VERTEX)

            .SetInputAttributeCount(3)
            .SetLocation(0, 0)
            .SetBinding(0, 0)
            .SetFormat(0, Specifications::ImageFormat::R32G32_SFLOAT)
            .SetOffset(0, IM_OFFSETOF(ImDrawVert, pos))
            .SetLocation(1, 1)
            .SetBinding(1, 0)
            .SetFormat(1, Specifications::ImageFormat::R32G32_SFLOAT)
            .SetOffset(1, IM_OFFSETOF(ImDrawVert, uv))
            .SetLocation(2, 2)
            .SetBinding(2, 0)
            .SetFormat(2, Specifications::ImageFormat::R8G8B8A8_UNORM)
            .SetOffset(2, IM_OFFSETOF(ImDrawVert, col))

            .UseShader("imgui")
            .SetShaderOverloadMaxSet(2000)

            .UseSwapchainAsRenderTarget();

        m_ui_pass = renderer->CreateRenderPass(builder->Detach());
        m_ui_pass->SetBindlessInput("TextureArray");
        m_ui_pass->Verify();
        m_ui_pass->Bake();
        /*
         * Font uploading
         */
        unsigned char* pixels;
        int            width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        size_t                               upload_size        = width * height * 4 * sizeof(char);

        Specifications::TextureSpecification font_tex_spec      = {};
        font_tex_spec.Width                                     = width;
        font_tex_spec.Height                                    = height;
        font_tex_spec.Data                                      = pixels;
        font_tex_spec.Format                                    = Specifications::ImageFormat::R8G8B8A8_UNORM;
        auto font_texture                                       = renderer->CreateTexture(font_tex_spec);
        auto font_tex_handle                                    = renderer->Device->GlobalTextures->Add(font_texture);

        io.Fonts->TexID                                         = (ImTextureID) font_tex_handle.Index;

        auto                              font_image_info       = font_texture->ImageBuffer->GetDescriptorImageInfo();
        uint32_t                          frame_count           = renderer->Device->SwapchainImageCount;
        auto                              shader                = m_ui_pass->Pipeline->GetShader();
        auto                              descriptor_set_map    = shader->GetDescriptorSetMap();
        std::vector<VkWriteDescriptorSet> write_descriptor_sets = {};

        for (unsigned i = 0; i < frame_count; ++i)
        {
            auto set = descriptor_set_map.at(0)[i];
            write_descriptor_sets.push_back(VkWriteDescriptorSet{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext = nullptr, .dstSet = set, .dstBinding = 0, .dstArrayElement = (uint32_t) font_tex_handle.Index, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &(font_image_info), .pBufferInfo = nullptr, .pTexelBufferView = nullptr});
        }

        vkUpdateDescriptorSets(renderer->Device->LogicalDevice, write_descriptor_sets.size(), write_descriptor_sets.data(), 0, nullptr);
    }

    void ImGUIRenderer::Deinitialize()
    {
        m_ui_pass->Dispose();

        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void ImGUIRenderer::StyleDarkTheme()
    {
        auto& colors                        = ImGui::GetStyle().Colors;
        colors[ImGuiCol_WindowBg]           = ImVec4{0.1f, 0.105f, 0.11f, 1.0f};

        // Headers
        colors[ImGuiCol_Header]             = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
        colors[ImGuiCol_HeaderHovered]      = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
        colors[ImGuiCol_HeaderActive]       = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

        // Buttons
        colors[ImGuiCol_Button]             = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
        colors[ImGuiCol_ButtonHovered]      = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
        colors[ImGuiCol_ButtonActive]       = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

        // Frame BG
        colors[ImGuiCol_FrameBg]            = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
        colors[ImGuiCol_FrameBgHovered]     = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
        colors[ImGuiCol_FrameBgActive]      = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

        // Tabs
        colors[ImGuiCol_Tab]                = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
        colors[ImGuiCol_TabHovered]         = ImVec4{0.38f, 0.3805f, 0.381f, 1.0f};
        colors[ImGuiCol_TabActive]          = ImVec4{0.28f, 0.2805f, 0.281f, 1.0f};
        colors[ImGuiCol_TabUnfocused]       = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};

        // Title
        colors[ImGuiCol_TitleBg]            = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
        colors[ImGuiCol_TitleBgActive]      = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
        colors[ImGuiCol_TitleBgCollapsed]   = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

        colors[ImGuiCol_DockingPreview]     = ImVec4{0.2f, 0.205f, 0.21f, .5f};
        colors[ImGuiCol_SeparatorHovered]   = ImVec4{1.f, 1.f, 1.0f, .5f};
        colors[ImGuiCol_SeparatorActive]    = ImVec4{1.f, 1.f, 1.0f, .5f};
        colors[ImGuiCol_CheckMark]          = ImVec4{1.0f, 1.f, 1.0f, 1.f};

        colors[ImGuiCol_PlotHistogram]      = ImVec4{1.0f, 1.f, 1.0f, 1.f};
    }

    void ImGUIRenderer::NewFrame()
    {
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
    }

    void ImGUIRenderer::DrawFrame(uint32_t frame_index, Hardwares::CommandBuffer* const command_buffer)
    {
        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();

        if (!draw_data)
        {
            return;
        }
        // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
        int fb_width  = (int) (draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
        int fb_height = (int) (draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
        if (fb_width <= 0 || fb_height <= 0)
        {
            return;
        }

        int vertex_count = draw_data->TotalVtxCount;
        int index_count  = draw_data->TotalIdxCount;

        if (vertex_count == 0 && index_count == 0)
        {
            return;
        }

        std::vector<ImDrawVert> vertex_data(vertex_count);
        std::vector<ImDrawIdx>  index_data(index_count);

        ImDrawVert*             vertex_data_ptr = vertex_data.data();
        for (int n = 0; n < draw_data->CmdListsCount; ++n)
        {
            const ImDrawList* cmd_list  = draw_data->CmdLists[n];
            const size_t      data_size = cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);
            Helpers::secure_memcpy(vertex_data_ptr, data_size, cmd_list->VtxBuffer.Data, data_size);
            vertex_data_ptr += cmd_list->VtxBuffer.Size;
        }

        ImDrawIdx* index_data_ptr = index_data.data();
        for (int n = 0; n < draw_data->CmdListsCount; ++n)
        {
            const ImDrawList* cmd_list  = draw_data->CmdLists[n];
            const size_t      data_size = cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx);
            Helpers::secure_memcpy(index_data_ptr, data_size, cmd_list->IdxBuffer.Data, data_size);
            index_data_ptr += cmd_list->IdxBuffer.Size;
        }

        auto& vertex_buffer = m_renderer->Device->VertexBufferSetManager.Access(m_vertex_buffer_handle);
        auto& index_buffer  = m_renderer->Device->IndexBufferSetManager.Access(m_index_buffer_handle);

        vertex_buffer->SetData<ImDrawVert>(frame_index, vertex_data);
        index_buffer->SetData<ImDrawIdx>(frame_index, index_data);

        auto device              = m_renderer->Device;
        auto current_framebuffer = device->SwapchainFramebuffers[device->CurrentFrameIndex];

        command_buffer->BeginRenderPass(m_ui_pass, current_framebuffer);
        command_buffer->BindVertexBuffer(vertex_buffer->At(frame_index));
        command_buffer->BindIndexBuffer(index_buffer->At(frame_index), sizeof(ImDrawIdx) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);

        // Setup scale and translation:
        // Our visible imgui space lies from draw_data->DisplayPps (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single
        // viewport apps.

        PushConstantData pc_data = {};
        pc_data.Scale[0]         = 2.0f / draw_data->DisplaySize.x;
        pc_data.Scale[1]         = 2.0f / draw_data->DisplaySize.y;
        pc_data.Translate[0]     = -1.0f - draw_data->DisplayPos.x * pc_data.Scale[0];
        pc_data.Translate[1]     = -1.0f - draw_data->DisplayPos.y * pc_data.Scale[1];

        // Will project scissor/clipping rectangles into framebuffer space
        ImVec2 clip_off          = draw_data->DisplayPos;       // (0,0) unless using multi-viewports
        ImVec2 clip_scale        = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

        // Render command lists
        // (Because we merged all buffers into a single one, we maintain our own offset into them)
        int    global_vtx_offset = 0;
        int    global_idx_offset = 0;
        for (int n = 0; n < draw_data->CmdListsCount; n++)
        {
            const ImDrawList* cmd_list = draw_data->CmdLists[n];
            for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
            {
                const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
                if (pcmd->UserCallback != nullptr)
                {
                    pcmd->UserCallback(cmd_list, pcmd);
                }
                else
                {
                    // Project scissor/clipping rectangles into framebuffer space
                    ImVec4 clip_rect;
                    clip_rect.x = std::max(0.f, (pcmd->ClipRect.x - clip_off.x) * clip_scale.x);
                    clip_rect.y = std::max(0.f, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                    clip_rect.z = std::max(0.f, (pcmd->ClipRect.z - clip_off.x) * clip_scale.x);
                    clip_rect.w = std::max(0.f, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

                    if (clip_rect.x >= 0 && clip_rect.x < fb_width && clip_rect.y >= 0 && clip_rect.y < fb_height && clip_rect.z >= 0.0f && clip_rect.w >= 0.0f)
                    {
                        // Apply scissor/clipping rectangle
                        VkRect2D scissor;
                        scissor.offset.x      = (int32_t) (clip_rect.x);
                        scissor.offset.y      = (int32_t) (clip_rect.y);
                        scissor.extent.width  = (uint32_t) (clip_rect.z - clip_rect.x);
                        scissor.extent.height = (uint32_t) (clip_rect.w - clip_rect.y);
                        command_buffer->SetScissor(scissor);

                        pc_data.TextureId = (uint32_t) (intptr_t) pcmd->TextureId;
                        command_buffer->PushConstants(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantData), &pc_data);
                        command_buffer->BindDescriptorSets(frame_index);
                        command_buffer->DrawIndexed(pcmd->ElemCount, 1, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset, 0);
                    }
                }
            }
            global_idx_offset += cmd_list->IdxBuffer.Size;
            global_vtx_offset += cmd_list->VtxBuffer.Size;
        }
        command_buffer->EndRenderPass();

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }
} // namespace ZEngine::Rendering::Renderers