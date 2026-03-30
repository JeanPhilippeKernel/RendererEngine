#include <Hardwares/VulkanDevice.h>
#include <Rendering/Renderers/ImGUIRenderer.h>
#include <Windows/CoreWindow.h>
// clang-format off
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <ImGuizmo/ImGuizmo.h>
// clang-format on

using namespace ZEngine::Hardwares;
using namespace ZEngine::Rendering;
using namespace ZEngine::Rendering::Textures;
using namespace ZEngine::Helpers;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Rendering::Renderers
{
    void ImGUIRenderer::Initialize(Hardwares::VulkanDevicePtr device)
    {
        Device      = device;
        RenderGraph = ZPushStructCtorArgs(Device->Arena, Renderers::RenderGraph);

        RenderGraph->Initialize(Device);

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

        io.ConfigFlags         |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags         |= ImGuiConfigFlags_DockingEnable;
        // io.ConfigFlags         |= ImGuiConfigFlags_ViewportsEnable;

        auto& style             = ImGui::GetStyle();
        style.WindowBorderSize  = 0.f;
        style.ChildBorderSize   = 0.f;
        style.FrameRounding     = 7.0f;

        io.FontDefault          = io.Fonts->AddFontFromFileTTF("Settings/Fonts/OpenSans/OpenSans-Regular.ttf", 17.f);

        auto current_window     = Device->CurrentWindow->GetNativeWindow();

        ImGui_ImplGlfw_InitForVulkan(reinterpret_cast<GLFWwindow*>(current_window), false);

        VBHandle            = Device->CreateVertexBufferSet();
        IdxBHandle          = Device->CreateIndexBufferSet();

        auto vb_buffer_set  = Device->VertexBufferSetManager.Access(VBHandle);
        auto idx_buffer_set = Device->IndexBufferSetManager.Access(IdxBHandle);
        for (unsigned i = 0; i < Device->SwapchainPtr->BufferredFrameCount; ++i)
        {
            vb_buffer_set->At(i)->Allocate(ZMega(5), "ImguiVertexBuffer");
            idx_buffer_set->At(i)->Allocate(ZMega(5), "ImguiIndexBuffer");
        }

        /*
         * Font uploading
         */
        AsyncResourceLoader::DeferralUpload deferral = {};
        deferral.UploadType                          = AsyncResourceLoader::UploadType::TEXTURE_BUFFER;

        unsigned char* pixels;
        int            width, height;
        io.Fonts->GetTexDataAsRGBA32(&(deferral.Data), &width, &height);
        size_t                               upload_size   = width * height * 4 * sizeof(uint8_t);

        Specifications::TextureSpecification font_tex_spec = {};
        font_tex_spec.Width                                = width;
        font_tex_spec.Height                               = height;
        font_tex_spec.Format                               = Specifications::ImageFormat::R8G8B8A8_UNORM;

        auto font_tex_handle                               = Device->CreateTexture(font_tex_spec);
        deferral.TexHandle                                 = font_tex_handle;

        Device->AsyncResLoader->SubmitDeferral(std::move(deferral));

        // We enqueue the tex handle so, we write the DescriptorSet at Present(...)
        Device->TextureHandleToUpdates.Enqueue(font_tex_handle);

        io.Fonts->TexID   = (ImTextureID) font_tex_handle.Index;

        auto pass_builder = RenderGraph->RenderPassBuilder;
        pass_builder->SetName("Imgui Pass")
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
            // .SetShaderOverloadMaxSet(2000) // Todo : deprecated API - should be removed

            .UseSwapchainAsRenderTarget();

        UIPass = Device->CreateRenderPass(pass_builder->Detach());
        UIPass->SetBindlessInput("TextureArray");
        UIPass->SetInput("_unused", Device->GlobalLinearWrapSamplerImageInfo);
        UIPass->SetInput("LinearWrapSampler", Device->GlobalLinearWrapSamplerImageInfo);
        UIPass->Verify();
        UIPass->Bake();
    }

    void ImGUIRenderer::Deinitialize()
    {
        UIPass->Dispose();

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
    void ImGUIRenderer::EndFrame()
    {
        // The render method has EndFrame()
        ImGui::Render();
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

    void ImGUIRenderer::PreparePayload(RenderOverlayPayload& r_payload)
    {
        ImDrawData* draw_data = ImGui::GetDrawData();

        if (!draw_data)
        {
            return;
        }
        // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer
        // coordinates)
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

        r_payload.VertexCount         = vertex_count;
        r_payload.IndexCount          = index_count;
        r_payload.IsIndexBufferUint16 = sizeof(ImDrawIdx) == 2;
        r_payload.VBHandle            = VBHandle;
        r_payload.IdxBHandle          = IdxBHandle;

        r_payload.VertexData.clear();
        r_payload.IndexData.clear();
        r_payload.VertexData.shrink_to_fit();
        r_payload.IndexData.shrink_to_fit();

        r_payload.VertexData.resize(vertex_count);
        r_payload.IndexData.resize(index_count);

        UIDrawVert* vertex_data_ptr = r_payload.VertexData.data();
        for (int n = 0; n < draw_data->CmdListsCount; ++n)
        {
            const ImDrawList* cmd_list  = draw_data->CmdLists[n];
            const size_t      data_size = cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);
            Helpers::secure_memcpy(vertex_data_ptr, data_size, cmd_list->VtxBuffer.Data, data_size);
            vertex_data_ptr += cmd_list->VtxBuffer.Size;
        }

        unsigned short* index_data_ptr = r_payload.IndexData.data();
        for (int n = 0; n < draw_data->CmdListsCount; ++n)
        {
            const ImDrawList* cmd_list  = draw_data->CmdLists[n];
            const size_t      data_size = cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx);
            Helpers::secure_memcpy(index_data_ptr, data_size, cmd_list->IdxBuffer.Data, data_size);
            index_data_ptr += cmd_list->IdxBuffer.Size;
        }

        // Setup scale and translation:
        // Our visible imgui space lies from draw_data->DisplayPps (top left) to
        // draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.

        r_payload.Pc[0]          = 2.0f / draw_data->DisplaySize.x;
        r_payload.Pc[1]          = 2.0f / draw_data->DisplaySize.y;
        r_payload.Pc[2]          = -1.0f - draw_data->DisplayPos.x * r_payload.Pc[0];
        r_payload.Pc[3]          = -1.0f - draw_data->DisplayPos.y * r_payload.Pc[1];

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
                        scissor.offset.x                               = (int32_t) (clip_rect.x);
                        scissor.offset.y                               = (int32_t) (clip_rect.y);
                        scissor.extent.width                           = (uint32_t) (clip_rect.z - clip_rect.x);
                        scissor.extent.height                          = (uint32_t) (clip_rect.w - clip_rect.y);

                        r_payload.TextureIds[r_payload.DrawDataIndex]  = (uint32_t) (intptr_t) pcmd->TextureId;
                        r_payload.ScissorCmds[r_payload.DrawDataIndex] = ScissorCmd{scissor.extent.width, scissor.extent.height, scissor.offset.x, scissor.offset.y};
                        r_payload.IndexedCmds[r_payload.DrawDataIndex] = IndexedCmd{pcmd->ElemCount, 1, pcmd->IdxOffset + global_idx_offset, (int32_t) (pcmd->VtxOffset + global_vtx_offset), 0};

                        r_payload.DrawDataIndex++;
                    }
                }
            }
            global_idx_offset += cmd_list->IdxBuffer.Size;
            global_vtx_offset += cmd_list->VtxBuffer.Size;
        }
    }
} // namespace ZEngine::Rendering::Renderers
