#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Hardwares/DeferredFreeQueue.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Buffers/Framebuffer.h>
#include <ZEngine/Rendering/Renderers/RenderPasses/RenderPass.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <ZEngine/Rendering/Specifications/TextureSpecification.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <ZEngine/ZEngineDef.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Renderers
{
    struct RenderGraphResourceBuilder;
    struct RenderGraphResourceInspector;
    struct RenderGraph;
    struct IRenderGraphCallbackPass;

    ZDEFINE_PTR(RenderGraphResourceBuilder);
    ZDEFINE_PTR(RenderGraphResourceInspector);
    ZDEFINE_PTR(RenderGraph);
    ZDEFINE_PTR(IRenderGraphCallbackPass);

    // Typed index into RenderGraph::Resources[]. No string on the execute hot path.
    struct RGResourceHandle
    {
        uint32_t Index   = UINT32_MAX;
        uint32_t Version = 0;

        bool     Valid() const
        {
            return Index != UINT32_MAX;
        }
    };

    enum class RGResourceKind : uint8_t
    {
        Attachment,
        Texture,
        Buffer,
    };

    // How a pass uses a resource — drives barrier stage/access/layout derivation.
    enum class RGAccess : uint8_t
    {
        None,
        ColorWrite,
        DepthWrite,
        DepthRead,
        ShaderRead,
        ShaderReadWrite,
        TransferRead,
        TransferWrite,
        Present,
        Count_,
    };

    struct RGResourceState
    {
        VkPipelineStageFlags Stage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkAccessFlags        Access = 0;
        VkImageLayout        Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    struct RGResource
    {
        cstring                              Name           = nullptr;
        RGResourceKind                       Kind           = RGResourceKind::Attachment;
        bool                                 External       = false;
        Textures::TextureHandle              TextureHandle  = {};
        RGResourceState                      CurrentState   = {}; // compile-time simulation
        RGResourceState                      RuntimeState   = {}; // per-frame Execute tracking
        uint32_t                             FirstPassIndex = UINT32_MAX;
        uint32_t                             LastPassIndex  = 0;
        bool                                 Transient      = true;
        Specifications::TextureSpecification Spec           = {};
    };

    struct RGPassResource
    {
        RGResourceHandle Handle     = {};
        RGAccess         Access     = RGAccess::None;
        cstring          BindingKey = nullptr;
    };

    struct RGPass
    {
        cstring                   Name                                = nullptr;
        bool                      Enabled                             = true;
        IRenderGraphCallbackPass* Callback                            = nullptr;
        RenderPasses::RenderPass* Handle                              = nullptr;
        ZRawPtr(Buffers::FramebufferVNext) Framebuffer                = nullptr;
        Core::Containers::Array<RGPassResource>       Reads           = {};
        Core::Containers::Array<RGPassResource>       Writes          = {};
        Core::Containers::Array<VkImageMemoryBarrier> ImageBarriers   = {};
        VkPipelineStageFlags                          BarrierSrcStage = 0;
        VkPipelineStageFlags                          BarrierDstStage = 0;
    };

    struct RGTransientSlot
    {
        Textures::TextureHandle              Handle        = {};
        Specifications::TextureSpecification Spec          = {};
        uint32_t                             FreeAfterPass = 0;
    };

    struct RGTransientPool
    {
        Core::Containers::Array<RGTransientSlot> Slots;

        void                                     Initialize(Core::Memory::ArenaAllocator* arena);
        Textures::TextureHandle                  TryAlias(const Specifications::TextureSpecification& spec, uint32_t first_pass);
        void                                     Register(Textures::TextureHandle handle, const Specifications::TextureSpecification& spec, uint32_t last_pass);
        void                                     MarkInUse(Textures::TextureHandle handle, uint32_t last_pass);
        void                                     Clear();
    };

    // Unchanged interface — all existing pass implementations compile without modification.
    struct IRenderGraphCallbackPass
    {
        virtual void Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)                                                                                                                       = 0;
        virtual void Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)                                          = 0;
        virtual void Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) = 0;
        virtual void Deinitialize(Hardwares::VulkanDevicePtr const device) {}
    };

    struct RenderGraph
    {
        RenderGraph()                                                   = default;
        ~RenderGraph()                                                  = default;

        Hardwares::VulkanDevicePtr                            Device    = nullptr;
        Scenes::SceneDataPtr                                  SceneData = nullptr;

        Core::Containers::Array<RGPass>                       Passes;
        Core::Containers::Array<RGResource>                   Resources;
        Core::Containers::Array<uint32_t>                     SortedPassIndices;

        // String → index: used only in Setup/Compile, not in Execute.
        Core::Containers::UnorderedHashMap<cstring, uint32_t> ResourceIndex;
        Core::Containers::UnorderedHashMap<cstring, uint32_t> PassIndex;

        RenderGraphResourceBuilderPtr                         ResourceBuilder   = nullptr;
        RenderGraphResourceInspectorPtr                       ResourceInspector = nullptr;
        RenderPasses::RenderPassBuilder*                      RenderPassBuilder = nullptr;

        RGTransientPool                                       TransientPool;

        void                                                  Initialize(Hardwares::VulkanDevicePtr device, Scenes::SceneDataPtr data = nullptr);
        void                                                  AddCallbackPass(cstring pass_name, IRenderGraphCallbackPass* const cb, bool enabled = true);
        void                                                  Setup();
        void                                                  Compile();
        void                                                  Execute(Hardwares::CommandBufferPtr const cb);
        void                                                  Resize(uint32_t width, uint32_t height);
        void                                                  Dispose();

        RGResourceHandle                                      ImportRenderTarget(cstring name, Textures::TextureHandle handle);

        // Access a pass by name — O(1) lookup via PassIndex; setup/config only, not Execute.
        RGPass*                                               GetPass(cstring name);
        void                                                  SetPassEnabled(cstring name, bool enabled);

    private:
        void BuildLifetimes();
        void AllocateTransientResources();
        void BuildBarriers();
        void BuildTopology();
        void AllocateFramebuffers();
    };

    // Pass-facing API — replaces RenderGraphResourceBuilder call sites in Setup().
    struct RenderGraphResourceBuilder
    {
        RenderGraph*     Graph       = nullptr;
        uint32_t         CurrentPass = UINT32_MAX;

        void             Initialize(RenderGraph* graph);

        // Declare that the current pass writes a transient color attachment.
        RGResourceHandle WriteColorAttachment(cstring name, const Specifications::TextureSpecification& spec);

        // Declare that the current pass writes a transient depth attachment.
        RGResourceHandle WriteDepthAttachment(cstring name, const Specifications::TextureSpecification& spec);

        // Declare that the current pass reads a resource as a sampled texture.
        RGResourceHandle ReadTexture(cstring name, cstring binding_key = nullptr);

        // Declare that the current pass reads a depth resource (read-only).
        RGResourceHandle ReadDepth(cstring name);

        // Import an externally-managed render target (not owned by the graph).
        RGResourceHandle ImportRenderTarget(cstring name, Textures::TextureHandle handle);

        // Attach an already-imported render target by name — looks up by name only.
        RGResourceHandle AttachRenderTarget(cstring name, const Textures::TextureHandle& texture);
    };

    // Pass-facing read API — replaces RenderGraphResourceInspector call sites in Execute().
    struct RenderGraphResourceInspector
    {
        RenderGraph*            Graph = nullptr;

        void                    Initialize(RenderGraph* graph);

        // Retrieve a texture handle by RGResourceHandle (O(1), no string).
        Textures::TextureHandle GetTextureHandle(RGResourceHandle handle) const;

        // String-keyed overloads — preserved for existing Execute() call sites.
        Textures::TextureHandle GetRenderTarget(cstring name) const;
        Textures::TextureHandle GetTexture(cstring name) const;
    };

} // namespace ZEngine::Rendering::Renderers
