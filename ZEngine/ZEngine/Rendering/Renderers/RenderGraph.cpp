#include <pch.h>
#include <GraphicRenderer.h>
#include <Rendering/Renderers/RenderGraph.h>

using namespace ZEngine::Core::Containers;

using namespace ZEngine::Helpers;

namespace ZEngine::Rendering::Renderers
{
    RenderGraphResource& RenderGraphBuilder::AttachBuffer(const char* name, const Hardwares::StorageBufferSetHandle& buffer)
    {
        m_graph.m_resource_map[name].Name                                = name;
        m_graph.m_resource_map[name].Type                                = RenderGraphResourceType::BUFFER_SET;
        m_graph.m_resource_map[name].ResourceInfo.StorageBufferSetHandle = buffer;
        m_graph.m_resource_map[name].ResourceInfo.External               = true;
        return m_graph.m_resource_map[name];
    }

    RenderGraphResource& RenderGraphBuilder::AttachBuffer(const char* name, const Hardwares::UniformBufferSetHandle& buffer)
    {
        m_graph.m_resource_map[name].Name                                = name;
        m_graph.m_resource_map[name].Type                                = RenderGraphResourceType::BUFFER_SET;
        m_graph.m_resource_map[name].ResourceInfo.UniformBufferSetHandle = buffer;
        m_graph.m_resource_map[name].ResourceInfo.External               = true;
        return m_graph.m_resource_map[name];
    }

    RenderGraphResource& RenderGraphBuilder::AttachTexture(const char* name, const Textures::TextureHandle& handle)
    {
        auto texture                                            = m_graph.Renderer->Device->GlobalTextures.Access(handle);
        m_graph.m_resource_map[name].Name                       = name;
        m_graph.m_resource_map[name].Type                       = RenderGraphResourceType::TEXTURE;
        m_graph.m_resource_map[name].ResourceInfo.TextureHandle = handle;
        m_graph.m_resource_map[name].ResourceInfo.TextureSpec   = texture->Specification;
        m_graph.m_resource_map[name].ResourceInfo.External      = true;
        return m_graph.m_resource_map[name];
    }

    RenderGraphResource& RenderGraphBuilder::AttachRenderTarget(const char* name, const Textures::TextureHandle& handle)
    {
        auto texture                                            = m_graph.Renderer->Device->GlobalTextures.Access(handle);
        m_graph.m_resource_map[name].Name                       = name;
        m_graph.m_resource_map[name].Type                       = RenderGraphResourceType::ATTACHMENT;
        m_graph.m_resource_map[name].ResourceInfo.TextureHandle = handle;
        m_graph.m_resource_map[name].ResourceInfo.TextureSpec   = texture->Specification;
        m_graph.m_resource_map[name].ResourceInfo.External      = true;
        return m_graph.m_resource_map[name];
    }

    RenderGraphResource& RenderGraphBuilder::CreateTexture(const char* name, const Specifications::TextureSpecification& spec)
    {
        m_graph.m_resource_map[name].Name                     = name;
        m_graph.m_resource_map[name].Type                     = RenderGraphResourceType::TEXTURE;
        m_graph.m_resource_map[name].ResourceInfo.TextureSpec = spec;
        return m_graph.m_resource_map[name];
    }

    RenderGraphResource& RenderGraphBuilder::CreateTexture(const char* name, const char* filename)
    {
        m_graph.m_resource_map[name].Name                       = name;
        m_graph.m_resource_map[name].Type                       = RenderGraphResourceType::TEXTURE;
        m_graph.m_resource_map[name].ResourceInfo.TextureHandle = m_graph.Renderer->AsyncLoader->LoadTextureFile(filename);
        return m_graph.m_resource_map[name];
    }

    RenderGraphResource& RenderGraphBuilder::CreateRenderTarget(const char* name, const Specifications::TextureSpecification& spec)
    {
        m_graph.m_resource_map[name].Name                     = name;
        m_graph.m_resource_map[name].Type                     = RenderGraphResourceType::ATTACHMENT;
        m_graph.m_resource_map[name].ResourceInfo.TextureSpec = spec;
        return m_graph.m_resource_map[name];
    }

    void RenderGraphBuilder::CreateRenderPassNode(const RenderGraphRenderPassCreation& creation)
    {
        m_graph.m_node[creation.Name].Creation = creation;
        for (const auto& output : m_graph.m_node.at(creation.Name).Creation.Outputs)
        {
            if (output.Type == RenderGraphResourceType::ATTACHMENT)
            {
                RenderGraphResource& resource = m_graph.m_resource_map[output.Name];
                resource.ProducerNodeName     = creation.Name;
            }
        }
    }

    RenderGraphResource& RenderGraphBuilder::CreateBufferSet(const char* name, BufferSetCreationType type)
    {
        m_graph.m_resource_map[name].Name = name;
        m_graph.m_resource_map[name].Type = RenderGraphResourceType::BUFFER_SET;
        switch (type)
        {
            case BufferSetCreationType::INDIRECT:
                m_graph.m_resource_map[name].ResourceInfo.IndirectBufferSetHandle = m_graph.Renderer->Device->CreateIndirectBufferSet();
                break;
            case BufferSetCreationType::UNIFORM:
                m_graph.m_resource_map[name].ResourceInfo.UniformBufferSetHandle = m_graph.Renderer->Device->CreateUniformBufferSet();
                break;
            case BufferSetCreationType::STORAGE:
                m_graph.m_resource_map[name].ResourceInfo.StorageBufferSetHandle = m_graph.Renderer->Device->CreateStorageBufferSet();
                break;
            case BufferSetCreationType::INDEX:
                m_graph.m_resource_map[name].ResourceInfo.IndexBufferSetHandle = m_graph.Renderer->Device->CreateIndexBufferSet();
                break;
            case BufferSetCreationType::VERTEX:
                m_graph.m_resource_map[name].ResourceInfo.VertexBufferSetHandle = m_graph.Renderer->Device->CreateVertexBufferSet();
                break;
        }
        m_graph.m_resource_map[name].ResourceInfo.External = false;
        return m_graph.m_resource_map[name];
    }

    void RenderGraph::Initialize(Core::Memory::ArenaAllocator* arena, GraphicRenderer* renderer)
    {
        Renderer          = renderer;
        Builder           = ZPushStructCtorArgs(arena, RenderGraphBuilder, *this);
        RenderPassBuilder = ZPushStructCtorArgs(arena, RenderPasses::RenderPassBuilder);
        RenderPassBuilder->Initialize(arena);
        m_sorted_nodes.init(arena, 7);
        m_node.init(arena);
        m_resource_map.init(arena);
    }

    void RenderGraph::Setup()
    {
        for (auto [name, node] : m_node)
        {
            node.EdgeNodes.init(Renderer->Device->Arena, 5);
            node.CallbackPass->Setup(name, this);
        }
    }

    void RenderGraph::Compile()
    {
        for (auto pass : m_node)
        {
            for (uint32_t i = 0; i < pass.second.Creation.Inputs.size(); ++i)
            {
                if (m_resource_map.contains(pass.second.Creation.Inputs[i].Name))
                {
                    RenderGraphResource& resource = m_resource_map[pass.second.Creation.Inputs[i].Name];
                    if (m_node.contains(resource.ProducerNodeName))
                    {
                        RenderGraphNode& producer_node = m_node[resource.ProducerNodeName];
                        producer_node.EdgeNodes.push(pass.first);
                    }
                }
            }
        }

        // ToDo:  Potentially remove Node that have no Edges from the graph...?

        /*
         * Topological Sorting
         */
        auto                           scratch       = ZGetScratch(Renderer->Device->Arena);

        Array<const char*>             sorted_nodes  = {};
        HashMap<const char*, uint32_t> visited_nodes = {};
        Array<const char*>             stack         = {};

        sorted_nodes.init(scratch.Arena, 6);
        stack.init(scratch.Arena, 6);
        visited_nodes.init(scratch.Arena);

        for (auto node : m_node)
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
                auto& graph_node         = m_node[node_name];
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
            m_sorted_nodes.push(*end);
            end = std::prev(end);
        }

        ZReleaseScratch(scratch);

        /*
         * Reading sorting graph node in reverse order and Create resource and RenderPass Node
         */
        for (const char* node_name : m_sorted_nodes)
        {
            auto& node = m_node[node_name];

            RenderPassBuilder->SetName(node.Creation.Name);

            for (auto& output : node.Creation.Outputs)
            {
                auto& resource = m_resource_map[output.Name];

                if (resource.ResourceInfo.External)
                {
                    RenderPassBuilder->UseRenderTarget(resource.ResourceInfo.TextureHandle);
                    continue;
                }

                if (output.Type == RenderGraphResourceType::ATTACHMENT)
                {
                    resource.ResourceInfo.TextureSpec.PerformTransition = false;
                    resource.ResourceInfo.TextureHandle                 = Renderer->Device->CreateTexture(resource.ResourceInfo.TextureSpec);

                    RenderPassBuilder->UseRenderTarget(resource.ResourceInfo.TextureHandle);
                }
            }

            for (auto& input : node.Creation.Inputs)
            {
                auto& resource = m_resource_map[input.Name];

                if (input.Type == RenderGraphResourceType::ATTACHMENT)
                {
                    RenderPassBuilder->AddInputAttachment(resource.ResourceInfo.TextureHandle);
                }
                else if (input.Type == RenderGraphResourceType::TEXTURE)
                {
                    RenderPassBuilder->AddInputTexture(input.BindingInputKeyName, resource.ResourceInfo.TextureHandle);
                }
            }

            node.CallbackPass->Compile(&(node.Handle), this);
        }

        for (const char* name : m_sorted_nodes)
        {
            auto&                                         node             = m_node[name];
            Specifications::FrameBufferSpecificationVNext framebuffer_spec = {.Width = node.Handle->RenderAreaWidth, .Height = node.Handle->RenderAreaHeight, .RenderTargets = node.Handle->RenderTargets, .Attachment = node.Handle->Attachment};
            node.Framebuffer                                               = ZPushStructCtorArgs(Renderer->Device->Arena, Buffers::FramebufferVNext, Renderer->Device, framebuffer_spec);
        }
    }

    void RenderGraph::Execute(Hardwares::CommandBuffer* const command_buffer, Rendering::Scenes::SceneData* const scene)
    {
        ZENGINE_VALIDATE_ASSERT(command_buffer, "Command Buffer can't be null")

        command_buffer->ClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        command_buffer->ClearDepth(1.0f, 0);

        for (auto& node_name : m_sorted_nodes)
        {
            auto& node = m_node[node_name];

            if (!node.Enabled)
            {
                continue;
            }

            for (auto& input : node.Creation.Inputs)
            {
                if (input.Type == RenderGraphResourceType::TEXTURE)
                {
                    auto&                                           resource               = m_resource_map[input.Name];
                    /*
                     * The input texture can from an attachment that should read as Shader Sampler2D data
                     * So we need ensure the right config for transition
                     */
                    bool                                            is_resource_attachment = resource.Type == RenderGraphResourceType::ATTACHMENT;

                    auto                                            texture                = Renderer->Device->GlobalTextures.Access(resource.ResourceInfo.TextureHandle);
                    auto                                            img_buf                = Renderer->Device->Image2DBufferManager.Access(texture->BufferHandle);
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

                auto& resource = m_resource_map[output.Name];
                ZENGINE_VALIDATE_ASSERT(resource.Type == RenderGraphResourceType::ATTACHMENT, "RenderPass Output should be an Attachment")

                auto                                            texture      = Renderer->Device->GlobalTextures.Access(resource.ResourceInfo.TextureHandle);
                auto                                            img_buf      = Renderer->Device->Image2DBufferManager.Access(texture->BufferHandle);
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

            node.CallbackPass->Execute(scene, node.Handle, command_buffer, this);
            node.CallbackPass->Render(scene, node.Handle, node.Framebuffer, command_buffer, this);
        }
    }

    void RenderGraph::Resize(uint32_t width, uint32_t height)
    {
        for (auto& node_name : m_sorted_nodes)
        {
            auto& node      = m_node[node_name];

            auto& pass_spec = node.Handle->Specification;

            pass_spec.ExternalOutputs.clear();
            pass_spec.Inputs.clear();
            pass_spec.InputTextures.clear();

            for (auto& output : node.Creation.Outputs)
            {
                auto& resource = m_resource_map[output.Name];

                if (output.Type == RenderGraphResourceType::REFERENCE)
                {
                    continue;
                }

                auto temp_handle        = Renderer->Device->GlobalTextures.Create();
                auto texture_to_dispose = Renderer->Device->GlobalTextures.Access(resource.ResourceInfo.TextureHandle);
                Renderer->Device->GlobalTextures.Update(temp_handle, *texture_to_dispose);
                Renderer->Device->TextureHandleToDispose.Enqueue(temp_handle);

                // We invalidate ResourceInfo.TextureHandle, so it can be recycle for another texture allocation
                Renderer->Device->GlobalTextures.Remove(resource.ResourceInfo.TextureHandle);
                resource.ResourceInfo.TextureSpec.Width  = width;
                resource.ResourceInfo.TextureSpec.Height = height;
                resource.ResourceInfo.TextureHandle      = Renderer->Device->CreateTexture(resource.ResourceInfo.TextureSpec);

                if ((output.Name == Renderer->FrameColorRenderTargetName) || (output.Name == Renderer->FrameDepthRenderTargetName))
                {
                    Renderer->Device->TextureHandleToUpdates.Enqueue(resource.ResourceInfo.TextureHandle);
                }
                pass_spec.ExternalOutputs.push(resource.ResourceInfo.TextureHandle);
            }

            for (auto& input : node.Creation.Inputs)
            {
                auto& resource = m_resource_map[input.Name];

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

            Specifications::FrameBufferSpecificationVNext framebuffer_spec = {.Width = node.Handle->RenderAreaWidth, .Height = node.Handle->RenderAreaHeight, .RenderTargets = node.Handle->RenderTargets, .Attachment = node.Handle->Attachment};
            node.Framebuffer                                               = ZPushStructCtorArgs(Renderer->Device->Arena, Buffers::FramebufferVNext, Renderer->Device, framebuffer_spec);
        }
    }

    void RenderGraph::Dispose()
    {
        for (auto& node_name : m_sorted_nodes)
        {
            auto& node = m_node[node_name];
            node.Handle->Dispose();
            node.Framebuffer->Dispose();
        }

        for (auto resource : m_resource_map)
        {
            auto& value = m_resource_map[resource.first];
            if (value.ResourceInfo.External)
            {
                continue;
            }

            if (value.Type == RenderGraphResourceType::ATTACHMENT || value.Type == RenderGraphResourceType::TEXTURE)
            {
                Renderer->Device->GlobalTextures.Remove(value.ResourceInfo.TextureHandle);
            }
            else if (value.Type == RenderGraphResourceType::BUFFER_SET)
            {
                // We are safe to call Remove(...) even if Handle is invalid
                Renderer->Device->StorageBufferSetManager.Remove(value.ResourceInfo.StorageBufferSetHandle);
                Renderer->Device->UniformBufferSetManager.Remove(value.ResourceInfo.UniformBufferSetHandle);
                Renderer->Device->IndirectBufferSetManager.Remove(value.ResourceInfo.IndirectBufferSetHandle);
                Renderer->Device->VertexBufferSetManager.Remove(value.ResourceInfo.VertexBufferSetHandle);
                Renderer->Device->IndexBufferSetManager.Remove(value.ResourceInfo.IndexBufferSetHandle);
            }
        }
    }

    RenderGraphResource& RenderGraph::GetResource(const char* name)
    {
        if (!m_resource_map.contains(name))
        {
            m_resource_map[name].Name = name;
        }
        return m_resource_map[name];
    }

    Textures::TextureHandle RenderGraph::GetRenderTarget(const char* name)
    {
        Textures::TextureHandle output = {};
        if (!m_resource_map.contains(name))
        {
            m_resource_map[name].Name = name;
        }
        if (m_resource_map[name].Type != RenderGraphResourceType::ATTACHMENT)
        {
            ZENGINE_CORE_WARN("{} isn't a valid Attachement Resource", name)
        }

        auto handle = m_resource_map[name].ResourceInfo.TextureHandle;
        if (handle.Valid())
        {
            output = handle;
        }
        return output;
    }

    Textures::TextureHandle RenderGraph::GetTexture(const char* name)
    {
        Textures::TextureHandle output = {};

        if (!m_resource_map.contains(name))
        {
            m_resource_map[name].Name = name;
        }
        if (m_resource_map[name].Type != RenderGraphResourceType::TEXTURE)
        {
            ZENGINE_CORE_WARN("{} isn't a valid Texture Resource", name)
        }

        auto handle = m_resource_map[name].ResourceInfo.TextureHandle;
        if (handle.Valid())
        {
            output = handle;
        }
        return output;
    }

    Hardwares::StorageBufferSetHandle RenderGraph::GetStorageBufferSet(const char* name)
    {
        if (!m_resource_map.contains(name))
        {
            m_resource_map[name].Name = name;
        }
        return m_resource_map[name].ResourceInfo.StorageBufferSetHandle;
    }

    Hardwares::VertexBufferSetHandle RenderGraph::GetVertexBufferSet(const char* name)
    {
        if (!m_resource_map.contains(name))
        {
            m_resource_map[name].Name = name;
        }
        return m_resource_map[name].ResourceInfo.VertexBufferSetHandle;
    }

    Hardwares::IndexBufferSetHandle RenderGraph::GetIndexBufferSet(const char* name)
    {
        if (!m_resource_map.contains(name))
        {
            m_resource_map[name].Name = name;
        }
        return m_resource_map[name].ResourceInfo.IndexBufferSetHandle;
    }

    Hardwares::UniformBufferSetHandle RenderGraph::GetBufferUniformSet(const char* name)
    {
        if (!m_resource_map.contains(name))
        {
            m_resource_map[name].Name = name;
        }
        return m_resource_map[name].ResourceInfo.UniformBufferSetHandle;
    }

    Hardwares::IndirectBufferSetHandle RenderGraph::GetIndirectBufferSet(const char* name)
    {
        if (!m_resource_map.contains(name))
        {
            m_resource_map[name].Name = name;
        }
        return m_resource_map[name].ResourceInfo.IndirectBufferSetHandle;
    }

    RenderGraphNode& RenderGraph::GetNode(const char* name)
    {
        ZENGINE_VALIDATE_ASSERT(m_node.contains(name), "Node Pass should be created first")
        return m_node[name];
    }

    void RenderGraph::AddCallbackPass(const char* pass_name, IRenderGraphCallbackPass* const pass_callback, bool enabled)
    {
        m_node[pass_name].Enabled      = enabled;
        m_node[pass_name].CallbackPass = pass_callback;
    }
} // namespace ZEngine::Rendering::Renderers
