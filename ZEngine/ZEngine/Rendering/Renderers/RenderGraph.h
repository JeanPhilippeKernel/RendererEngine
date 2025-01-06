#pragma once
#include <Buffers/Framebuffer.h>
#include <Helpers/IntrusivePtr.h>
#include <Rendering/Buffers/IndexBuffer.h>
#include <Rendering/Buffers/IndirectBuffer.h>
#include <Rendering/Buffers/VertexBuffer.h>
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
    struct GraphicRenderer;
    struct RenderGraphBuilder;
    struct RenderGraphNode;
    struct RenderGraph;

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
        STORAGE,
        VERTEX,
        INDEX
    };

    struct RenderGraphResourceInfo
    {
        bool                                 External = false;
        Specifications::TextureSpecification TextureSpec;
        Textures::TextureHandle              TextureHandle;
        Buffers::UniformBufferSetHandle      UniformBufferSetHandle;
        Buffers::StorageBufferSetHandle      StorageBufferSetHandle;
        Buffers::IndirectBufferSetHandle     IndirectBufferSetHandle;
        Buffers::VertexBufferSetHandle       VertexBufferSetHandle;
        Buffers::IndexBufferSetHandle        IndexBufferSetHandle;
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

    struct IRenderGraphCallbackPass : public Helpers::RefCounted
    {
        virtual void Setup(std::string_view name, RenderGraph* const graph)                                                                                                                                         = 0;
        virtual void Compile(Helpers::Ref<RenderPasses::RenderPass>& pass, RenderGraph* const graph)                                                                                                                = 0;
        virtual void Execute(uint32_t frame_index, Rendering::Scenes::SceneRawData* const scene_data, RenderPasses::RenderPass* const pass, Buffers::CommandBuffer* const command_buffer, RenderGraph* const graph) = 0;
        virtual void Render(uint32_t frame_index, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Buffers::CommandBuffer* const command_buffer, RenderGraph* const graph)       = 0;
    };

    struct RenderGraphNode
    {
        bool                                    Enabled      = true;
        RenderGraphRenderPassCreation           Creation     = {};
        std::unordered_set<std::string>         EdgeNodes    = {};
        Helpers::Ref<RenderPasses::RenderPass>  Handle       = nullptr;
        Helpers::Ref<Buffers::FramebufferVNext> Framebuffer  = nullptr;
        Helpers::Ref<IRenderGraphCallbackPass>  CallbackPass = nullptr;
    };

    class RenderGraph : public Helpers::RefCounted
    {
    public:
        RenderGraph(GraphicRenderer* renderer) : Renderer(renderer), Builder(Helpers::CreateRef<RenderGraphBuilder>(*this)) {}
        ~RenderGraph()                                                  = default;

        GraphicRenderer*                              Renderer          = nullptr;
        Helpers::Ref<RenderGraphBuilder>              Builder           = nullptr;
        Helpers::Ref<RenderPasses::RenderPassBuilder> RenderPassBuilder = Helpers::CreateRef<RenderPasses::RenderPassBuilder>();

        void                                          Setup();
        void                                          Compile();
        void                                          Execute(uint32_t frame_index, Buffers::CommandBuffer* const command_buffer, Rendering::Scenes::SceneRawData* const scene_data);
        void                                          Resize(uint32_t width, uint32_t height);
        void                                          Dispose();
        RenderGraphResource&                          GetResource(std::string_view);
        Textures::TextureHandle                       GetRenderTarget(std::string_view);
        Textures::TextureHandle                       GetTexture(std::string_view);
        Buffers::StorageBufferSetHandle               GetStorageBufferSet(std::string_view);
        Buffers::VertexBufferSetHandle                GetVertexBufferSet(std::string_view);
        Buffers::IndexBufferSetHandle                 GetIndexBufferSet(std::string_view);
        Buffers::UniformBufferSetHandle               GetBufferUniformSet(std::string_view);
        Buffers::IndirectBufferSetHandle              GetIndirectBufferSet(std::string_view);
        RenderGraphNode&                              GetNode(std::string_view);
        void                                          AddCallbackPass(std::string_view pass_name, const Helpers::Ref<IRenderGraphCallbackPass>& pass_callback, bool enabled = true);

    private:
        std::vector<std::string>                   m_sorted_nodes;
        std::map<std::string, RenderGraphNode>     m_node;
        std::map<std::string, RenderGraphResource> m_resource_map;
        friend struct RenderGraphBuilder;
    };

    struct RenderGraphBuilder : public Helpers::RefCounted
    {
        RenderGraphBuilder(RenderGraph& graph) : m_graph(graph) {}

        RenderGraphResource& CreateTexture(std::string_view name, const Specifications::TextureSpecification& spec);
        RenderGraphResource& CreateTexture(std::string_view name, std::string_view filename);
        RenderGraphResource& CreateRenderTarget(std::string_view name, const Specifications::TextureSpecification& spec);
        RenderGraphResource& AttachBuffer(std::string_view name, const Buffers::StorageBufferSetHandle& buffer);
        RenderGraphResource& AttachBuffer(std::string_view name, const Buffers::UniformBufferSetHandle& buffer);
        RenderGraphResource& AttachTexture(std::string_view name, const Textures::TextureHandle& texture);
        RenderGraphResource& AttachRenderTarget(std::string_view name, const Textures::TextureHandle& texture);
        void                 CreateRenderPassNode(const RenderGraphRenderPassCreation&);

        RenderGraphResource& CreateBuffer(std::string_view name) = delete;
        RenderGraphResource& CreateBufferSet(std::string_view name, BufferSetCreationType type = BufferSetCreationType::STORAGE);

    private:
        RenderGraph& m_graph;
    };
} // namespace ZEngine::Rendering::Renderers