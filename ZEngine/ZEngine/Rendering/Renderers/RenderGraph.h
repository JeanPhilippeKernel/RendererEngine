#pragma once
#include <Helpers/IntrusivePtr.h>
#include <Rendering/Buffers/IndirectBuffer.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>
#include <Rendering/Scenes/GraphicScene.h>
#include <Rendering/Specifications/TextureSpecification.h>
#include <Rendering/Textures/Texture.h>
#include <ZEngineDef.h>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ZEngine::Rendering::Renderers
{
    enum RenderGraphResourceType
    {
        UNDEFINED = -1,
        BUFFER    = 0,
        BUFFER_SET,
        ATTACHMENT,
        TEXTURE,
        REFERENCE
    };

    enum BufferSetCreationType
    {
        INDIRECT,
        UNIFORM,
        STORAGE
    };

    struct RenderGraphResourceInfo
    {
        bool                                 External = false;
        Specifications::TextureSpecification TextureSpec;
        Ref<Textures::Texture>               TextureHandle;
        Ref<Buffers::UniformBufferSet>       UniformBufferSetHandle;
        Ref<Buffers::StorageBufferSet>       BufferSetHandle;
        Ref<Buffers::IndirectBufferSet>      IndirectBufferSetHandle;
    };

    struct RenderGraphResource
    {
        const char*             Name;
        std::string             ProducerNodeName;
        RenderGraphResourceType Type;
        RenderGraphResourceInfo ResourceInfo;
    };

    struct RenderGraphRenderPassInputOutputInfo
    {
        const char*             Name;
        const char*             BindingInputKeyName;
        RenderGraphResourceType Type = RenderGraphResourceType::ATTACHMENT;
    };

    struct RenderGraphRenderPassCreation
    {
        const char*                                       Name;
        std::vector<RenderGraphRenderPassInputOutputInfo> Inputs;
        std::vector<RenderGraphRenderPassInputOutputInfo> Outputs;
    };

    struct RenderGraphBuilder;
    struct RenderGraphNode;
    struct RenderGraph;

    struct IRenderGraphCallbackPass : public Helpers::RefCounted
    {
        virtual void Setup(std::string_view name, RenderGraphBuilder* const builder)                                              = 0;
        virtual void Compile(Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder, RenderGraph& graph) = 0;
        virtual void Execute(
            uint32_t                               frame_index,
            Rendering::Scenes::SceneRawData* const scene_data,
            RenderPasses::RenderPass*              pass,
            Buffers::CommandBuffer*                command_buffer,
            RenderGraph* const                     graph) = 0;
    };

    struct RenderGraphNode
    {
        RenderGraphRenderPassCreation   Creation;
        std::unordered_set<std::string> EdgeNodes;
        Ref<RenderPasses::RenderPass>   Handle;
        Ref<IRenderGraphCallbackPass>   CallbackPass;
    };

    class RenderGraph : public Helpers::RefCounted
    {
    public:
        RenderGraph() : m_builder(CreateRef<RenderGraphBuilder>(*this)), m_render_pass_builder(new RenderPasses::RenderPassBuilder{}) {}
        ~RenderGraph() = default;

        void Setup();
        void Compile();
        void Execute(uint32_t frame_index, Buffers::CommandBuffer* const command_buffer, Rendering::Scenes::SceneRawData* const scene_data);

        void Resize(uint32_t width, uint32_t height);

        void Dispose();

        RenderGraphResource&            GetResource(std::string_view);
        Ref<Textures::Texture>          GetRenderTarget(std::string_view);
        Ref<Textures::Texture>          GetTexture(std::string_view);
        Ref<Buffers::StorageBufferSet>  GetBufferSet(std::string_view);
        Ref<Buffers::UniformBufferSet>  GetBufferUniformSet(std::string_view);
        Ref<Buffers::IndirectBufferSet> GetIndirectBufferSet(std::string_view);

        Ref<RenderGraphBuilder> GetBuilder() const;
        RenderGraphNode&        GetNode(std::string_view);
        void                    AddCallbackPass(std::string_view pass_name, const Ref<IRenderGraphCallbackPass>& pass_callback);

    private:
        std::map<std::string, RenderGraphNode>     m_node;
        std::vector<std::string>                   m_sorted_nodes;
        std::map<std::string, RenderGraphResource> m_resource_map;
        Ref<RenderGraphBuilder>                    m_builder;
        Ref<RenderPasses::RenderPassBuilder>       m_render_pass_builder;

        friend struct RenderGraphBuilder;
    };

    struct RenderGraphBuilder : public Helpers::RefCounted
    {
        RenderGraphBuilder(RenderGraph& graph) : m_graph(graph) {}

        RenderGraphResource& CreateTexture(std::string_view name, const Specifications::TextureSpecification& spec);
        RenderGraphResource& CreateRenderTarget(std::string_view name, const Specifications::TextureSpecification& spec);
        RenderGraphResource& AttachBuffer(std::string_view name, const Ref<Buffers::StorageBufferSet>& buffer);
        RenderGraphResource& AttachBuffer(std::string_view name, const Ref<Buffers::UniformBufferSet>& buffer);
        RenderGraphResource& AttachTexture(std::string_view name, const Ref<Textures::Texture>& texture);
        void                 CreateRenderPassNode(const RenderGraphRenderPassCreation&);

        RenderGraphResource& CreateBuffer(std::string_view name) = delete;
        RenderGraphResource& CreateBufferSet(std::string_view name, uint32_t count = 1, BufferSetCreationType type = BufferSetCreationType::STORAGE);

    private:
        RenderGraph& m_graph;
    };
} // namespace ZEngine::Rendering::Renderers