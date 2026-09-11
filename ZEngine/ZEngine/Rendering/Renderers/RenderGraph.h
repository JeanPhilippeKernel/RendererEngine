#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Hardwares/DeferredFreeQueue.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Buffers/Framebuffer.h>
#include <ZEngine/Rendering/Primitives/Semaphore.h>
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
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
        // Buffer accesses are conservatively scoped to all commands until a pass
        // declares its pipeline domain (graphics/compute/transfer) explicitly.
        BufferRead,
        BufferReadWrite,
        // Consumed by vkCmdDrawIndirect/vkCmdDrawIndexedIndirect. This needs a
        // distinct access mask from a shader storage-buffer read.
        IndirectRead,
        Count_,
    };

    struct RGResourceState
    {
        VkPipelineStageFlags2 Stage  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        VkAccessFlags2        Access = 0;
        VkImageLayout         Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    /// @brief Returns the Vulkan synchronization state for a resource access.
    RGResourceState GetRGAccessState(RGAccess access);

    struct RGResource
    {
        cstring                              Name           = nullptr;
        RGResourceKind                       Kind           = RGResourceKind::Attachment;
        bool                                 External       = false;
        Textures::TextureHandle              TextureHandle  = {};
        // Graph buffers are imported views owned by the existing GPU allocator.
        // Transient buffer allocation is intentionally deferred to the allocator
        // integration slice; no buffer is copied or freed by RenderGraph today.
        const Core::Memory::BufferView*      Buffer         = nullptr;
        // External resources may enter the graph with a known producer state.
        // Graph-owned images begin undefined; imported host buffers begin at the
        // host-write state set by ImportBuffer().
        RGResourceState                      InitialState   = {};
        RGResourceState                      CurrentState   = {}; // compile-time simulation
        RGResourceState                      RuntimeState   = {}; // per-frame Execute tracking
        // Monotonic logical version assigned during Setup(). Versions express
        // dependency order; attachment updates may still share one physical image.
        uint32_t                             LatestVersion  = 0;
        uint32_t                             FirstPassIndex = UINT32_MAX;
        uint32_t                             LastPassIndex  = 0;
        bool                                 Transient      = true;
        Specifications::TextureSpecification Spec           = {};
    };

    struct RGPassResource
    {
        RGResourceHandle              Handle     = {};
        RGAccess                      Access     = RGAccess::None;
        cstring                       BindingKey = nullptr;
        // Relevant to color/depth attachment writes only. It is deliberately a
        // pass-use property: later passes may load the same image after its first
        // writer cleared it.
        Specifications::LoadOperation LoadOp     = Specifications::LoadOperation::CLEAR;
    };

    // Compile-time transition intent. The VkImage and source state deliberately are
    // not stored here: imported resources may change backing image and every
    // resource's old layout is execution-history dependent.
    struct RGImageBarrierPlan
    {
        uint32_t        ResourceIndex    = UINT32_MAX;
        RGResourceState DestinationState = {};
        bool            DiscardContents  = false;
    };

    struct RGBufferBarrierPlan
    {
        uint32_t        ResourceIndex    = UINT32_MAX;
        RGResourceState DestinationState = {};
    };

    // A contiguous run in topological execution order. Queue batches are planned
    // during Compile; the executor consumes them only after it can emit the
    // corresponding release/acquire barriers. The plan always falls back to
    // GRAPHIC_QUEUE when the device has no distinct family.
    struct RGQueueBatch
    {
        Rendering::QueueType Queue          = Rendering::QueueType::GRAPHIC_QUEUE;
        uint32_t             FirstPassOrder = 0;
        uint32_t             PassCount      = 0;
    };

    // A cross-queue resource hazard. Submission of ToBatch must wait for
    // FromBatch's timeline signal; same-queue ordering is implicit.
    struct RGQueueDependency
    {
        uint32_t FromBatch = UINT32_MAX;
        uint32_t ToBatch   = UINT32_MAX;
    };

    // One exclusive-resource ownership hand-off between distinct queue-family
    // batches. Execution emits a release in FromBatch and an acquire in ToBatch.
    struct RGQueueOwnershipTransfer
    {
        uint32_t ResourceIndex = UINT32_MAX;
        uint32_t FromBatch     = UINT32_MAX;
        uint32_t ToBatch       = UINT32_MAX;
    };

    struct RGPass
    {
        cstring                   Name                                  = nullptr;
        bool                      Enabled                               = true;
        IRenderGraphCallbackPass* Callback                              = nullptr;
        RenderPasses::RenderPass* Handle                                = nullptr;
        ZRawPtr(Buffers::FramebufferVNext) Framebuffer                  = nullptr;
        Core::Containers::Array<RGPassResource>      Reads              = {};
        Core::Containers::Array<RGPassResource>      Writes             = {};
        Core::Containers::Array<RGImageBarrierPlan>  BarrierPlans       = {};
        Core::Containers::Array<RGBufferBarrierPlan> BufferBarrierPlans = {};
        // Requested by the pass type, then resolved against the selected GPU's
        // actual queue-family capabilities during Compile().
        Rendering::QueueType                         RequestedQueue     = Rendering::QueueType::GRAPHIC_QUEUE;
        Rendering::QueueType                         Queue              = Rendering::QueueType::GRAPHIC_QUEUE;
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
        Core::Containers::Array<RGQueueBatch>                 QueueBatches;
        Core::Containers::Array<RGQueueDependency>            QueueDependencies;
        Core::Containers::Array<RGQueueOwnershipTransfer>     QueueOwnershipTransfers;
        // Separate timelines per resolved queue type. A single timeline cannot
        // represent independent queues: a signal from compute could overtake a
        // lower-valued transfer signal, making a wait on the shared maximum
        // incorrectly complete before transfer work had finished.
        static constexpr uint32_t                             QueueTimelineCount                      = static_cast<uint32_t>(Rendering::QueueType::COUNT);
        Rendering::Primitives::Semaphore*                     QueueTimelines[QueueTimelineCount]      = {};
        uint64_t                                              QueueTimelineValues[QueueTimelineCount] = {};

        // String → index: used only in Setup/Compile, not in Execute.
        Core::Containers::UnorderedHashMap<cstring, uint32_t> ResourceIndex;
        Core::Containers::UnorderedHashMap<cstring, uint32_t> PassIndex;

        RenderGraphResourceBuilderPtr                         ResourceBuilder   = nullptr;
        RenderGraphResourceInspectorPtr                       ResourceInspector = nullptr;
        RenderPasses::RenderPassBuilder*                      RenderPassBuilder = nullptr;
        // Render-thread owned. SetPassEnabled requests a structural rebuild at the
        // next Execute so a previously disabled pass receives its pass/framebuffer.
        bool                                                  m_needs_recompile = false;
        bool                                                  m_compile_valid   = false;

        RGTransientPool                                       TransientPool;

        void                                                  Initialize(Hardwares::VulkanDevicePtr device, Scenes::SceneDataPtr data = nullptr);
        void                                                  AddCallbackPass(cstring pass_name, IRenderGraphCallbackPass* const cb, bool enabled = true);
        void                                                  Setup();
        void                                                  Compile();
        Hardwares::CommandBuffer*                             Execute(Hardwares::CommandBufferPtr const cb);
        void                                                  Resize(uint32_t width, uint32_t height);
        void                                                  Dispose();

        RGResourceHandle                                      ImportRenderTarget(cstring name, Textures::TextureHandle handle);
        RGResourceHandle                                      ImportBuffer(cstring name, const Core::Memory::BufferView* buffer);
        // Rebind an imported buffer for the active frame without changing the
        // compiled resource declaration or its compile-time barrier state.
        bool                                                  UpdateImportedBuffer(cstring name, const Core::Memory::BufferView* buffer);

        // Access a pass by name — O(1) lookup via PassIndex; setup/config only, not Execute.
        RGPass*                                               GetPass(cstring name);
        void                                                  SetPassEnabled(cstring name, bool enabled);

    private:
        void BuildLifetimes();
        void AllocateTransientResources();
        void BuildBarriers();
        void BuildQueueSchedule();
        // Returns false when the enabled subgraph has a dependency cycle. A cycle
        // has no valid execution order and therefore makes this compile invalid.
        bool BuildTopology();
        void AllocateFramebuffers();
        bool ValidateDeclarations();
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

        // Declare a color attachment update that preserves the preceding pass's
        // contents. `spec.LoadOp` must be LOAD.
        RGResourceHandle UpdateColorAttachment(cstring name, const Specifications::TextureSpecification& spec);

        // Declare that the current pass reads a resource as a sampled texture.
        RGResourceHandle ReadTexture(cstring name, cstring binding_key = nullptr);

        // Handle-based form preserves the exact logical version selected by a
        // producer. Use this for pass composition instead of re-resolving a name.
        RGResourceHandle ReadTexture(RGResourceHandle handle, cstring binding_key = nullptr);

        // Declare that the current pass reads a depth resource (read-only).
        RGResourceHandle ReadDepth(cstring name);

        // Handle-based form of ReadDepth().
        RGResourceHandle ReadDepth(RGResourceHandle handle);

        // Import an allocator-owned buffer. RenderGraph tracks synchronization but
        // never owns or frees the BufferView.
        RGResourceHandle ImportBuffer(cstring name, const Core::Memory::BufferView* buffer);
        RGResourceHandle ReadBuffer(cstring name, cstring binding_key = nullptr);
        RGResourceHandle ReadBuffer(RGResourceHandle handle, cstring binding_key = nullptr);
        RGResourceHandle ReadWriteBuffer(cstring name, cstring binding_key = nullptr);
        RGResourceHandle ReadIndirectBuffer(cstring name);

        // Import an externally-managed render target (not owned by the graph).
        RGResourceHandle ImportRenderTarget(cstring name, Textures::TextureHandle handle);

        // Attach an already-imported render target by name — looks up by name only.
        RGResourceHandle AttachRenderTarget(cstring name, const Textures::TextureHandle& texture);
    };

    // Pass-facing read API — replaces RenderGraphResourceInspector call sites in Execute().
    struct RenderGraphResourceInspector
    {
        RenderGraph*                    Graph = nullptr;

        void                            Initialize(RenderGraph* graph);

        // Retrieve a texture handle by RGResourceHandle (O(1), no string).
        Textures::TextureHandle         GetTextureHandle(RGResourceHandle handle) const;
        const Core::Memory::BufferView* GetBuffer(RGResourceHandle handle) const;

        // String-keyed overloads — preserved for existing Execute() call sites.
        Textures::TextureHandle         GetRenderTarget(cstring name) const;
        Textures::TextureHandle         GetTexture(cstring name) const;
    };

} // namespace ZEngine::Rendering::Renderers
