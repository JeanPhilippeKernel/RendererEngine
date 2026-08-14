#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/VFS/IVFSFile.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/ImGUIRenderer.h>
#include <ZEngine/Windows/CoreWindow.h>
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

        ImGuiIO& io                      = ImGui::GetIO();
        io.ConfigViewportsNoTaskBarIcon  = true;
        io.ConfigViewportsNoDecoration   = true;
        io.BackendFlags                 |= ImGuiBackendFlags_RendererHasViewports;
        io.BackendFlags                 |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendFlags                 |= ImGuiBackendFlags_HasMouseHoveredViewport;
        io.BackendRendererName           = "ZEngine-Imgui";

        {
            auto* ctx          = Engine::GetContext();
            auto  ini_vfs_path = Core::VFS::VFSPath::Parse("/ZodiacEngine/Settings/DefaultLayout.ini");
            if (ini_vfs_path.Succeeded())
            {
                auto exists = ctx->VFS->Exists(ini_vfs_path.Value());
                if (exists.Succeeded() && exists.Value())
                {
                    static char s_ini_path[MAX_FILE_PATH_COUNT];
                    fmt::format_to_n(s_ini_path, sizeof(s_ini_path) - 1, "{}/Settings/DefaultLayout.ini", ctx->EngineAssetsBackend.NativeRoot());
                    io.IniFilename = s_ini_path;
                }
            }
        }

        io.ConfigFlags         |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags         |= ImGuiConfigFlags_DockingEnable;
        // io.ConfigFlags         |= ImGuiConfigFlags_ViewportsEnable;

        auto& style             = ImGui::GetStyle();
        style.WindowBorderSize  = 0.f;
        style.ChildBorderSize   = 0.f;
        style.FrameRounding     = 7.0f;

        {
            auto* vfs      = Engine::GetContext()->VFS;
            auto  font_res = Core::VFS::VFSPath::Parse("/ZodiacEngine/Settings/Fonts/OpenSans/OpenSans-Regular.ttf");
            if (font_res.Succeeded())
            {
                auto file_res = vfs->Open(font_res.Value(), Core::VFS::VFSOpenFlags::Read);
                if (file_res.Succeeded())
                {
                    auto* f        = file_res.Value();
                    auto  size_res = f->Size();
                    if (size_res.Succeeded())
                    {
                        const uint64_t                       sz   = size_res.Value();
                        void*                                data = IM_ALLOC(static_cast<size_t>(sz));
                        Core::Containers::ArrayView<uint8_t> view{static_cast<uint8_t*>(data), sz};
                        f->ReadAll(view);
                        io.FontDefault = io.Fonts->AddFontFromMemoryTTF(data, static_cast<int>(sz), 17.f);
                    }
                    vfs->Close(f);
                }
            }
        }

        auto current_window = Device->CurrentWindow->GetNativeWindow();

        ImGui_ImplGlfw_InitForVulkan(reinterpret_cast<GLFWwindow*>(current_window), false);

        // HOST_VISIBLE — one buffer per frame-in-flight to avoid write-after-read hazard.
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i)
        {
            VBHandles[i]   = Device->GpuMem.AllocateBuffer(ZMega(5), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, "ImguiVertexBuffer");
            IdxBHandles[i] = Device->GpuMem.AllocateBuffer(ZMega(5), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, "ImguiIndexBuffer");
        }

        /*
         * Font uploading — RRM creates the texture and copies pixel data into an
         * owned buffer. The GPU upload is deferred to the first BeginFrame call.
         */
        unsigned char* pixels;
        int            width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        if (Device->RRM)
        {
            auto* rrm   = static_cast<RenderResourceManager*>(Device->RRM);
            FontTexture = rrm->UploadFontAtlas(pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        }

        Device->TextureHandleToUpdates.Enqueue(FontTexture);
        io.Fonts->TexID   = (ImTextureID) FontTexture.Index;

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
        UIPass->SetInput("LinearWrapSampler", Device->GlobalLinearWrapSamplerImageInfo);
        UIPass->SetInput("LinearClampSampler", Device->GlobalLinearClampToEdgeSamplerImageInfo);
        UIPass->Verify();
        UIPass->Bake();
    }

    void ImGUIRenderer::Deinitialize()
    {
        UIPass->Dispose();

        if (FontTexture.Valid())
            Device->TextureHandleToDispose.Enqueue(FontTexture);

        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i)
        {
            if (VBHandles[i])
                Device->GpuMem.FreeBuffer(VBHandles[i]);
            if (IdxBHandles[i])
                Device->GpuMem.FreeBuffer(IdxBHandles[i]);
        }

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
        // VBHandle/IdxBHandle are set in RenderOverlay after BeginFrame sets CurrentFrame.

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
