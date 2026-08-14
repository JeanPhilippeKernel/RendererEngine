#include <ZEngine/Core/Containers/UnorderedHashSet.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/IRenderer.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <stack>

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
            NodeMap[name].EdgeNodes.init(Device->Arena);
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
                        producer_node.EdgeNodes.insert(pass.first);
                    }
                }
            }
        }

        // ToDo:  Potentially remove Node that have no Edges from the graph...?

        /*
         * Topological Sorting
         */
        auto                      scratch         = ZGetScratch(Device->Arena);

        Array<cstring>            sorted_nodes    = {};
        UnorderedHashSet<cstring> processed_nodes = {};
        UnorderedHashSet<cstring> visited_nodes   = {};
        UnorderedHashSet<cstring> in_stack_nodes  = {};
        std::stack<cstring>       stack           = {};

        sorted_nodes.init(scratch.Arena, NodeMap.size());
        visited_nodes.init(scratch.Arena);
        processed_nodes.init(scratch.Arena);
        in_stack_nodes.init(scratch.Arena);

        for (const auto& [name, _] : NodeMap)
        {
            if (processed_nodes.contains(name))
            {
                continue;
            }

            stack.push(name);

            while (!stack.empty())
            {
                // Copy, not reference: pushing to the stack can invalidate a top() ref.
                const auto top = stack.top();

                if (!visited_nodes.contains(top))
                {
                    visited_nodes.insert(top);
                    in_stack_nodes.insert(top);
                    // Re-push self below its children so it is emitted only after all
                    // descendants are processed (post-order).
                    stack.push(top);

                    for (auto edge : NodeMap[top].EdgeNodes)
                    {
                        if (in_stack_nodes.contains(edge))
                        {
                            ZENGINE_CORE_ERROR("RenderGraph: cycle detected between '{}' and '{}'", top, edge)
                            continue;
                        }
                        if (!visited_nodes.contains(edge))
                        {
                            stack.push(edge);
                        }
                    }
                }
                else
                {
                    stack.pop();

                    if (!processed_nodes.contains(top))
                    {
                        sorted_nodes.push(top);
                        processed_nodes.insert(top);
                        in_stack_nodes.remove(top);
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

                else if (output.Type == RenderGraphResourceType::REFERENCE)
                {
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
            auto& node = NodeMap[name];
            if (node.Handle && (node.Handle->Specification.Type != Specifications::RenderPassType::GRAPHIC))
            {
                continue;
            }

            Specifications::FrameBufferSpecificationVNext framebuffer_spec = {
                .Width         = node.Handle->RenderAreaWidth,
                .Height        = node.Handle->RenderAreaHeight,
                .RenderTargets = node.Handle->RenderTargets,
                .Attachment    = node.Handle->Attachment,
            };
            node.Framebuffer = ZPushStructCtorArgs(Device->Arena, Buffers::FramebufferVNext, Device, framebuffer_spec);
        }
    }

    void RenderGraph::Execute(Hardwares::CommandBufferPtr const command_buffer)
    {
        ZENGINE_VALIDATE_ASSERT(command_buffer, "Command Buffer can't be null")

        command_buffer->ClearColor(0.11f, 0.11f, 0.11f, 1.0f); // #1C1C1C dark carbon
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

                if (resource.Type == RenderGraphResourceType::ATTACHMENT)
                {
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

            if ((pass_spec.Type != Specifications::RenderPassType::GRAPHIC) && (pass_spec.Type != Specifications::RenderPassType::COMPUTE))
            {
                continue;
            }

            pass_spec.ExternalOutputs.clear();
            pass_spec.Inputs.clear();
            pass_spec.InputTextures.clear();

            for (auto& output : node.Creation.Outputs)
            {
                auto& resource = ResourceMap[output.Name];

                if (output.Type == RenderGraphResourceType::REFERENCE)
                {
                    pass_spec.ExternalOutputs.push(resource.ResourceInfo.TextureHandle);
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
            node.CallbackPass->Deinitialize(Device);
            node.Handle->Dispose();
            if (node.Handle->Specification.Type == Specifications::RenderPassType::GRAPHIC)
            {
                node.Framebuffer->Dispose();
            }
        }

        for (const auto& resource : ResourceMap)
        {
            auto& value = ResourceMap[resource.first];
            if (value.ResourceInfo.External)
            {
                continue;
            }

            if (value.Type == RenderGraphResourceType::ATTACHMENT || value.Type == RenderGraphResourceType::TEXTURE)
            {
                if (value.ResourceInfo.TextureHandle.Valid())
                    Device->TextureHandleToDispose.Enqueue(value.ResourceInfo.TextureHandle);
            }
        }
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
        Graph->ResourceMap[name].ResourceInfo.TextureHandle = Graph->Device->RRM ? static_cast<RenderResourceManager*>(Graph->Device->RRM)->SubmitTextureFile(0, 0, filename) : Rendering::Textures::TextureHandle{};
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
        for (const auto& output : creation.Outputs)
        {
            if (output.Type == RenderGraphResourceType::ATTACHMENT)
            {
                RenderGraphResource& resource = Graph->ResourceMap[output.Name];
                resource.ProducerNodeName     = creation.Name;
            }
        }
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

    RenderGraphNode& RenderGraphResourceInspector::GetNode(cstring name)
    {
        ZENGINE_VALIDATE_ASSERT(Graph->NodeMap.contains(name), "Node Pass should be created first")
        return Graph->NodeMap[name];
    }
} // namespace ZEngine::Rendering::Renderers
