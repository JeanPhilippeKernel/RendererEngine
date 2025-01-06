#include <pch.h>
#include <GraphicRenderer.h>
#include <Rendering/Buffers/CommandBuffer.h>
#include <Rendering/Renderers/RenderGraph.h>

using namespace ZEngine::Helpers;

namespace ZEngine::Rendering::Renderers
{
    RenderGraphResource& RenderGraphBuilder::AttachBuffer(std::string_view name, const Buffers::StorageBufferSetHandle& buffer)
    {
        std::string resource_name(name);

        m_graph.m_resource_map[resource_name].Name                                = name.data();
        m_graph.m_resource_map[resource_name].Type                                = RenderGraphResourceType::BUFFER_SET;
        m_graph.m_resource_map[resource_name].ResourceInfo.StorageBufferSetHandle = buffer;
        m_graph.m_resource_map[resource_name].ResourceInfo.External               = true;
        return m_graph.m_resource_map[resource_name];
    }

    RenderGraphResource& RenderGraphBuilder::AttachBuffer(std::string_view name, const Buffers::UniformBufferSetHandle& buffer)
    {
        std::string resource_name(name);

        m_graph.m_resource_map[resource_name].Name                                = name.data();
        m_graph.m_resource_map[resource_name].Type                                = RenderGraphResourceType::BUFFER_SET;
        m_graph.m_resource_map[resource_name].ResourceInfo.UniformBufferSetHandle = buffer;
        m_graph.m_resource_map[resource_name].ResourceInfo.External               = true;
        return m_graph.m_resource_map[resource_name];
    }

    RenderGraphResource& RenderGraphBuilder::AttachTexture(std::string_view name, const Textures::TextureHandle& handle)
    {
        std::string resource_name(name);

        auto        texture                                              = m_graph.Renderer->Device->GlobalTextures->Access(handle);
        m_graph.m_resource_map[resource_name].Name                       = name.data();
        m_graph.m_resource_map[resource_name].Type                       = RenderGraphResourceType::TEXTURE;
        m_graph.m_resource_map[resource_name].ResourceInfo.TextureHandle = handle;
        m_graph.m_resource_map[resource_name].ResourceInfo.TextureSpec   = texture->Specification;
        m_graph.m_resource_map[resource_name].ResourceInfo.External      = true;
        return m_graph.m_resource_map[resource_name];
    }

    RenderGraphResource& RenderGraphBuilder::AttachRenderTarget(std::string_view name, const Textures::TextureHandle& handle)
    {
        std::string resource_name(name);

        auto        texture                                              = m_graph.Renderer->Device->GlobalTextures->Access(handle);
        m_graph.m_resource_map[resource_name].Name                       = name.data();
        m_graph.m_resource_map[resource_name].Type                       = RenderGraphResourceType::ATTACHMENT;
        m_graph.m_resource_map[resource_name].ResourceInfo.TextureHandle = handle;
        m_graph.m_resource_map[resource_name].ResourceInfo.TextureSpec   = texture->Specification;
        m_graph.m_resource_map[resource_name].ResourceInfo.External      = true;
        return m_graph.m_resource_map[resource_name];
    }

    RenderGraphResource& RenderGraphBuilder::CreateTexture(std::string_view name, const Specifications::TextureSpecification& spec)
    {
        std::string resource_name(name);
        m_graph.m_resource_map[resource_name].Name                     = name.data();
        m_graph.m_resource_map[resource_name].Type                     = RenderGraphResourceType::TEXTURE;
        m_graph.m_resource_map[resource_name].ResourceInfo.TextureSpec = spec;
        return m_graph.m_resource_map[resource_name];
    }

    RenderGraphResource& RenderGraphBuilder::CreateTexture(std::string_view name, std::string_view filename)
    {
        std::string resource_name(name);

        m_graph.m_resource_map[resource_name].Name                       = name.data();
        m_graph.m_resource_map[resource_name].Type                       = RenderGraphResourceType::TEXTURE;
        m_graph.m_resource_map[resource_name].ResourceInfo.TextureHandle = m_graph.Renderer->LoadTextureFile(filename);
        return m_graph.m_resource_map[resource_name];
    }

    RenderGraphResource& RenderGraphBuilder::CreateRenderTarget(std::string_view name, const Specifications::TextureSpecification& spec)
    {
        std::string resource_name(name);

        m_graph.m_resource_map[resource_name].Name                     = name.data();
        m_graph.m_resource_map[resource_name].Type                     = RenderGraphResourceType::ATTACHMENT;
        m_graph.m_resource_map[resource_name].ResourceInfo.TextureSpec = spec;
        return m_graph.m_resource_map[resource_name];
    }

    void RenderGraphBuilder::CreateRenderPassNode(const RenderGraphRenderPassCreation& creation)
    {
        std::string name(creation.Name);

        m_graph.m_node[name].Creation = creation;
        for (auto& output : m_graph.m_node[name].Creation.Outputs)
        {
            if (output.Type == RenderGraphResourceType::ATTACHMENT)
            {
                RenderGraphResource& resource = m_graph.m_resource_map[output.Name];
                resource.ProducerNodeName     = name;
            }
        }
    }

    RenderGraphResource& RenderGraphBuilder::CreateBufferSet(std::string_view name, BufferSetCreationType type)
    {
        std::string resource_name(name);

        m_graph.m_resource_map[resource_name].Name = name.data();
        m_graph.m_resource_map[resource_name].Type = RenderGraphResourceType::BUFFER_SET;
        switch (type)
        {
            case BufferSetCreationType::INDIRECT:
                m_graph.m_resource_map[resource_name].ResourceInfo.IndirectBufferSetHandle = m_graph.Renderer->CreateIndirectBufferSet();
                break;
            case BufferSetCreationType::UNIFORM:
                m_graph.m_resource_map[resource_name].ResourceInfo.UniformBufferSetHandle = m_graph.Renderer->CreateUniformBufferSet();
                break;
            case BufferSetCreationType::STORAGE:
                m_graph.m_resource_map[resource_name].ResourceInfo.StorageBufferSetHandle = m_graph.Renderer->CreateStorageBufferSet();
                break;
            case BufferSetCreationType::INDEX:
                m_graph.m_resource_map[resource_name].ResourceInfo.IndexBufferSetHandle = m_graph.Renderer->CreateIndexBufferSet();
                break;
            case BufferSetCreationType::VERTEX:
                m_graph.m_resource_map[resource_name].ResourceInfo.VertexBufferSetHandle = m_graph.Renderer->CreateVertexBufferSet();
                break;
        }
        m_graph.m_resource_map[resource_name].ResourceInfo.External = false;
        return m_graph.m_resource_map[resource_name];
    }

    void RenderGraph::Setup()
    {
        for (auto& [name, node] : m_node)
        {
            node.CallbackPass->Setup(name, this);
        }
    }

    void RenderGraph::Compile()
    {
        for (auto& pass : m_node)
        {
            for (uint32_t i = 0; i < pass.second.Creation.Inputs.size(); ++i)
            {
                if (m_resource_map.count(pass.second.Creation.Inputs[i].Name))
                {
                    RenderGraphResource& resource = m_resource_map[pass.second.Creation.Inputs[i].Name];
                    if (m_node.count(resource.ProducerNodeName))
                    {
                        RenderGraphNode& producer_node = m_node[resource.ProducerNodeName];
                        producer_node.EdgeNodes.insert(pass.first);
                    }
                }
            }
        }

        // ToDo:  Potentially remove Node that have no Edges from the graph...?

        /*
         * Topological Sorting
         */
        std::vector<std::string>        sorted_nodes  = {};
        std::map<std::string, uint32_t> visited_nodes = {};
        std::vector<std::string>        stack         = {};

        for (auto& node : m_node)
        {
            stack.emplace_back(node.first);
            while (!stack.empty())
            {
                auto& node_name = stack.back();
                if (visited_nodes[node_name] == 2)
                {
                    stack.pop_back();
                    continue;
                }

                if (visited_nodes[node_name] == 1)
                {
                    visited_nodes[node_name] = 2;
                    sorted_nodes.push_back(node_name);
                    stack.pop_back();
                    continue;
                }

                visited_nodes[node_name] = 1;
                auto& graph_node         = m_node[node_name];
                if (graph_node.EdgeNodes.empty())
                {
                    continue;
                }

                for (auto& edge : graph_node.EdgeNodes)
                {
                    if (!visited_nodes.contains(edge))
                    {
                        stack.emplace_back(edge);
                    }
                }
            }
        }

        for (auto rbegin = sorted_nodes.rbegin(); rbegin != sorted_nodes.rend(); ++rbegin)
        {
            m_sorted_nodes.push_back(*rbegin);
        }

        /*
         * Reading sorting graph node in reverse order and Create resource and RenderPass Node
         */
        auto& global_textures = *(Renderer->Device->GlobalTextures);
        for (std::string_view node_name : m_sorted_nodes)
        {
            auto& node = m_node[node_name.data()];

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
                    auto texture                                        = Renderer->CreateTexture(resource.ResourceInfo.TextureSpec);
                    resource.ResourceInfo.TextureHandle                 = global_textures.Add(texture);

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

            node.CallbackPass->Compile(node.Handle, this);
        }

        for (std::string_view name : m_sorted_nodes)
        {
            auto&                                         node             = m_node[name.data()];
            Specifications::FrameBufferSpecificationVNext framebuffer_spec = {.Width = node.Handle->RenderAreaWidth, .Height = node.Handle->RenderAreaHeight, .RenderTargets = node.Handle->RenderTargets, .Attachment = node.Handle->Attachment};
            node.Framebuffer                                               = CreateRef<Buffers::FramebufferVNext>(Renderer->Device, framebuffer_spec);
        }
    }

    void RenderGraph::Execute(uint32_t frame_index, Buffers::CommandBuffer* const command_buffer, Rendering::Scenes::SceneRawData* const scene_data)
    {
        ZENGINE_VALIDATE_ASSERT(command_buffer, "Command Buffer can't be null")

        auto& global_textures = *(Renderer->Device->GlobalTextures);

        command_buffer->ClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        command_buffer->ClearDepth(1.0f, 0); // Todo : setting value at 1.0f crash on Integrated GPU, floating precision issue or Hardware issue ??

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

                    auto                                            texture                = global_textures[resource.ResourceInfo.TextureHandle];
                    auto&                                           buffer                 = texture->ImageBuffer->GetBuffer();

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

                auto                                            texture      = global_textures[resource.ResourceInfo.TextureHandle];
                auto&                                           buffer       = texture->ImageBuffer->GetBuffer();

                Specifications::ImageMemoryBarrierSpecification barrier_spec = {};
                if (texture->IsDepthTexture)
                {
                    barrier_spec.ImageHandle           = buffer.Handle;
                    barrier_spec.OldLayout             = Specifications::ImageLayout::UNDEFINED;
                    barrier_spec.NewLayout             = Specifications::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    barrier_spec.ImageAspectMask       = VkImageAspectFlagBits(VK_IMAGE_ASPECT_DEPTH_BIT /*| VK_IMAGE_ASPECT_STENCIL_BIT*/); // Todo : To consider Stencil buffer, we want to extend Texture
                                                                                                                                             // spec to introduce HasStencil bit
                    barrier_spec.SourceAccessMask      = 0;
                    barrier_spec.DestinationAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                    barrier_spec.SourceStageMask       = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                    barrier_spec.DestinationStageMask  = VkPipelineStageFlagBits(VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
                    barrier_spec.LayerCount            = 1;

                    Primitives::ImageMemoryBarrier barrier{barrier_spec};
                    command_buffer->TransitionImageLayout(barrier);
                }
                else
                {
                    barrier_spec.ImageHandle           = buffer.Handle;
                    barrier_spec.OldLayout             = Specifications::ImageLayout::UNDEFINED;
                    barrier_spec.NewLayout             = Specifications::ImageLayout::COLOR_ATTACHMENT_OPTIMAL;
                    barrier_spec.ImageAspectMask       = VK_IMAGE_ASPECT_COLOR_BIT;
                    barrier_spec.SourceAccessMask      = 0;
                    barrier_spec.DestinationAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    barrier_spec.SourceStageMask       = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                    barrier_spec.DestinationStageMask  = VkPipelineStageFlagBits(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
                    barrier_spec.LayerCount            = 1;

                    Primitives::ImageMemoryBarrier barrier{barrier_spec};
                    command_buffer->TransitionImageLayout(barrier);
                }
            }

            node.CallbackPass->Execute(frame_index, scene_data, node.Handle.get(), command_buffer, this);
            node.CallbackPass->Render(frame_index, node.Handle.get(), node.Framebuffer.get(), command_buffer, this);
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

                auto temp_handle        = Renderer->Device->GlobalTextures->Create();
                auto texture_to_dispose = Renderer->Device->GlobalTextures->Access(resource.ResourceInfo.TextureHandle);
                Renderer->Device->GlobalTextures->Update(temp_handle, texture_to_dispose);
                Renderer->Device->GlobalTextures->Remove(temp_handle);

                resource.ResourceInfo.TextureSpec.Width  = width;
                resource.ResourceInfo.TextureSpec.Height = height;
                auto texture                             = Renderer->CreateTexture(resource.ResourceInfo.TextureSpec);
                Renderer->Device->GlobalTextures->Update(resource.ResourceInfo.TextureHandle, texture);
                pass_spec.ExternalOutputs.emplace_back(resource.ResourceInfo.TextureHandle);
            }

            for (auto& input : node.Creation.Inputs)
            {
                auto& resource = m_resource_map[input.Name];

                if (resource.Type == RenderGraphResourceType::ATTACHMENT && input.Type == RenderGraphResourceType::ATTACHMENT)
                {
                    pass_spec.Inputs.push_back(resource.ResourceInfo.TextureHandle);
                }
                /*
                 * The resource is an attachment from a RenderPass output, but the current node consumes it as Image for sampling operation
                 */
                else if (resource.Type == RenderGraphResourceType::ATTACHMENT && input.Type == RenderGraphResourceType::TEXTURE)
                {
                    pass_spec.InputTextures[input.BindingInputKeyName] = resource.ResourceInfo.TextureHandle;
                }
            }

            node.Handle->UpdateRenderTargets();
            node.Handle->UpdateInputBinding();

            Specifications::FrameBufferSpecificationVNext framebuffer_spec = {.Width = node.Handle->RenderAreaWidth, .Height = node.Handle->RenderAreaHeight, .RenderTargets = node.Handle->RenderTargets, .Attachment = node.Handle->Attachment};
            node.Framebuffer                                               = CreateRef<Buffers::FramebufferVNext>(Renderer->Device, framebuffer_spec);
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

        for (auto& resource : m_resource_map)
        {
            auto& value = m_resource_map[resource.first];
            if (value.ResourceInfo.External)
            {
                continue;
            }

            if (value.Type == RenderGraphResourceType::ATTACHMENT || value.Type == RenderGraphResourceType::TEXTURE)
            {
                if (value.ResourceInfo.TextureHandle)
                {
                    Renderer->Device->GlobalTextures->Remove(value.ResourceInfo.TextureHandle);
                }
            }
            else if (value.Type == RenderGraphResourceType::BUFFER_SET)
            {
                if (value.ResourceInfo.StorageBufferSetHandle)
                {
                    Renderer->StorageBufferSetManager.Remove(value.ResourceInfo.StorageBufferSetHandle);
                }
                else if (value.ResourceInfo.UniformBufferSetHandle)
                {
                    Renderer->UniformBufferSetManager.Remove(value.ResourceInfo.UniformBufferSetHandle);
                }
                else if (value.ResourceInfo.IndirectBufferSetHandle)
                {
                    Renderer->IndirectBufferSetManager.Remove(value.ResourceInfo.IndirectBufferSetHandle);
                }
                else if (value.ResourceInfo.VertexBufferSetHandle)
                {
                    Renderer->VertexBufferSetManager.Remove(value.ResourceInfo.VertexBufferSetHandle);
                }
                else if (value.ResourceInfo.IndexBufferSetHandle)
                {
                    Renderer->IndexBufferSetManager.Remove(value.ResourceInfo.IndexBufferSetHandle);
                }
            }
        }
    }

    RenderGraphResource& RenderGraph::GetResource(std::string_view name)
    {
        std::string resource_name(name);
        if (!m_resource_map.contains(resource_name))
        {
            m_resource_map[resource_name].Name = name.data();
        }
        return m_resource_map[resource_name];
    }

    Textures::TextureHandle RenderGraph::GetRenderTarget(std::string_view name)
    {
        Textures::TextureHandle output = {};
        std::string             resource_name(name);
        if (!m_resource_map.contains(resource_name))
        {
            m_resource_map[resource_name].Name = name.data();
        }
        if (m_resource_map[resource_name].Type != RenderGraphResourceType::ATTACHMENT)
        {
            ZENGINE_CORE_WARN("{} isn't a valid Attachement Resource", name.data())
        }

        auto handle = m_resource_map[resource_name].ResourceInfo.TextureHandle;
        if (handle.Valid())
        {
            output = handle;
        }
        return output;
    }

    Textures::TextureHandle RenderGraph::GetTexture(std::string_view name)
    {
        Textures::TextureHandle output = {};

        std::string             resource_name(name);
        if (!m_resource_map.contains(resource_name))
        {
            m_resource_map[resource_name].Name = name.data();
        }
        if (m_resource_map[resource_name].Type != RenderGraphResourceType::TEXTURE)
        {
            ZENGINE_CORE_WARN("{} isn't a valid Texture Resource", name.data())
        }

        auto handle = m_resource_map[resource_name].ResourceInfo.TextureHandle;
        if (handle.Valid())
        {
            output = handle;
        }
        return output;
    }

    Buffers::StorageBufferSetHandle RenderGraph::GetStorageBufferSet(std::string_view name)
    {
        std::string resource_name(name);
        if (!m_resource_map.contains(resource_name))
        {
            m_resource_map[resource_name].Name = name.data();
        }
        return m_resource_map[resource_name].ResourceInfo.StorageBufferSetHandle;
    }

    Buffers::VertexBufferSetHandle RenderGraph::GetVertexBufferSet(std::string_view name)
    {
        std::string resource_name(name);
        if (!m_resource_map.contains(resource_name))
        {
            m_resource_map[resource_name].Name = name.data();
        }
        return m_resource_map[resource_name].ResourceInfo.VertexBufferSetHandle;
    }

    Buffers::IndexBufferSetHandle RenderGraph::GetIndexBufferSet(std::string_view name)
    {
        std::string resource_name(name);
        if (!m_resource_map.contains(resource_name))
        {
            m_resource_map[resource_name].Name = name.data();
        }
        return m_resource_map[resource_name].ResourceInfo.IndexBufferSetHandle;
    }

    Buffers::UniformBufferSetHandle RenderGraph::GetBufferUniformSet(std::string_view name)
    {
        std::string resource_name(name);
        if (!m_resource_map.contains(resource_name))
        {
            m_resource_map[resource_name].Name = name.data();
        }
        return m_resource_map[resource_name].ResourceInfo.UniformBufferSetHandle;
    }

    Buffers::IndirectBufferSetHandle RenderGraph::GetIndirectBufferSet(std::string_view name)
    {
        std::string resource_name(name);
        if (!m_resource_map.contains(resource_name))
        {
            m_resource_map[resource_name].Name = name.data();
        }
        return m_resource_map[resource_name].ResourceInfo.IndirectBufferSetHandle;
    }

    RenderGraphNode& RenderGraph::GetNode(std::string_view name)
    {
        auto pass_name = name.data();

        ZENGINE_VALIDATE_ASSERT(m_node.contains(pass_name), "Node Pass should be created first")
        return m_node[pass_name];
    }

    void RenderGraph::AddCallbackPass(std::string_view pass_name, const Ref<IRenderGraphCallbackPass>& pass_callback, bool enabled)
    {
        std::string resource_name(pass_name);

        m_node[resource_name].Enabled      = enabled;
        m_node[resource_name].CallbackPass = pass_callback;
    }
} // namespace ZEngine::Rendering::Renderers
