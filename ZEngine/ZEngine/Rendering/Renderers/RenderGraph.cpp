#include <IRenderer.h>
#include <RenderGraph.h>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Helpers;

namespace ZEngine::Rendering::Renderers
{
    void RenderGraph::Initialize(Hardwares::VulkanDevicePtr device, Scenes::SceneDataPtr data)
    {
        Device            = device;
        SceneData         = data;
        ResourceBuilder   = ZPushStruct(Device->Arena, RenderGraphResourceBuilder);
        ResourceInspector = ZPushStruct(Device->Arena, RenderGraphResourceInspector);
        RenderPassBuilder = ZPushStructCtorArgs(Device->Arena, RenderPasses::RenderPassBuilder);

        SortedNodesMap.init(Device->Arena, 16);
        NodeMap.init(Device->Arena);
        ResourceMap.init(Device->Arena);
        RenderPassBuilder->Initialize(Device->Arena);

        ResourceBuilder->Initialize(this);
        ResourceInspector->Initialize(this);
    }

    void RenderGraph::AddCallbackPass(cstring pass_name, IRenderGraphCallbackPass* const pass_callback, bool enabled)
    {
        NodeMap[pass_name].Enabled      = enabled;
        NodeMap[pass_name].CallbackPass = pass_callback;
    }

    void RenderGraph::Setup()
    {
        for (auto [name, _] : NodeMap) // Todo UnorderedHashMap needs to support for (auto& [key, val]) {....}
        {
            NodeMap[name].EdgeNodes.init(Device->Arena, 5);
            NodeMap[name].CallbackPass->Setup(Device, name, ResourceBuilder, ResourceInspector);
        }
    }

    void RenderGraph::Compile()
    {
        for (auto pass : NodeMap)
        {
            for (uint32_t i = 0; i < pass.second.Creation.Inputs.size(); ++i)
            {
                if (ResourceMap.contains(pass.second.Creation.Inputs[i].Name))
                {
                    RenderGraphResource& resource = ResourceMap[pass.second.Creation.Inputs[i].Name];
                    if (NodeMap.contains(resource.ProducerNodeName))
                    {
                        RenderGraphNode& producer_node = NodeMap[resource.ProducerNodeName];
                        producer_node.EdgeNodes.push(pass.first);
                    }
                }
            }
        }

        // ToDo:  Potentially remove Node that have no Edges from the graph...?

        /*
         * Topological Sorting
         */
        auto                                scratch       = ZGetScratch(Device->Arena);

        Array<cstring>                      sorted_nodes  = {};
        UnorderedHashMap<cstring, uint32_t> visited_nodes = {};
        Array<cstring>                      stack         = {};

        sorted_nodes.init(scratch.Arena, 6);
        stack.init(scratch.Arena, 6);
        visited_nodes.init(scratch.Arena);

        for (auto node : NodeMap)
        {
            stack.push(node.first);
            while (!stack.empty())
            {
                auto& node_name = stack.back();
                if (visited_nodes[node_name] == 2)
                {
                    stack.pop();
                    continue;
                }

                if (visited_nodes[node_name] == 1)
                {
                    visited_nodes[node_name] = 2;
                    sorted_nodes.push(node_name);
                    stack.pop();
                    continue;
                }

                visited_nodes[node_name] = 1;
                auto& graph_node         = NodeMap[node_name];
                if (graph_node.EdgeNodes.empty())
                {
                    continue;
                }

                for (auto edge : graph_node.EdgeNodes)
                {
                    if (!visited_nodes.contains(edge))
                    {
                        stack.push(edge);
                    }
                }
            }
        }

        auto begin = sorted_nodes.begin();
        auto end   = std::prev(sorted_nodes.end());
        while (end >= begin)
        {
            SortedNodesMap.push(*end);
            end = std::prev(end);
        }

        ZReleaseScratch(scratch);

        /*
         * Reading sorting graph node in reverse order and Create resource and RenderPass Node
         */
        for (cstring node_name : SortedNodesMap)
        {
            auto& node = NodeMap[node_name];

            RenderPassBuilder->SetName(node.Creation.Name);

            for (auto& output : node.Creation.Outputs)
            {
                auto& resource = ResourceMap[output.Name];

                if (resource.ResourceInfo.External)
                {
                    RenderPassBuilder->UseRenderTarget(resource.ResourceInfo.TextureHandle);
                    continue;
                }

                if (output.Type == RenderGraphResourceType::ATTACHMENT)
                {
                    resource.ResourceInfo.TextureSpec.PerformTransition = false;
                    resource.ResourceInfo.TextureHandle                 = Device->CreateTexture(resource.ResourceInfo.TextureSpec);

                    RenderPassBuilder->UseRenderTarget(resource.ResourceInfo.TextureHandle);
                }
            }

            for (auto& input : node.Creation.Inputs)
            {
                auto& resource = ResourceMap[input.Name];

                if (input.Type == RenderGraphResourceType::ATTACHMENT)
                {
                    RenderPassBuilder->AddInputAttachment(resource.ResourceInfo.TextureHandle);
                }
                else if (input.Type == RenderGraphResourceType::TEXTURE)
                {
                    RenderPassBuilder->AddInputTexture(input.BindingInputKeyName, resource.ResourceInfo.TextureHandle);
                }
            }

            node.CallbackPass->Compile(Device, SceneData, RenderPassBuilder, ResourceInspector, &(node.Handle));
        }

        for (cstring name : SortedNodesMap)
        {
            auto&                                         node             = NodeMap[name];
            Specifications::FrameBufferSpecificationVNext framebuffer_spec = {.Width = node.Handle->RenderAreaWidth, .Height = node.Handle->RenderAreaHeight, .RenderTargets = node.Handle->RenderTargets, .Attachment = node.Handle->Attachment};
            node.Framebuffer                                               = ZPushStructCtorArgs(Device->Arena, Buffers::FramebufferVNext, Device, framebuffer_spec);
        }
    }

    void RenderGraph::Execute(Hardwares::CommandBufferPtr const command_buffer)
    {
        ZENGINE_VALIDATE_ASSERT(command_buffer, "Command Buffer can't be null")

        command_buffer->ClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        command_buffer->ClearDepth(1.0f, 0);

        for (auto& node_name : SortedNodesMap)
        {
            auto& node = NodeMap[node_name];

            if (!node.Enabled)
            {
                continue;
            }

            for (auto& input : node.Creation.Inputs)
            {
                if (input.Type == RenderGraphResourceType::TEXTURE)
                {
                    auto&                                           resource               = ResourceMap[input.Name];
                    /*
                     * The input texture can from an attachment that should read as Shader Sampler2D data
                     * So we need ensure the right config for transition
                     */
                    bool                                            is_resource_attachment = resource.Type == RenderGraphResourceType::ATTACHMENT;

                    auto                                            texture                = Device->GlobalTextures.Access(resource.ResourceInfo.TextureHandle);
                    auto                                            img_buf                = Device->Image2DBufferManager.Access(texture->BufferHandle);
                    auto&                                           buffer                 = img_buf->GetBuffer();

                    Specifications::ImageMemoryBarrierSpecification barrier_spec           = {};
                    barrier_spec.ImageHandle                                               = buffer.Handle;
                    barrier_spec.OldLayout                                                 = is_resource_attachment ? Specifications::ImageLayout::COLOR_ATTACHMENT_OPTIMAL : Specifications::ImageLayout::UNDEFINED;
                    barrier_spec.NewLayout                                                 = Specifications::ImageLayout::SHADER_READ_ONLY_OPTIMAL;
                    barrier_spec.ImageAspectMask                                           = VK_IMAGE_ASPECT_COLOR_BIT;
                    barrier_spec.SourceAccessMask                                          = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    barrier_spec.DestinationAccessMask                                     = VK_ACCESS_SHADER_READ_BIT;
                    barrier_spec.SourceStageMask                                           = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    barrier_spec.DestinationStageMask                                      = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    barrier_spec.LayerCount                                                = 1;

                    Primitives::ImageMemoryBarrier barrier{barrier_spec};
                    command_buffer->TransitionImageLayout(barrier);
                    img_buf->Layout = barrier_spec.NewLayout;
                }
            }

            for (auto& output : node.Creation.Outputs)
            {
                if (output.Type == RenderGraphResourceType::REFERENCE)
                {
                    continue;
                }

                auto& resource = ResourceMap[output.Name];
                ZENGINE_VALIDATE_ASSERT(resource.Type == RenderGraphResourceType::ATTACHMENT, "RenderPass Output should be an Attachment")

                auto                                            texture      = Device->GlobalTextures.Access(resource.ResourceInfo.TextureHandle);
                auto                                            img_buf      = Device->Image2DBufferManager.Access(texture->BufferHandle);
                auto&                                           buffer       = img_buf->GetBuffer();

                Specifications::ImageMemoryBarrierSpecification barrier_spec = {};
                if (texture->IsDepthTexture)
                {
                    barrier_spec.ImageHandle           = buffer.Handle;
                    barrier_spec.OldLayout             = img_buf->Layout;
                    barrier_spec.NewLayout             = Specifications::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    barrier_spec.ImageAspectMask       = VkImageAspectFlagBits(VK_IMAGE_ASPECT_DEPTH_BIT /*| VK_IMAGE_ASPECT_STENCIL_BIT*/); // Todo : To consider Stencil
                                                                                                                                             // buffer, we want to extend
                                                                                                                                             // Texture spec to introduce
                                                                                                                                             // HasStencil bit
                    barrier_spec.SourceAccessMask      = 0;
                    barrier_spec.DestinationAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                    barrier_spec.SourceStageMask       = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                    barrier_spec.DestinationStageMask  = VkPipelineStageFlagBits(VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
                    barrier_spec.LayerCount            = 1;

                    Primitives::ImageMemoryBarrier barrier{barrier_spec};
                    command_buffer->TransitionImageLayout(barrier);
                    img_buf->Layout = barrier_spec.NewLayout;
                }
                else
                {
                    barrier_spec.ImageHandle           = buffer.Handle;
                    barrier_spec.OldLayout             = img_buf->Layout;
                    barrier_spec.NewLayout             = Specifications::ImageLayout::COLOR_ATTACHMENT_OPTIMAL;
                    barrier_spec.ImageAspectMask       = VK_IMAGE_ASPECT_COLOR_BIT;
                    barrier_spec.SourceAccessMask      = 0;
                    barrier_spec.DestinationAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    barrier_spec.SourceStageMask       = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                    barrier_spec.DestinationStageMask  = VkPipelineStageFlagBits(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
                    barrier_spec.LayerCount            = 1;

                    Primitives::ImageMemoryBarrier barrier{barrier_spec};
                    command_buffer->TransitionImageLayout(barrier);
                    img_buf->Layout = barrier_spec.NewLayout;
                }
            }

            node.CallbackPass->Execute(Device, ResourceInspector, SceneData, node.Handle, node.Framebuffer, command_buffer);
        }
    }

    void RenderGraph::Resize(uint32_t width, uint32_t height)
    {
        for (auto& node_name : SortedNodesMap)
        {
            auto& node      = NodeMap[node_name];

            auto& pass_spec = node.Handle->Specification;

            pass_spec.ExternalOutputs.clear();
            pass_spec.Inputs.clear();
            pass_spec.InputTextures.clear();

            for (auto& output : node.Creation.Outputs)
            {
                auto& resource = ResourceMap[output.Name];

                if (output.Type == RenderGraphResourceType::REFERENCE)
                {
                    continue;
                }

                auto temp_handle        = Device->GlobalTextures.Create();
                auto texture_to_dispose = Device->GlobalTextures.Access(resource.ResourceInfo.TextureHandle);
                Device->GlobalTextures.Update(temp_handle, *texture_to_dispose);
                Device->TextureHandleToDispose.Enqueue(temp_handle);

                // We invalidate ResourceInfo.TextureHandle, so it can be recycle for another texture allocation
                Device->GlobalTextures.Remove(resource.ResourceInfo.TextureHandle);
                resource.ResourceInfo.TextureSpec.Width  = width;
                resource.ResourceInfo.TextureSpec.Height = height;
                resource.ResourceInfo.TextureHandle      = Device->CreateTexture(resource.ResourceInfo.TextureSpec);

                if ((output.Name == RendererResourceName::FrameColorRenderTargetName) || (output.Name == RendererResourceName::FrameDepthRenderTargetName))
                {
                    Device->TextureHandleToUpdates.Enqueue(resource.ResourceInfo.TextureHandle);
                }
                pass_spec.ExternalOutputs.push(resource.ResourceInfo.TextureHandle);
            }

            for (auto& input : node.Creation.Inputs)
            {
                auto& resource = ResourceMap[input.Name];

                if (resource.Type == RenderGraphResourceType::ATTACHMENT && input.Type == RenderGraphResourceType::ATTACHMENT)
                {
                    pass_spec.Inputs.push(resource.ResourceInfo.TextureHandle);
                }
                /*
                 * The resource is an attachment from a RenderPass output, but the current node consumes it as Image for
                 * sampling operation
                 */
                else if (resource.Type == RenderGraphResourceType::ATTACHMENT && input.Type == RenderGraphResourceType::TEXTURE)
                {
                    pass_spec.InputTextures[input.BindingInputKeyName] = resource.ResourceInfo.TextureHandle;
                }
            }

            node.Handle->UpdateRenderTargets();
            node.Handle->UpdateInputBinding();

            Specifications::FrameBufferSpecificationVNext framebuffer_spec = {
                .Width         = node.Handle->RenderAreaWidth,
                .Height        = node.Handle->RenderAreaHeight,
                .RenderTargets = node.Handle->RenderTargets,
                .Attachment    = node.Handle->Attachment,
            };
            node.Framebuffer = ZPushStructCtorArgs(Device->Arena, Buffers::FramebufferVNext, Device, framebuffer_spec);
        }
    }

    void RenderGraph::Dispose()
    {
        for (auto& node_name : SortedNodesMap)
        {
            auto& node = NodeMap[node_name];
            node.Handle->Dispose();
            node.Framebuffer->Dispose();
        }

        for (auto resource : ResourceMap)
        {
            auto& value = ResourceMap[resource.first];
            if (value.ResourceInfo.External)
            {
                continue;
            }

            if (value.Type == RenderGraphResourceType::ATTACHMENT || value.Type == RenderGraphResourceType::TEXTURE)
            {
                Device->GlobalTextures.Remove(value.ResourceInfo.TextureHandle);
            }
            else if (value.Type == RenderGraphResourceType::BUFFER_SET)
            {
                // We are safe to call Remove(...) even if Handle is invalid
                Device->StorageBufferSetManager.Remove(value.ResourceInfo.StorageBufferSetHandle);
                Device->UniformBufferSetManager.Remove(value.ResourceInfo.UniformBufferSetHandle);
                Device->IndirectBufferSetManager.Remove(value.ResourceInfo.IndirectBufferSetHandle);
                Device->VertexBufferSetManager.Remove(value.ResourceInfo.VertexBufferSetHandle);
                Device->IndexBufferSetManager.Remove(value.ResourceInfo.IndexBufferSetHandle);
            }
        }
    }

    RenderGraphResource& RenderGraphResourceBuilder::AttachBuffer(cstring name, const Hardwares::StorageBufferSetHandle& buffer)
    {
        Graph->ResourceMap[name].Name                                = name;
        Graph->ResourceMap[name].Type                                = RenderGraphResourceType::BUFFER_SET;
        Graph->ResourceMap[name].ResourceInfo.StorageBufferSetHandle = buffer;
        Graph->ResourceMap[name].ResourceInfo.External               = true;
        return Graph->ResourceMap[name];
    }

    RenderGraphResource& RenderGraphResourceBuilder::AttachBuffer(cstring name, const Hardwares::UniformBufferSetHandle& buffer)
    {
        Graph->ResourceMap[name].Name                                = name;
        Graph->ResourceMap[name].Type                                = RenderGraphResourceType::BUFFER_SET;
        Graph->ResourceMap[name].ResourceInfo.UniformBufferSetHandle = buffer;
        Graph->ResourceMap[name].ResourceInfo.External               = true;
        return Graph->ResourceMap[name];
    }

    RenderGraphResource& RenderGraphResourceBuilder::AttachTexture(cstring name, const Textures::TextureHandle& handle)
    {
        auto texture                                        = Graph->Device->GlobalTextures.Access(handle);
        Graph->ResourceMap[name].Name                       = name;
        Graph->ResourceMap[name].Type                       = RenderGraphResourceType::TEXTURE;
        Graph->ResourceMap[name].ResourceInfo.TextureHandle = handle;
        Graph->ResourceMap[name].ResourceInfo.TextureSpec   = texture->Specification;
        Graph->ResourceMap[name].ResourceInfo.External      = true;
        return Graph->ResourceMap[name];
    }

    RenderGraphResource& RenderGraphResourceBuilder::AttachRenderTarget(cstring name, const Textures::TextureHandle& handle)
    {
        auto texture                                        = Graph->Device->GlobalTextures.Access(handle);
        Graph->ResourceMap[name].Name                       = name;
        Graph->ResourceMap[name].Type                       = RenderGraphResourceType::ATTACHMENT;
        Graph->ResourceMap[name].ResourceInfo.TextureHandle = handle;
        Graph->ResourceMap[name].ResourceInfo.TextureSpec   = texture->Specification;
        Graph->ResourceMap[name].ResourceInfo.External      = true;
        return Graph->ResourceMap[name];
    }

    void RenderGraphResourceBuilder::Initialize(RenderGraphPtr graph)
    {
        Graph = graph;
    }

    RenderGraphResource& RenderGraphResourceBuilder::CreateTexture(cstring name, const Specifications::TextureSpecification& spec)
    {
        Graph->ResourceMap[name].Name                     = name;
        Graph->ResourceMap[name].Type                     = RenderGraphResourceType::TEXTURE;
        Graph->ResourceMap[name].ResourceInfo.TextureSpec = spec;
        return Graph->ResourceMap[name];
    }

    RenderGraphResource& RenderGraphResourceBuilder::CreateTexture(cstring name, cstring filename)
    {
        Graph->ResourceMap[name].Name                       = name;
        Graph->ResourceMap[name].Type                       = RenderGraphResourceType::TEXTURE;
        Graph->ResourceMap[name].ResourceInfo.TextureHandle = Graph->Device->AsyncResLoader->Submit(0, 1 /* 1 : just for testing*/, {.TextureUpload = {.Filename = filename}});
        return Graph->ResourceMap[name];
    }

    RenderGraphResource& RenderGraphResourceBuilder::CreateRenderTarget(cstring name, const Specifications::TextureSpecification& spec)
    {
        Graph->ResourceMap[name].Name                     = name;
        Graph->ResourceMap[name].Type                     = RenderGraphResourceType::ATTACHMENT;
        Graph->ResourceMap[name].ResourceInfo.TextureSpec = spec;
        return Graph->ResourceMap[name];
    }

    void RenderGraphResourceBuilder::CreateRenderPassNode(const RenderGraphRenderPassCreation& creation)
    {
        Graph->NodeMap[creation.Name].Creation = creation;
        for (const auto& output : Graph->NodeMap.at(creation.Name).Creation.Outputs)
        {
            if (output.Type == RenderGraphResourceType::ATTACHMENT)
            {
                RenderGraphResource& resource = Graph->ResourceMap[output.Name];
                resource.ProducerNodeName     = creation.Name;
            }
        }
    }

    RenderGraphResource& RenderGraphResourceBuilder::CreateBufferSet(cstring name, BufferSetCreationType type)
    {
        Graph->ResourceMap[name].Name = name;
        Graph->ResourceMap[name].Type = RenderGraphResourceType::BUFFER_SET;
        switch (type)
        {
            case BufferSetCreationType::INDIRECT:
                Graph->ResourceMap[name].ResourceInfo.IndirectBufferSetHandle = Graph->Device->CreateIndirectBufferSet();
                break;
            case BufferSetCreationType::UNIFORM:
                Graph->ResourceMap[name].ResourceInfo.UniformBufferSetHandle = Graph->Device->CreateUniformBufferSet();
                break;
            case BufferSetCreationType::STORAGE:
                Graph->ResourceMap[name].ResourceInfo.StorageBufferSetHandle = Graph->Device->CreateStorageBufferSet();
                break;
            case BufferSetCreationType::INDEX:
                Graph->ResourceMap[name].ResourceInfo.IndexBufferSetHandle = Graph->Device->CreateIndexBufferSet();
                break;
            case BufferSetCreationType::VERTEX:
                Graph->ResourceMap[name].ResourceInfo.VertexBufferSetHandle = Graph->Device->CreateVertexBufferSet();
                break;
        }
        Graph->ResourceMap[name].ResourceInfo.External = false;
        return Graph->ResourceMap[name];
    }

    void RenderGraphResourceInspector::Initialize(RenderGraphPtr graph)
    {
        Graph = graph;
    }

    RenderGraphResource& RenderGraphResourceInspector::GetResource(cstring name)
    {
        if (!Graph->ResourceMap.contains(name))
        {
            Graph->ResourceMap[name].Name = name;
        }
        return Graph->ResourceMap[name];
    }

    Textures::TextureHandle RenderGraphResourceInspector::GetRenderTarget(cstring name)
    {
        Textures::TextureHandle output = {};
        if (!Graph->ResourceMap.contains(name))
        {
            Graph->ResourceMap[name].Name = name;
        }
        if (Graph->ResourceMap[name].Type != RenderGraphResourceType::ATTACHMENT)
        {
            ZENGINE_CORE_WARN("{} isn't a valid Attachement Resource", name)
        }

        auto handle = Graph->ResourceMap[name].ResourceInfo.TextureHandle;
        if (handle.Valid())
        {
            output = handle;
        }
        return output;
    }

    Textures::TextureHandle RenderGraphResourceInspector::GetTexture(cstring name)
    {
        Textures::TextureHandle output = {};

        if (!Graph->ResourceMap.contains(name))
        {
            Graph->ResourceMap[name].Name = name;
        }
        if (Graph->ResourceMap[name].Type != RenderGraphResourceType::TEXTURE)
        {
            ZENGINE_CORE_WARN("{} isn't a valid Texture Resource", name)
        }

        auto handle = Graph->ResourceMap[name].ResourceInfo.TextureHandle;
        if (handle.Valid())
        {
            output = handle;
        }
        return output;
    }

    Hardwares::StorageBufferSetHandle RenderGraphResourceInspector::GetStorageBufferSet(cstring name)
    {
        if (!Graph->ResourceMap.contains(name))
        {
            Graph->ResourceMap[name].Name = name;
        }
        return Graph->ResourceMap[name].ResourceInfo.StorageBufferSetHandle;
    }

    Hardwares::VertexBufferSetHandle RenderGraphResourceInspector::GetVertexBufferSet(cstring name)
    {
        if (!Graph->ResourceMap.contains(name))
        {
            Graph->ResourceMap[name].Name = name;
        }
        return Graph->ResourceMap[name].ResourceInfo.VertexBufferSetHandle;
    }

    Hardwares::IndexBufferSetHandle RenderGraphResourceInspector::GetIndexBufferSet(cstring name)
    {
        if (!Graph->ResourceMap.contains(name))
        {
            Graph->ResourceMap[name].Name = name;
        }
        return Graph->ResourceMap[name].ResourceInfo.IndexBufferSetHandle;
    }

    Hardwares::UniformBufferSetHandle RenderGraphResourceInspector::GetBufferUniformSet(cstring name)
    {
        if (!Graph->ResourceMap.contains(name))
        {
            Graph->ResourceMap[name].Name = name;
        }
        return Graph->ResourceMap[name].ResourceInfo.UniformBufferSetHandle;
    }

    Hardwares::IndirectBufferSetHandle RenderGraphResourceInspector::GetIndirectBufferSet(cstring name)
    {
        if (!Graph->ResourceMap.contains(name))
        {
            Graph->ResourceMap[name].Name = name;
        }
        return Graph->ResourceMap[name].ResourceInfo.IndirectBufferSetHandle;
    }

    RenderGraphNode& RenderGraphResourceInspector::GetNode(cstring name)
    {
        ZENGINE_VALIDATE_ASSERT(Graph->NodeMap.contains(name), "Node Pass should be created first")
        return Graph->NodeMap[name];
    }
} // namespace ZEngine::Rendering::Renderers
