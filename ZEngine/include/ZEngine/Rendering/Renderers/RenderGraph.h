#pragma once
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <ZEngineDef.h>
#include <Helpers/IntrusivePtr.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>
#include <Rendering/Specifications/TextureSpecification.h>
#include <Rendering/Textures/Texture.h>
#include <Rendering/Scenes/GraphicScene.h>

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

    struct RenderGraphResourceInfo
    {
        bool                                 External = false;
        Specifications::TextureSpecification TextureSpec;
        Ref<Textures::Texture>               TextureHandle;
        Ref<Buffers::UniformBufferSet>       UniformBufferSetHandle;
        Ref<Buffers::StorageBufferSet>       BufferSetHandle;
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

    using SetupPassCallback   = std::function<void(std::string_view name, RenderGraphBuilder* const builder)>;
    using CompilePassCallback = std::function<void(Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder)>;
    using ExecutePassCallback = std::function<void(uint32_t frame_index, Rendering::Scenes::SceneRawData* const scene_data, RenderPasses::RenderPass* pass, Buffers::CommandBuffer* command_buffer)>;

    struct PassCallback
    {
        SetupPassCallback   Setup;
        CompilePassCallback Compile;
        ExecutePassCallback Execute;
    };

    struct RenderGraphNode
    {
        RenderGraphRenderPassCreation Creation;
        PassCallback                  Callback;
        std::vector<std::string>      EdgeNodes;
        Ref<RenderPasses::RenderPass> Handle;
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

        Ref<RenderGraphBuilder> GetBuilder() const;
        RenderGraphResource&    GetResource(std::string_view);
        RenderGraphNode&        GetNode(std::string_view);
        void                    AddCallbackPass(std::string_view pass_name, SetupPassCallback setup_callback, CompilePassCallback compile, ExecutePassCallback execute_callback);

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

        RenderGraphResource CreateBuffer()                                             = delete;
        RenderGraphResource CreateBufferSet(std::string_view name, uint32_t count = 1) = delete;

    private:
        RenderGraph& m_graph;
    };
} // namespace ZEngine::Rendering::Renderers