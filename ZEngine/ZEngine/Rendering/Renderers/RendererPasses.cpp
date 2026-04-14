#include <GraphicRenderer.h>
#include <RendererPasses.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Rendering::Renderers
{
    void UploadPass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        WriteOnceControl.init(device->Arena, device->SwapchainPtr->BufferredFrameCount, device->SwapchainPtr->BufferredFrameCount);

        SkyboxVertexData.init(device->Arena, 24, make_initializer_list(device->Arena, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f));
        SkyboxIndexData.init(device->Arena, 36, make_initializer_list<uint16_t>(device->Arena, 0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 5, 4, 7, 7, 6, 5, 4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4));
        GridVertexData.init(device->Arena, 12, make_initializer_list<float>(device->Arena, -1.0f, 0.0f, -1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f));
        GridIndexData.init(device->Arena, 6, make_initializer_list<uint16_t>(device->Arena, 0, 1, 2, 2, 3, 0));

        const auto& skybox_res_vb_info = res_builder->CreateBufferSet("SkyboxVbSet", BufferSetCreationType::VERTEX);
        const auto& skybox_res_ib_info = res_builder->CreateBufferSet("SkyboxIbSet", BufferSetCreationType::INDEX);
        const auto& grid_res_vb_info   = res_builder->CreateBufferSet("GridVbSet", BufferSetCreationType::VERTEX);
        const auto& grid_res_ib_info   = res_builder->CreateBufferSet("GridIbSet", BufferSetCreationType::INDEX);

        SkyboxVBHandle                 = skybox_res_vb_info.ResourceInfo.VertexBufferSetHandle;
        SkyboxIBHandle                 = skybox_res_ib_info.ResourceInfo.IndexBufferSetHandle;
        GridVBHandle                   = grid_res_vb_info.ResourceInfo.VertexBufferSetHandle;
        GridIBHandle                   = grid_res_ib_info.ResourceInfo.IndexBufferSetHandle;

        auto count                     = device->SwapchainPtr->BufferredFrameCount;

        auto skybox_vb_buffer_set      = device->VertexBufferSetManager.Access(SkyboxVBHandle);
        auto skybox_ib_buffer_set      = device->IndexBufferSetManager.Access(SkyboxIBHandle);
        auto grid_vb_buffer_set        = device->VertexBufferSetManager.Access(GridVBHandle);
        auto grid_ib_buffer_set        = device->IndexBufferSetManager.Access(GridIBHandle);

        for (int i = 0; i < count; ++i)
        {
            auto skybox_vb_view = ArrayView{SkyboxVertexData};
            auto skybox_ib_view = ArrayView{SkyboxIndexData};
            auto grid_vb_view   = ArrayView{GridVertexData};
            auto grid_ib_view   = ArrayView{GridIndexData};

            skybox_vb_buffer_set->At(i)->Allocate(skybox_vb_view.size_bytes(), "SkyboxVb");
            skybox_ib_buffer_set->At(i)->Allocate(skybox_ib_view.size_bytes(), "SkyboxIb");
            grid_vb_buffer_set->At(i)->Allocate(grid_vb_view.size_bytes(), "GridVb");
            grid_ib_buffer_set->At(i)->Allocate(grid_ib_view.size_bytes(), "GridIb");
        }

        RenderGraphRenderPassCreation pass_node = {.Name = name};

        pass_node.Inputs.init(device->Arena, 1);
        pass_node.Outputs.init(device->Arena, 2);
        pass_node.Outputs.push(RenderGraphRenderPassInputOutputInfo{.Name = RendererResourceName::FrameDepthRenderTargetName});
        pass_node.Outputs.push(RenderGraphRenderPassInputOutputInfo{.Name = RendererResourceName::FrameColorRenderTargetName});

        res_builder->CreateRenderPassNode(pass_node);
    }

    void UploadPass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        // this compile action is purely fake, as this pass is only used to upload data to the buffers,
        // and doesn't actually need a render pass. However, we need to create a dummy render pass to be able to execute this pass in the render graph.
        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("Initial-Pipeline")
                                 .SetInputBindingCount(1)
                                 .SetStride(0, sizeof(float) * 3)
                                 .SetRate(0, VK_VERTEX_INPUT_RATE_VERTEX)
                                 .SetInputAttributeCount(1)
                                 .SetLocation(0, 0)

                                 .SetBinding(0, 0)
                                 .SetFormat(0, Specifications::ImageFormat::R32G32B32_SFLOAT)
                                 .SetOffset(0, 0)

                                 .EnablePipelineDepthTest(true)
                                 .UseShader("initial")
                                 .Detach();
            // clang-format off
            *output_pass = device->CreateRenderPass(pass_spec);
            // clang-format on
            (*output_pass)->Bake();
        }
    }

    void UploadPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        auto index = device->SwapchainPtr->CurrentFrame->Index;
        if (WriteOnceControl[device->SwapchainPtr->CurrentFrame->Index] != 0)
        {
            return;
        }

        auto skybox_vb_buffer_set = device->VertexBufferSetManager.Access(SkyboxVBHandle);
        auto skybox_ib_buffer_set = device->IndexBufferSetManager.Access(SkyboxIBHandle);
        auto grid_vb_buffer_set   = device->VertexBufferSetManager.Access(GridVBHandle);
        auto grid_ib_buffer_set   = device->IndexBufferSetManager.Access(GridIBHandle);

        skybox_vb_buffer_set->At(index)->Write(index, 0, ArrayView{SkyboxVertexData});
        skybox_ib_buffer_set->At(index)->Write(index, 0, ArrayView{SkyboxIndexData});
        grid_vb_buffer_set->At(index)->Write(index, 0, ArrayView{GridVertexData});
        grid_ib_buffer_set->At(index)->Write(index, 0, ArrayView{GridIndexData});

        WriteOnceControl[device->SwapchainPtr->CurrentFrame->Index] = 1;
    }

    void InitialPass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        // VertexData.init(device->Arena, 3, make_initializer_list(device->Arena, 0.0f, 0.0f, 0.0f));

        // auto& vb_res    = res_builder->CreateBufferSet("initial_vertex_buffer", BufferSetCreationType::VERTEX);
        // VBHandle        = vb_res.ResourceInfo.VertexBufferSetHandle;

        // auto vb_view    = ArrayView{VertexData};

        // auto buffer_set = device->VertexBufferSetManager.Access(VBHandle);
        // for (unsigned i = 0; i < device->SwapchainPtr->BufferredFrameCount; ++i)
        //{
        //     auto buffer = buffer_set->At(i);
        //     buffer->Allocate(vb_view.size_bytes(), "initial_vertex_buffer");
        //     buffer->Write(i, 0, vb_view);
        // }

        RenderGraphRenderPassCreation pass_node = {.Name = name};

        pass_node.Inputs.init(device->Arena, 2);
        pass_node.Outputs.init(device->Arena, 1);
        pass_node.Inputs.push(RenderGraphRenderPassInputOutputInfo{.Name = RendererResourceName::FrameDepthRenderTargetName});
        pass_node.Inputs.push(RenderGraphRenderPassInputOutputInfo{.Name = RendererResourceName::FrameColorRenderTargetName});

        res_builder->CreateRenderPassNode(pass_node);
    }

    void InitialPass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("Initial-Pipeline")
                                 .SetInputBindingCount(1)
                                 .SetStride(0, sizeof(float) * 3)
                                 .SetRate(0, VK_VERTEX_INPUT_RATE_VERTEX)
                                 .SetInputAttributeCount(1)
                                 .SetLocation(0, 0)

                                 .SetBinding(0, 0)
                                 .SetFormat(0, Specifications::ImageFormat::R32G32B32_SFLOAT)
                                 .SetOffset(0, 0)

                                 .EnablePipelineDepthTest(true)
                                 .UseShader("initial")
                                 .Detach();
            // clang-format off
            *output_pass = device->CreateRenderPass(pass_spec);
            // clang-format on
            (*output_pass)->Bake();
        }
    }

    void InitialPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        auto buffer_set    = device->VertexBufferSetManager.Access(VBHandle);
        auto vertex_buffer = buffer_set->At(device->SwapchainPtr->CurrentFrame->Index);

        command_buffer->BeginRenderPass(pass, framebuffer->Handle, false);
        {
            uint32_t w = pass->GetRenderAreaWidth();
            uint32_t h = pass->GetRenderAreaHeight();
            command_buffer->SetViewport(w, h);
            command_buffer->SetScissor(w, h);
        }
        command_buffer->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, pass->Pipeline);
        command_buffer->BindVertexBuffer(*vertex_buffer);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
        command_buffer->Draw(1, 1, 0, 0);
        command_buffer->EndRenderPass();
    }

    void DepthPrePass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        RenderGraphRenderPassCreation pass_node = {.Name = name};

        pass_node.Inputs.init(device->Arena, 1);
        pass_node.Inputs.push(RenderGraphRenderPassInputOutputInfo{.Name = RendererResourceName::FrameDepthRenderTargetName});

        res_builder->CreateRenderPassNode(pass_node);
    }

    void DepthPrePass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        CHECK_AND_ESCAPE_NULL(output_pass)

        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("Depth-Prepass-Pipeline")
                                 .EnablePipelineDepthTest(true)

                                 .UseShader("depth_prepass_scene")
                                 .Detach();
            // clang-format off
            *output_pass = device->CreateRenderPass(pass_spec);
            // clang-format on
            (*output_pass)->Bake();
        }

        if (scene)
        {
            (*output_pass)->SetInput("UBCamera", scene->SceneCameraBufferHandle);

            (*output_pass)->SetInput("VertexSB", scene->VertexBufferHandle);
            (*output_pass)->SetInput("IndexSB", scene->IndexBufferHandle);
            (*output_pass)->SetInput("DrawDataSB", scene->RenderDataBufferHandle);
            (*output_pass)->SetInput("TransformSB", scene->TransformBufferHandle);
            (*output_pass)->Verify();
        }
    }

    void DepthPrePass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!scene || !scene->IndirectBufferHandle)
        {
            return;
        }

        auto indirect_buffer = device->IndirectBufferSetManager.Access(scene->IndirectBufferHandle);
        command_buffer->BeginRenderPass(pass, framebuffer->Handle, false);
        {
            uint32_t w = pass->GetRenderAreaWidth();
            uint32_t h = pass->GetRenderAreaHeight();
            command_buffer->SetViewport(w, h);
            command_buffer->SetScissor(w, h);
        }
        command_buffer->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, pass->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
        command_buffer->DrawIndirect(*indirect_buffer->At(device->SwapchainPtr->CurrentFrame->Index));
        command_buffer->EndRenderPass();
    }

    void SkyboxPass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        auto env_map_res                            = res_builder->CreateTexture("skybox_env_map", "Settings/EnvironmentMaps/bergen_4k.hdr");

        m_env_map                                   = env_map_res.ResourceInfo.TextureHandle;
        // m_vb_handle         = device->CreateVertexBufferSet();
        // m_ib_handle         = device->CreateIndexBufferSet();

        // auto count          = device->SwapchainPtr->BufferredFrameCount;
        // auto vtx_buffer_set = device->VertexBufferSetManager.Access(m_vb_handle);
        // auto idx_buffer_set = device->IndexBufferSetManager.Access(m_ib_handle);

        // auto vtx_buf_view   = ArrayView{m_vertex_data};
        // auto idx_buf_view   = ArrayView{m_index_data};

        // for (int i = 0; i < count; ++i)
        //{
        //     auto vertex_buffer = vtx_buffer_set->At(i);
        //     auto index_buffer  = idx_buffer_set->At(i);

        //    vertex_buffer->Allocate(vtx_buf_view.size_bytes(), "SkyboxPassVtx");
        //    index_buffer->Allocate(idx_buf_view.size_bytes(), "SkyboxPassIdx");

        //    vertex_buffer->Write(i, 0, vtx_buf_view);
        //    index_buffer->Write(i, 0, idx_buf_view);
        //}

        auto&                         output_skybox = res_builder->CreateRenderTarget("skybox_render_target", {.Width = 1280, .Height = 780, .Format = ImageFormat::R8G8B8A8_UNORM});
        RenderGraphRenderPassCreation pass_node     = {.Name = name};

        pass_node.Inputs.init(device->Arena, 2);
        pass_node.Outputs.init(device->Arena, 1);

        pass_node.Inputs.push(RenderGraphRenderPassInputOutputInfo{.Name = RendererResourceName::FrameDepthRenderTargetName});
        pass_node.Inputs.push(RenderGraphRenderPassInputOutputInfo{.Name = RendererResourceName::FrameColorRenderTargetName});
        pass_node.Outputs.push(RenderGraphRenderPassInputOutputInfo{.Name = output_skybox.Name});

        res_builder->CreateRenderPassNode(pass_node);
    }

    void SkyboxPass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        CHECK_AND_ESCAPE_NULL(output_pass)

        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("Skybox-Pipeline")
                                 .SetInputBindingCount(1)
                                 .SetStride(0, sizeof(float) * 3)
                                 .SetRate(0, VK_VERTEX_INPUT_RATE_VERTEX)
                                 .SetInputAttributeCount(1)
                                 .SetLocation(0, 0)

                                 .SetBinding(0, 0)
                                 .SetFormat(0, Specifications::ImageFormat::R32G32B32_SFLOAT)
                                 .SetOffset(0, 0)
                                 .EnablePipelineDepthTest(true)
                                 .EnablePipelineDepthWrite(false)

                                 .UseShader("skybox")
                                 .Detach();
            // clang-format off
            *output_pass = device->CreateRenderPass(pass_spec);
            // clang-format on
            (*output_pass)->Bake();
        }

        if (scene)
        {
            (*output_pass)->SetInput("UBCamera", scene->SceneCameraBufferHandle);
            (*output_pass)->SetInput("EnvMap", m_env_map);
            (*output_pass)->SetInput("LinearWrapSampler", device->GlobalLinearWrapSamplerImageInfo);
        }

        (*output_pass)->Verify();
    }

    void SkyboxPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        const auto& vb_handle     = res_inspector->GetVertexBufferSet("SkyboxVbSet");
        const auto& ib_handle     = res_inspector->GetIndexBufferSet("SkyboxIbSet");

        auto        vertex_buffer = device->VertexBufferSetManager.Access(vb_handle);
        auto        index_buffer  = device->IndexBufferSetManager.Access(ib_handle);

        command_buffer->BeginRenderPass(pass, framebuffer->Handle, false);
        {
            uint32_t w = pass->GetRenderAreaWidth();
            uint32_t h = pass->GetRenderAreaHeight();
            command_buffer->SetViewport(w, h);
            command_buffer->SetScissor(w, h);
        }
        command_buffer->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, pass->Pipeline);
        command_buffer->BindVertexBuffer(*vertex_buffer->At(device->SwapchainPtr->CurrentFrame->Index));
        command_buffer->BindIndexBuffer(*index_buffer->At(device->SwapchainPtr->CurrentFrame->Index), VK_INDEX_TYPE_UINT16);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
        command_buffer->DrawIndexed(36, 1, 0, 0, 0);
        command_buffer->EndRenderPass();
    }

    void GridPass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        auto                          arena       = device->Arena;

        // m_index_data.init(arena, 6, make_initializer_list<uint16_t>(arena, 0, 1, 2, 2, 3, 0));
        // m_vertex_data.init(arena, 12, make_initializer_list<float>(arena, -1.0f, 0.0f, -1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f));

        // m_vb_handle         = device->CreateVertexBufferSet();
        // m_ib_handle         = device->CreateIndexBufferSet();

        // auto count          = device->SwapchainPtr->BufferredFrameCount;
        // auto vtx_buffer_set = device->VertexBufferSetManager.Access(m_vb_handle);
        // auto idx_buffer_set = device->IndexBufferSetManager.Access(m_ib_handle);

        // auto vtx_buf_view   = ArrayView{m_vertex_data};
        // auto idx_buf_view   = ArrayView{m_index_data};

        // for (int i = 0; i < count; ++i)
        //{
        //     auto vertex_buffer = vtx_buffer_set->At(i);
        //     auto index_buffer  = idx_buffer_set->At(i);

        //    vertex_buffer->Allocate(vtx_buf_view.size_bytes(), "GridPassVtx");
        //    index_buffer->Allocate(idx_buf_view.size_bytes(), "GridPassIdx");

        //    vertex_buffer->Write(i, 0, vtx_buf_view);
        //    index_buffer->Write(i, 0, idx_buf_view);
        //}

        auto&                         output_grid = res_builder->CreateRenderTarget("grid_render_target", {.Width = 1280, .Height = 780, .Format = ImageFormat::R8G8B8A8_UNORM});
        RenderGraphRenderPassCreation pass_node   = {.Name = name};

        pass_node.Inputs.init(arena, 2);
        pass_node.Outputs.init(arena, 1);

        pass_node.Inputs.push(RenderGraphRenderPassInputOutputInfo{.Name = RendererResourceName::FrameDepthRenderTargetName});
        pass_node.Inputs.push(RenderGraphRenderPassInputOutputInfo{.Name = RendererResourceName::FrameColorRenderTargetName});
        pass_node.Outputs.push(RenderGraphRenderPassInputOutputInfo{.Name = output_grid.Name});

        res_builder->CreateRenderPassNode(pass_node);
    }

    void GridPass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        CHECK_AND_ESCAPE_NULL(output_pass)

        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("Infinite-Grid-Pipeline")
                                 .SetInputBindingCount(1)
                                 .SetStride(0, sizeof(float) * 3)
                                 .SetRate(0, VK_VERTEX_INPUT_RATE_VERTEX)
                                 .SetInputAttributeCount(1)
                                 .SetLocation(0, 0)

                                 .SetBinding(0, 0)
                                 .SetFormat(0, Specifications::ImageFormat::R32G32B32_SFLOAT)
                                 .SetOffset(0, 0)

                                 .EnablePipelineDepthTest(true)
                                 .UseShader("infinite_grid")
                                 .Detach();
            // clang-format off
            *output_pass = device->CreateRenderPass(pass_spec);
            // clang-format on
            (*output_pass)->Bake();
        }

        if (scene)
        {
            (*output_pass)->SetInput("UBCamera", scene->SceneCameraBufferHandle);
            (*output_pass)->Verify();
        }
    }

    void GridPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {

        auto vb_handle     = res_inspector->GetVertexBufferSet("GridVbSet");
        auto id_handle     = res_inspector->GetIndexBufferSet("GridIbSet");

        auto vb_set        = device->VertexBufferSetManager.Access(vb_handle);
        auto ib_set        = device->IndexBufferSetManager.Access(id_handle);

        auto vertex_buffer = vb_set->At(device->SwapchainPtr->CurrentFrame->Index);
        auto index_buffer  = ib_set->At(device->SwapchainPtr->CurrentFrame->Index);

        command_buffer->BeginRenderPass(pass, framebuffer->Handle, false);
        {
            uint32_t w = pass->GetRenderAreaWidth();
            uint32_t h = pass->GetRenderAreaHeight();
            command_buffer->SetViewport(w, h);
            command_buffer->SetScissor(w, h);
        }
        command_buffer->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, pass->Pipeline);
        command_buffer->BindVertexBuffer(*vertex_buffer);
        command_buffer->BindIndexBuffer(*index_buffer, VK_INDEX_TYPE_UINT16);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
        command_buffer->DrawIndexed(6, 1, 0, 0, 0);
        command_buffer->EndRenderPass();
    }

    void GbufferPass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        Specifications::TextureSpecification normal_output_spec   = {.IsUsageStorage = true, .Width = 1280, .Height = 780, .Format = ImageFormat::R16G16B16A16_SFLOAT};
        Specifications::TextureSpecification position_output_spec = {.IsUsageStorage = true, .Width = 1280, .Height = 780, .Format = ImageFormat::R16G16B16A16_SFLOAT};
        Specifications::TextureSpecification specular_output_spec = {.IsUsageStorage = true, .Width = 1280, .Height = 780, .Format = ImageFormat::R8G8B8A8_UNORM};
        Specifications::TextureSpecification colour_output_spec   = {.IsUsageStorage = true, .Width = 1280, .Height = 780, .Format = ImageFormat::R8G8B8A8_UNORM};

        auto&                                gbuffer_albedo       = res_builder->CreateRenderTarget("gbuffer_albedo_render_target", colour_output_spec);
        auto&                                gbuffer_specular     = res_builder->CreateRenderTarget("gbuffer_specular_render_target", specular_output_spec);
        auto&                                gbuffer_normals      = res_builder->CreateRenderTarget("gbuffer_normals_render_target", normal_output_spec);
        auto&                                gbuffer_position     = res_builder->CreateRenderTarget("gbuffer_position_render_target", position_output_spec);

        RenderGraphRenderPassCreation        pass_node            = {.Name = name};

        pass_node.Inputs.init(device->Arena, 2);
        pass_node.Outputs.init(device->Arena, 4);

        pass_node.Inputs.push(RenderGraphRenderPassInputOutputInfo{.Name = RendererResourceName::FrameDepthRenderTargetName});
        pass_node.Inputs.push(RenderGraphRenderPassInputOutputInfo{.Name = RendererResourceName::FrameColorRenderTargetName});

        pass_node.Outputs.push({.Name = gbuffer_albedo.Name});
        pass_node.Outputs.push({.Name = gbuffer_specular.Name});
        pass_node.Outputs.push({.Name = gbuffer_normals.Name});
        pass_node.Outputs.push({.Name = gbuffer_position.Name});
        res_builder->CreateRenderPassNode(pass_node);
    }

    void GbufferPass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        CHECK_AND_ESCAPE_NULL(output_pass)

        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("GBuffer-Pipeline").EnablePipelineDepthTest(true).UseShader("g_buffer").Detach();
            *output_pass   = device->CreateRenderPass(pass_spec);
            (*output_pass)->Bake();
        }

        if (scene)
        {
            (*output_pass)->SetInput("UBCamera", scene->SceneCameraBufferHandle);

            (*output_pass)->SetInput("VertexSB", scene->VertexBufferHandle);
            (*output_pass)->SetInput("IndexSB", scene->IndexBufferHandle);
            (*output_pass)->SetInput("DrawDataSB", scene->RenderDataBufferHandle);
            (*output_pass)->SetInput("TransformSB", scene->TransformBufferHandle);
            (*output_pass)->SetInput("MatSB", scene->MaterialBufferHandle);

            (*output_pass)->SetBindlessInput("TextureArray");
            (*output_pass)->Verify();
        }
    }

    void GbufferPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        CHECK_AND_ESCAPE_NULL(scene)
        CHECK_AND_ESCAPE_NULL(scene->IndirectBufferHandle)

        auto indirect_buffer = device->IndirectBufferSetManager.Access(scene->IndirectBufferHandle);
        command_buffer->BeginRenderPass(pass, framebuffer->Handle, false);
        {
            uint32_t w = pass->GetRenderAreaWidth();
            uint32_t h = pass->GetRenderAreaHeight();
            command_buffer->SetViewport(w, h);
            command_buffer->SetScissor(w, h);
        }
        command_buffer->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, pass->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
        command_buffer->DrawIndirect(*indirect_buffer->At(device->SwapchainPtr->CurrentFrame->Index));
        command_buffer->EndRenderPass();
    }

    void LightingPass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        // auto&                                builder              = graph->Builder;
        // auto&                                renderer             = graph->Renderer;

        // Specifications::TextureSpecification lighting_output_spec = {.Width = 1280, .Height = 780, .Format = ImageFormat::R8G8B8A8_UNORM};
        // auto&                                lighting_output      = builder->CreateRenderTarget("lighting_render_target", lighting_output_spec);
        // RenderGraphRenderPassCreation        pass_node            = {.Name = name.data()};

        // pass_node.Inputs.init(graph->Renderer->Device->Arena, 5);
        // pass_node.Outputs.init(graph->Renderer->Device->Arena, 1);

        // pass_node.Inputs.push(RenderGraphRenderPassInputOutputInfo{.Name = renderer->FrameDepthRenderTargetName});
        // pass_node.Inputs.push({.Name = "gbuffer_albedo_render_target", .BindingInputKeyName = "AlbedoSampler", .Type = RenderGraphResourceType::TEXTURE});
        // pass_node.Inputs.push({.Name = "gbuffer_position_render_target", .BindingInputKeyName = "PositionSampler", .Type = RenderGraphResourceType::TEXTURE});
        // pass_node.Inputs.push({.Name = "gbuffer_normals_render_target", .BindingInputKeyName = "NormalSampler", .Type = RenderGraphResourceType::TEXTURE});
        // pass_node.Inputs.push({.Name = "gbuffer_specular_render_target", .BindingInputKeyName = "SpecularSampler", .Type = RenderGraphResourceType::TEXTURE});
        // pass_node.Outputs.push(RenderGraphRenderPassInputOutputInfo{.Name = lighting_output.Name});

        // builder->CreateRenderPassNode(pass_node);
    }

    void LightingPass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        // if (!pass)
        //{
        //     return;
        // }

        // auto& builder  = graph->RenderPassBuilder;
        // auto& renderer = graph->Renderer;

        // if (pass && !(*pass))
        //{
        //     auto pass_spec = builder->SetPipelineName("Deferred-lighting-Pipeline").EnablePipelineDepthTest(true).UseShader("deferred_lighting").Detach();

        //    *pass          = renderer->CreateRenderPass(pass_spec);
        //    (*pass)->Bake();
        //}

        //(*pass)->SetInput("UBCamera", renderer->SceneCameraBufferHandle);
        //(*pass)->SetInput("VertexSB", scene->VertexBufferHandle);
        //(*pass)->SetInput("IndexSB", scene->IndexBufferHandle);
        //(*pass)->SetInput("DrawDataSB", scene->IndirectDataDrawBufferHandle);
        //(*pass)->SetInput("TransformSB", scene->TransformBufferHandle);
        //(*pass)->SetInput("MatSB", scene->MaterialBufferHandle);

        // auto directional_light_buffer = graph->GetStorageBufferSet("g_scene_directional_light_buffer");
        // auto point_light_buffer       = graph->GetStorageBufferSet("g_scene_point_light_buffer");
        // auto spot_light_buffer        = graph->GetStorageBufferSet("g_scene_spot_light_buffer");

        //(*pass)->SetInput("DirectionalLightSB", directional_light_buffer);
        //(*pass)->SetInput("PointLightSB", point_light_buffer);
        //(*pass)->SetInput("SpotLightSB", spot_light_buffer);

        //(*pass)->Verify();
    }

    void LightingPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        // auto directional_light_buffer_handle = graph->GetStorageBufferSet("g_scene_directional_light_buffer");
        // auto point_light_buffer_handle       = graph->GetStorageBufferSet("g_scene_point_light_buffer");
        // auto spot_light_buffer_handle        = graph->GetStorageBufferSet("g_scene_spot_light_buffer");
        ///*
        // * Composing Light Data
        // */
        // auto directional_light_buffer        = graph->Renderer->Device->StorageBufferSetManager.Access(directional_light_buffer_handle);
        // auto point_light_buffer              = graph->Renderer->Device->StorageBufferSetManager.Access(point_light_buffer_handle);
        // auto spot_light_buffer               = graph->Renderer->Device->StorageBufferSetManager.Access(spot_light_buffer_handle);

        // auto dir_light_data                  = Lights::CreateLightBuffer<Lights::GpuDirectionLight>(scene_data->DirectionalLights);
        // auto point_light_data                = Lights::CreateLightBuffer<Lights::GpuPointLight>(scene_data->PointLights);
        // auto spot_light_data                 = Lights::CreateLightBuffer<Lights::GpuSpotlight>(scene_data->SpotLights);

        // directional_light_buffer->SetData<uint8_t>(frame_index, dir_light_data);
        // point_light_buffer->SetData<uint8_t>(frame_index, point_light_data);
        // spot_light_buffer->SetData<uint8_t>(frame_index, spot_light_data);

        // if (!scene->IndirectBufferHandle)
        //{
        //     return;
        // }

        // auto renderer        = graph->Renderer;
        // auto indirect_buffer = renderer->Device->IndirectBufferSetManager.Access(scene->IndirectBufferHandle);

        // command_buffer->BeginRenderPass(pass, framebuffer->Handle);
        // command_buffer->BindDescriptorSets(scene->FrameIndex);
        // command_buffer->DrawIndirect(*indirect_buffer->At(scene->FrameIndex));
        // command_buffer->EndRenderPass();
    }
} // namespace ZEngine::Rendering::Renderers
