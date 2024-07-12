#include <pch.h>
#include <Rendering/Renderers/RenderGraph.h>
#include <Rendering/Textures/Texture2D.h>

namespace ZEngine::Rendering::Renderers
{
    RenderGraphResource& RenderGraphBuilder::AttachBuffer(std::string_view name, const Ref<Buffers::StorageBufferSet>& buffer)
    {
        std::string resource_name(name);

        m_graph.m_resource_map[resource_name].Name                         = name.data();
        m_graph.m_resource_map[resource_name].Type                         = RenderGraphResourceType::BUFFER_SET;
        m_graph.m_resource_map[resource_name].ResourceInfo.BufferSetHandle = buffer;
        m_graph.m_resource_map[resource_name].ResourceInfo.External        = true;
        return m_graph.m_resource_map[resource_name];
    }

    RenderGraphResource& RenderGraphBuilder::AttachBuffer(std::string_view name, const Ref<Buffers::UniformBufferSet>& buffer)
    {
        std::string resource_name(name);

        m_graph.m_resource_map[resource_name].Name                                = name.data();
        m_graph.m_resource_map[resource_name].Type                                = RenderGraphResourceType::BUFFER_SET;
        m_graph.m_resource_map[resource_name].ResourceInfo.UniformBufferSetHandle = buffer;
        m_graph.m_resource_map[resource_name].ResourceInfo.External               = true;
        return m_graph.m_resource_map[resource_name];
    }

    RenderGraphResource& RenderGraphBuilder::AttachTexture(std::string_view name, const Ref<Textures::Texture>& texture)
    {
        std::string resource_name(name);

        m_graph.m_resource_map[resource_name].Name                       = name.data();
        m_graph.m_resource_map[resource_name].Type                       = RenderGraphResourceType::TEXTURE;
        m_graph.m_resource_map[resource_name].ResourceInfo.TextureHandle = texture;
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

    RenderGraphResource& RenderGraphBuilder::CreateBufferSet(std::string_view name, uint32_t count, BufferSetCreationType type)
    {
        std::string resource_name(name);

        m_graph.m_resource_map[resource_name].Name = name.data();
        m_graph.m_resource_map[resource_name].Type = RenderGraphResourceType::BUFFER_SET;
        switch (type)
        {
            case ZEngine::Rendering::Renderers::INDIRECT:
                m_graph.m_resource_map[resource_name].ResourceInfo.IndirectBufferSetHandle = CreateRef<Buffers::IndirectBufferSet>(count);
                break;
            case ZEngine::Rendering::Renderers::UNIFORM:
                m_graph.m_resource_map[resource_name].ResourceInfo.UniformBufferSetHandle = CreateRef<Buffers::UniformBufferSet>(count);
                break;
            case ZEngine::Rendering::Renderers::STORAGE:
                m_graph.m_resource_map[resource_name].ResourceInfo.BufferSetHandle = CreateRef<Buffers::StorageBufferSet>(count);
                break;
        }
        m_graph.m_resource_map[resource_name].ResourceInfo.External = false;
        return m_graph.m_resource_map[resource_name];
    }

    void RenderGraph::Setup()
    {
        for (auto& pass_callback : m_node)
        {
            pass_callback.second.CallbackPass->Setup(pass_callback.first, m_builder.get());
        }
    }

    void RenderGraph::Compile()
    {
        for (auto& pass : m_node)
        {
            for (uint32_t i = 0; i < pass.second.Creation.Inputs.size(); ++i)
            {
                RenderGraphResource& resource      = m_resource_map[pass.second.Creation.Inputs[i].Name];
                RenderGraphNode&     producer_node = m_node[resource.ProducerNodeName];
                producer_node.EdgeNodes.insert(pass.first);
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
        RenderPasses::RenderPassBuilder pass_builder = {};

        for (std::string_view node_name : m_sorted_nodes)
        {
            auto& node = m_node[node_name.data()];

            pass_builder.SetName(node.Creation.Name);

            for (auto& output : node.Creation.Outputs)
            {
                auto& resource = m_resource_map[output.Name];

                if (resource.ResourceInfo.External)
                {
                    pass_builder.UseRenderTarget(resource.ResourceInfo.TextureHandle);
                    continue;
                }

                if (output.Type == RenderGraphResourceType::ATTACHMENT)
                {
                    resource.ResourceInfo.TextureSpec.PerformTransition = false;
                    resource.ResourceInfo.TextureHandle                 = Textures::Texture2D::Create(resource.ResourceInfo.TextureSpec);
                    pass_builder.UseRenderTarget(resource.ResourceInfo.TextureHandle);
                }
            }

            for (auto& input : node.Creation.Inputs)
            {
                auto& resource = m_resource_map[input.Name];

                if (input.Type == RenderGraphResourceType::ATTACHMENT)
                {
                    pass_builder.AddInputAttachment(resource.ResourceInfo.TextureHandle);
                }
                else if (input.Type == RenderGraphResourceType::TEXTURE)
                {
                    pass_builder.AddInputTexture(input.BindingInputKeyName, resource.ResourceInfo.TextureHandle);
                }
            }

            node.CallbackPass->Compile(node.Handle, pass_builder, *this);
        }
    }

    void RenderGraph::Execute(uint32_t frame_index, Buffers::CommandBuffer* const command_buffer, Rendering::Scenes::SceneRawData* const scene_data)
    {
        ZENGINE_VALIDATE_ASSERT(command_buffer, "Command Buffer can't be null")
        command_buffer->ClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        command_buffer->ClearDepth(1.0f, 0);

        command_buffer->Begin();
        for (auto& node_name : m_sorted_nodes)
        {
            auto& node = m_node[node_name];

            for (auto& input : node.Creation.Inputs)
            {
                if (input.Type == RenderGraphResourceType::TEXTURE)
                {
                    auto& resource = m_resource_map[input.Name];
                    /*
                     * The input texture can from an attachment that should read as Shader Sampler2D data
                     * So we need ensure the right config for transition
                     */
                    bool is_resource_attachment = resource.Type == RenderGraphResourceType::ATTACHMENT;

                    auto& buffer = resource.ResourceInfo.TextureHandle->GetBuffer();

                    Specifications::ImageMemoryBarrierSpecification barrier_spec = {};
                    barrier_spec.ImageHandle                                     = buffer.Handle;
                    barrier_spec.OldLayout             = is_resource_attachment ? Specifications::ImageLayout::COLOR_ATTACHMENT_OPTIMAL : Specifications::ImageLayout::UNDEFINED;
                    barrier_spec.NewLayout             = Specifications::ImageLayout::SHADER_READ_ONLY_OPTIMAL;
                    barrier_spec.ImageAspectMask       = VK_IMAGE_ASPECT_COLOR_BIT;
                    barrier_spec.SourceAccessMask      = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    barrier_spec.DestinationAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    barrier_spec.SourceStageMask       = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                    barrier_spec.DestinationStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    barrier_spec.LayerCount            = 1;

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

                auto& buffer = resource.ResourceInfo.TextureHandle->GetBuffer();

                Specifications::ImageMemoryBarrierSpecification barrier_spec = {};
                if (resource.ResourceInfo.TextureHandle->IsDepthTexture())
                {
                    barrier_spec.ImageHandle           = buffer.Handle;
                    barrier_spec.OldLayout             = Specifications::ImageLayout::UNDEFINED;
                    barrier_spec.NewLayout             = Specifications::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    barrier_spec.ImageAspectMask       = VkImageAspectFlagBits(VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
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
        }
        command_buffer->End();
    }

    void RenderGraph::Resize(uint32_t width, uint32_t height)
    {
        for (auto& node_name : m_sorted_nodes)
        {
            auto& node = m_node[node_name];

            auto& pass_spec = node.Handle->GetSpecification();
            pass_spec.ExternalOutputs.clear();
            pass_spec.ExternalOutputs.shrink_to_fit();

            pass_spec.Inputs.clear();
            pass_spec.Inputs.shrink_to_fit();

            pass_spec.InputTextures.clear();

            for (auto& output : node.Creation.Outputs)
            {
                auto& resource = m_resource_map[output.Name];

                if (output.Type == RenderGraphResourceType::REFERENCE)
                {
                    continue;
                }

                resource.ResourceInfo.TextureHandle->Dispose();

                resource.ResourceInfo.TextureSpec.Width  = width;
                resource.ResourceInfo.TextureSpec.Height = height;
                resource.ResourceInfo.TextureHandle      = Textures::Texture2D::Create(resource.ResourceInfo.TextureSpec);

                pass_spec.ExternalOutputs.push_back(resource.ResourceInfo.TextureHandle);
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

            node.Handle->ResizeFramebuffer();
            node.Handle->UpdateInputBinding();
        }
    }

    void RenderGraph::Dispose()
    {
        for (auto& node_name : m_sorted_nodes)
        {
            auto& node = m_node[node_name];
            node.Handle->Dispose();
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
                    value.ResourceInfo.TextureHandle->Dispose();
                    value.ResourceInfo.TextureHandle = nullptr;
                }
            }
            else if (value.Type == RenderGraphResourceType::BUFFER_SET)
            {
                if (value.ResourceInfo.BufferSetHandle)
                {
                    value.ResourceInfo.BufferSetHandle->Dispose();
                    value.ResourceInfo.BufferSetHandle = nullptr;
                }
                else if (value.ResourceInfo.UniformBufferSetHandle)
                {
                    value.ResourceInfo.UniformBufferSetHandle->Dispose();
                    value.ResourceInfo.UniformBufferSetHandle = nullptr;
                }
                else if (value.ResourceInfo.IndirectBufferSetHandle)
                {
                    value.ResourceInfo.IndirectBufferSetHandle->Dispose();
                    value.ResourceInfo.IndirectBufferSetHandle = nullptr;
                }
            }
        }
    }

    Ref<RenderGraphBuilder> RenderGraph::GetBuilder() const
    {
        return m_builder;
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

    Ref<Textures::Texture> RenderGraph::GetRenderTarget(std::string_view name)
    {
        std::string resource_name(name);
        if (!m_resource_map.contains(resource_name))
        {
            m_resource_map[resource_name].Name = name.data();
        }
        if (m_resource_map[resource_name].Type != RenderGraphResourceType::ATTACHMENT)
        {
            ZENGINE_CORE_WARN("{} isn't a valid Attachement Resource", name.data())
        }
        return m_resource_map[resource_name].ResourceInfo.TextureHandle;
    }

    Ref<Textures::Texture> RenderGraph::GetTexture(std::string_view name)
    {
        std::string resource_name(name);
        if (!m_resource_map.contains(resource_name))
        {
            m_resource_map[resource_name].Name = name.data();
        }
        if (m_resource_map[resource_name].Type != RenderGraphResourceType::TEXTURE)
        {
            ZENGINE_CORE_WARN("{} isn't a valid Texture Resource", name.data())
        }
        return m_resource_map[resource_name].ResourceInfo.TextureHandle;
    }

    Ref<Buffers::StorageBufferSet> RenderGraph::GetBufferSet(std::string_view name)
    {
        std::string resource_name(name);
        if (!m_resource_map.contains(resource_name))
        {
            m_resource_map[resource_name].Name = name.data();
        }
        return m_resource_map[resource_name].ResourceInfo.BufferSetHandle;
    }

    Ref<Buffers::UniformBufferSet> RenderGraph::GetBufferUniformSet(std::string_view name)
    {
        std::string resource_name(name);
        if (!m_resource_map.contains(resource_name))
        {
            m_resource_map[resource_name].Name = name.data();
        }
        return m_resource_map[resource_name].ResourceInfo.UniformBufferSetHandle;
    }

    Ref<Buffers::IndirectBufferSet> RenderGraph::GetIndirectBufferSet(std::string_view name)
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

    void RenderGraph::AddCallbackPass(std::string_view pass_name, const Ref<IRenderGraphCallbackPass>& pass_callback)
    {
        std::string resource_name(pass_name);

        m_node[resource_name].CallbackPass = pass_callback;
    }
} // namespace ZEngine::Rendering::Renderers
