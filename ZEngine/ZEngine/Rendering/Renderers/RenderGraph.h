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

namespace ZEngine::Rendering::Renderers
{
    struct RenderGraphResourceBuilder;
    struct RenderGraphResourceInspector;
    struct RenderGraphNode;
    struct RenderGraph;

    ZDEFINE_PTR(RenderGraphResourceBuilder);
    ZDEFINE_PTR(RenderGraphResourceInspector);
    ZDEFINE_PTR(RenderGraph);

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
        cstring                 Name;
        cstring                 ProducerNodeName;
        RenderGraphResourceType Type;
        RenderGraphResourceInfo ResourceInfo;
    };

    struct RenderGraphRenderPassInputOutputInfo
    {
        cstring                 Name;
        cstring                 BindingInputKeyName;
        RenderGraphResourceType Type = RenderGraphResourceType::ATTACHMENT;
    };

    struct RenderGraphRenderPassCreation
    {
        cstring                                                       Name;
        Core::Containers::Array<RenderGraphRenderPassInputOutputInfo> Inputs;
        Core::Containers::Array<RenderGraphRenderPassInputOutputInfo> Outputs;
    };

    struct IRenderGraphCallbackPass
    {
        virtual void Setup(std::string_view name, RenderGraph* const graph)                                                                                                                                                          = 0;
        virtual void Compile(RenderPasses::RenderPass** const pass, RenderGraph* const graph)                                                                                                                                        = 0;
        virtual void Execute(Rendering::Scenes::SceneData* const scene, RenderPasses::RenderPass* const pass, Hardwares::CommandBuffer* const command_buffer, RenderGraph* const graph)                                              = 0;
        virtual void Render(Rendering::Scenes::SceneData* const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBuffer* const command_buffer, RenderGraph* const graph) = 0;
    };

    struct RenderGraphNode
    {
        bool                             Enabled       = true;
        RenderGraphRenderPassCreation    Creation      = {};
        Core::Containers::Array<cstring> EdgeNodes     = {};
        ZRawPtr(RenderPasses::RenderPass) Handle       = nullptr;
        ZRawPtr(Buffers::FramebufferVNext) Framebuffer = nullptr;
        ZRawPtr(IRenderGraphCallbackPass) CallbackPass = nullptr;
    };

    struct RenderGraph
    {
        RenderGraph() {}
        ~RenderGraph() {}

        bool                                                    MarkAsDirty       = false;

        Hardwares::VulkanDevicePtr                              Device            = nullptr;

        Core::Containers::Array<cstring>                        SortedNodesMap    = {};
        Core::Containers::HashMap<cstring, RenderGraphNode>     NodeMap           = {};
        Core::Containers::HashMap<cstring, RenderGraphResource> ResourceMap       = {};

        RenderGraphResourceBuilderPtr                           ResourceBuilder   = nullptr;
        RenderGraphResourceInspectorPtr                         ResourceInspector = nullptr;
        RenderPasses::RenderPassBuilder*                        RenderPassBuilder = nullptr;

        void                                                    Initialize(Hardwares::VulkanDevicePtr device);

        void                                                    Setup();
        void                                                    Compile();
        void                                                    Execute(Hardwares::CommandBuffer* const command_buffer, Rendering::Scenes::SceneData* const scene_data);
        void                                                    Resize(uint32_t width, uint32_t height);
        void                                                    Dispose();
        void                                                    AddCallbackPass(cstring pass_name, IRenderGraphCallbackPass* const pass_callback, bool enabled = true);
    };

    struct RenderGraphResourceInspector
    {
        RenderGraphPtr                     Graph = nullptr;

        void                               Initialize(RenderGraphPtr graph);

        RenderGraphResource&               GetResource(cstring name);
        Textures::TextureHandle            GetRenderTarget(cstring name);
        Textures::TextureHandle            GetTexture(cstring name);
        Hardwares::StorageBufferSetHandle  GetStorageBufferSet(cstring name);
        Hardwares::VertexBufferSetHandle   GetVertexBufferSet(cstring name);
        Hardwares::IndexBufferSetHandle    GetIndexBufferSet(cstring name);
        Hardwares::UniformBufferSetHandle  GetBufferUniformSet(cstring name);
        Hardwares::IndirectBufferSetHandle GetIndirectBufferSet(cstring name);
        RenderGraphNode&                   GetNode(cstring name);
    };

    struct RenderGraphResourceBuilder
    {
        RenderGraphPtr       Graph = nullptr;

        void                 Initialize(RenderGraphPtr graph);

        RenderGraphResource& CreateTexture(cstring name, const Specifications::TextureSpecification& spec);
        RenderGraphResource& CreateTexture(cstring name, cstring filename);
        RenderGraphResource& CreateRenderTarget(cstring name, const Specifications::TextureSpecification& spec);
        RenderGraphResource& AttachBuffer(cstring name, const Hardwares::StorageBufferSetHandle& buffer);
        RenderGraphResource& AttachBuffer(cstring name, const Hardwares::UniformBufferSetHandle& buffer);
        RenderGraphResource& AttachTexture(cstring name, const Textures::TextureHandle& texture);
        RenderGraphResource& AttachRenderTarget(cstring name, const Textures::TextureHandle& texture);
        void                 CreateRenderPassNode(const RenderGraphRenderPassCreation&);

        RenderGraphResource& CreateBuffer(cstring name) = delete;
        RenderGraphResource& CreateBufferSet(cstring name, BufferSetCreationType type = BufferSetCreationType::STORAGE);
    };
} // namespace ZEngine::Rendering::Renderers