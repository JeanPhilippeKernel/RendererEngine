#pragma once
#include <Buffers/Framebuffer.h>
#include <Core/Containers/Array.h>
#include <Core/Containers/HashMap.h>
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>
#include <Rendering/Scenes/GraphicScene.h>
#include <Rendering/Specifications/TextureSpecification.h>
#include <Rendering/Textures/Texture.h>
#include <ZEngineDef.h>
#include <functional>
#include <string>

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
        union
        {
            Textures::TextureHandle            TextureHandle;
            Hardwares::UniformBufferSetHandle  UniformBufferSetHandle;
            Hardwares::StorageBufferSetHandle  StorageBufferSetHandle;
            Hardwares::IndirectBufferSetHandle IndirectBufferSetHandle;
            Hardwares::VertexBufferSetHandle   VertexBufferSetHandle;
            Hardwares::IndexBufferSetHandle    IndexBufferSetHandle;
        };
    };

    struct RenderGraphResource
    {
        const char*             Name;
        const char*             ProducerNodeName;
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
        const char*                                                   Name;
        Core::Containers::Array<RenderGraphRenderPassInputOutputInfo> Inputs;
        Core::Containers::Array<RenderGraphRenderPassInputOutputInfo> Outputs;
    };

    struct IRenderGraphCallbackPass
    {
        virtual void Setup(std::string_view name, RenderGraph* const graph)                                                                                                                                                                                   = 0;
        virtual void Compile(RenderPasses::RenderPass** const pass, RenderGraph* const graph, Rendering::Scenes::SceneRawData* const scene)                                                                                                                   = 0;
        virtual void Execute(uint32_t frame_index, Rendering::Scenes::SceneRawData* const scene, RenderPasses::RenderPass* const pass, Hardwares::CommandBuffer* const command_buffer, RenderGraph* const graph)                                              = 0;
        virtual void Render(uint32_t frame_index, Rendering::Scenes::SceneRawData* const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBuffer* const command_buffer, RenderGraph* const graph) = 0;
    };

    struct RenderGraphNode
    {
        bool                                 Enabled   = true;
        RenderGraphRenderPassCreation        Creation  = {};
        Core::Containers::Array<const char*> EdgeNodes = {};
        ZRawPtr(RenderPasses::RenderPass) Handle       = nullptr;
        ZRawPtr(Buffers::FramebufferVNext) Framebuffer = nullptr;
        ZRawPtr(IRenderGraphCallbackPass) CallbackPass = nullptr;
    };

    class RenderGraph
    {
    public:
        RenderGraph() {}
        ~RenderGraph() {}

        bool                               MarkAsDirty       = false;
        GraphicRenderer*                   Renderer          = nullptr;
        RenderGraphBuilder*                Builder           = nullptr;
        RenderPasses::RenderPassBuilder*   RenderPassBuilder = nullptr;

        void                               Initialize(Core::Memory::ArenaAllocator* arena, GraphicRenderer* renderer);

        void                               Setup();
        void                               Compile(Rendering::Scenes::SceneRawData* const scene_data);
        void                               Execute(uint32_t frame_index, Hardwares::CommandBuffer* const command_buffer, Rendering::Scenes::SceneRawData* const scene_data);
        void                               Resize(uint32_t width, uint32_t height);
        void                               Dispose();
        RenderGraphResource&               GetResource(const char*);
        Textures::TextureHandle            GetRenderTarget(const char*);
        Textures::TextureHandle            GetTexture(const char*);
        Hardwares::StorageBufferSetHandle  GetStorageBufferSet(const char*);
        Hardwares::VertexBufferSetHandle   GetVertexBufferSet(const char*);
        Hardwares::IndexBufferSetHandle    GetIndexBufferSet(const char*);
        Hardwares::UniformBufferSetHandle  GetBufferUniformSet(const char*);
        Hardwares::IndirectBufferSetHandle GetIndirectBufferSet(const char*);
        RenderGraphNode&                   GetNode(const char*);
        void                               AddCallbackPass(const char* pass_name, IRenderGraphCallbackPass* const pass_callback, bool enabled = true);

    private:
        Core::Containers::Array<const char*>                        m_sorted_nodes;
        Core::Containers::HashMap<const char*, RenderGraphNode>     m_node;
        Core::Containers::HashMap<const char*, RenderGraphResource> m_resource_map;
        friend struct RenderGraphBuilder;
    };

    struct RenderGraphBuilder
    {
        RenderGraphBuilder(RenderGraph& graph) : m_graph(graph) {}

        RenderGraphResource& CreateTexture(const char* name, const Specifications::TextureSpecification& spec);
        RenderGraphResource& CreateTexture(const char* name, const char* filename);
        RenderGraphResource& CreateRenderTarget(const char* name, const Specifications::TextureSpecification& spec);
        RenderGraphResource& AttachBuffer(const char* name, const Hardwares::StorageBufferSetHandle& buffer);
        RenderGraphResource& AttachBuffer(const char* name, const Hardwares::UniformBufferSetHandle& buffer);
        RenderGraphResource& AttachTexture(const char* name, const Textures::TextureHandle& texture);
        RenderGraphResource& AttachRenderTarget(const char* name, const Textures::TextureHandle& texture);
        void                 CreateRenderPassNode(const RenderGraphRenderPassCreation&);

        RenderGraphResource& CreateBuffer(const char* name) = delete;
        RenderGraphResource& CreateBufferSet(const char* name, BufferSetCreationType type = BufferSetCreationType::STORAGE);

    private:
        RenderGraph& m_graph;
    };
} // namespace ZEngine::Rendering::Renderers