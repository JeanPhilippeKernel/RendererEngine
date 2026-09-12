#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Hardwares/AsyncUploadQueue.h>
#include <ZEngine/Hardwares/DeferredFreeQueue.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Buffers/Framebuffer.h>
#include <ZEngine/Rendering/Primitives/Semaphore.h>
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/Readback/RGReadbackRing.h>
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
    struct RGPersistentPass;

    ZDEFINE_PTR(RenderGraphResourceBuilder);
    ZDEFINE_PTR(RenderGraphResourceInspector);
    ZDEFINE_PTR(RenderGraph);
    ZDEFINE_PTR(IRenderGraphCallbackPass);

    /// @brief Names of the shared virtual resources in the default frame graph.
    struct RendererResourceName
    {
        inline static cstring FrameDepthRenderTargetName  = "g_frame_depth_render_target";
        inline static cstring FrameSharedRenderTargetName = "g_frame_shared_render_target";
        inline static cstring FrameColorRenderTargetName  = "g_frame_color_render_target";

        inline static cstring GBufferAlbedoAOName         = "g_gbuffer_albedo_ao";
        inline static cstring GBufferNormalRoughnessName  = "g_gbuffer_normal_roughness";
        inline static cstring GBufferMetallicEmissiveName = "g_gbuffer_metallic_emissive";

        inline static cstring SceneCameraBufferName       = "SceneCamera";
    };

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

    /// @brief Stable index for one readback declaration in the current frame graph.
    struct RGReadbackHandle
    {
        uint32_t Index = UINT32_MAX;

        bool     Valid() const
        {
            return Index != UINT32_MAX;
        }
    };

    /// @brief Stable index for one graph-owned occlusion query pool.
    /// @details A pool owns one Vulkan query-pool object for every actual
    /// DeviceSwapchain::FrameContext. The handle remains valid while the
    /// RenderGraph exists; its Vulkan object is selected only while recording
    /// the active frame context.
    struct RGQueryHandle
    {
        uint32_t Index = UINT32_MAX;

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
        // Logical WSI resource. CommandBuffer owns its image layout transitions
        // because the backing image changes with every acquired frame.
        Swapchain,
    };

    /// @brief Declares that a pass has an externally observable side effect.
    enum class RGPassFlags : uint8_t
    {
        None      = 0,
        NeverCull = 1 << 0,
    };

    constexpr bool HasRGPassFlag(RGPassFlags flags, RGPassFlags flag)
    {
        return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
    }

    // How a pass uses a resource — drives barrier stage/access/layout derivation.
    enum class RGAccess : uint8_t
    {
        None,
        ColorWrite,
        ColorReadWrite,
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
        BufferWrite,
        BufferReadWrite,
        // Consumed by vkCmdDrawIndirect/vkCmdDrawIndexedIndirect. This needs a
        // distinct access mask from a shader storage-buffer read.
        IndirectRead,
        // Consumed by VK_EXT_conditional_rendering. This exact buffer version
        // gates a following pass without a CPU readback or synchronization stall.
        ConditionalRead,
        // Write-only storage image access. Read/write storage uses
        // ShaderReadWrite and therefore retains both shader access bits.
        StorageWrite,
        Count_,
    };

    /// @brief Shader stages that can consume a bindless image declaration.
    enum class RGShaderStages : uint8_t
    {
        None     = 0,
        Vertex   = 1 << 0,
        TessCtrl = 1 << 1,
        TessEval = 1 << 2,
        Geometry = 1 << 3,
        Fragment = 1 << 4,
        Compute  = 1 << 5,
    };

    constexpr RGShaderStages operator|(RGShaderStages lhs, RGShaderStages rhs)
    {
        return static_cast<RGShaderStages>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
    }

    constexpr bool HasRGShaderStage(RGShaderStages stages, RGShaderStages stage)
    {
        return (static_cast<uint8_t>(stages) & static_cast<uint8_t>(stage)) != 0;
    }

    /// @brief Fallback selected when conditional rendering is unavailable.
    enum class RGConditionalFallback : uint8_t
    {
        Reject,
        Unconditional,
    };

    /// @brief GPU conditional-execution parameters for one graph pass.
    struct ConditionalSpec
    {
        VkDeviceSize          Offset   = 0;
        bool                  Invert   = false;
        RGConditionalFallback Fallback = RGConditionalFallback::Reject;
    };

    /// @brief Resolved conditional-rendering declaration stored by a graph pass.
    struct RGConditionalState
    {
        RGResourceHandle Condition     = {};
        ConditionalSpec  Specification = {};
        bool             Enabled       = false;
        bool             UsesFallback  = false;
    };

    struct RGResourceState
    {
        VkPipelineStageFlags2 Stage  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        VkAccessFlags2        Access = 0;
        VkImageLayout         Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    /// @brief One image subresource range declared by a graph use.
    /// @details An empty aspect mask selects the resource's native aspect. The
    /// remaining-count constants have the same meaning as their Vulkan fields.
    struct RGSubresourceRange
    {
        VkImageAspectFlags AspectMask     = 0;
        uint32_t           BaseMipLevel   = 0;
        uint32_t           LevelCount     = VK_REMAINING_MIP_LEVELS;
        uint32_t           BaseArrayLayer = 0;
        uint32_t           LayerCount     = VK_REMAINING_ARRAY_LAYERS;
    };

    /// @brief Returns the Vulkan synchronization state for a resource access.
    RGResourceState GetRGAccessState(RGAccess access);

    /// @brief Compile-time metadata for one immutable logical resource version.
    struct RGResourceVersion
    {
        uint32_t ProducerPass   = UINT32_MAX;
        uint32_t FirstPassIndex = UINT32_MAX;
        uint32_t LastPassIndex  = 0;
        bool     Exported       = false;
    };

    /// @brief The state of one exact aspect/mip/layer image subresource.
    struct RGSubresourceState
    {
        RGSubresourceRange Range = {};
        RGResourceState    State = {};
    };

    struct RGResource
    {
        cstring                                     Name                     = nullptr;
        RGResourceKind                              Kind                     = RGResourceKind::Attachment;
        bool                                        External                 = false;
        Textures::TextureHandle                     TextureHandle            = {};
        // Imported buffers remain allocator-owned. Transient buffers are owned by
        // the graph transient pool and retired with the graph.
        const Core::Memory::BufferView*             Buffer                   = nullptr;
        VkDeviceSize                                BufferSize               = 0;
        VkBufferUsageFlags                          BufferUsage              = 0;
        // External resources may enter the graph with a known producer state.
        // Graph-owned images begin undefined; imported host buffers begin at the
        // host-write state set by ImportBuffer().
        RGResourceState                             InitialState             = {};
        RGResourceState                             CurrentState             = {}; // compile-time simulation
        RGResourceState                             RuntimeState             = {}; // per-frame Execute tracking
        // Monotonic logical version assigned during Setup(). The physical-image
        // implementation is currently in-place, but every version has its own
        // producer and lifetime metadata.
        uint32_t                                    LatestVersion            = 0;
        Core::Containers::Array<RGResourceVersion>  Versions                 = {};
        Core::Containers::Array<RGSubresourceState> CompileSubresourceStates = {};
        Core::Containers::Array<RGSubresourceState> RuntimeSubresourceStates = {};
        bool                                        HasStreamingTicket       = false;
        Hardwares::StreamingUploadTicket            StreamingTicket          = {};
        uint32_t                                    FirstPassIndex           = UINT32_MAX;
        uint32_t                                    LastPassIndex            = 0;
        bool                                        Transient                = true;
        Specifications::TextureSpecification        Spec                     = {};
    };

    struct RGPassResource
    {
        RGResourceHandle              Handle           = {};
        RGAccess                      Access           = RGAccess::None;
        cstring                       BindingKey       = nullptr;
        RGSubresourceRange            Range            = {};
        // Used by exact bindless image reads. All other declarations derive their
        // state exclusively from Access.
        RGResourceState               StateOverride    = {};
        uint32_t                      BindlessSlot     = UINT32_MAX;
        bool                          HasStateOverride = false;
        // Relevant to color/depth attachment writes only. It is deliberately a
        // pass-use property: later passes may load the same image after its first
        // writer cleared it.
        Specifications::LoadOperation LoadOp           = Specifications::LoadOperation::CLEAR;
    };

    /// @brief Work the graph records itself instead of forwarding to a callback pass.
    enum class RGInternalPassOperation : uint8_t
    {
        None,
        Readback,
        QueryReadback,
    };

    // Compile-time transition intent. The VkImage and source state deliberately are
    // not stored here: imported resources may change backing image and every
    // resource's old layout is execution-history dependent.
    struct RGImageBarrierPlan
    {
        uint32_t           ResourceIndex    = UINT32_MAX;
        RGResourceState    DestinationState = {};
        bool               DiscardContents  = false;
        RGSubresourceRange Range            = {};
    };

    struct RGBufferBarrierPlan
    {
        uint32_t        ResourceIndex    = UINT32_MAX;
        RGResourceState DestinationState = {};
    };

    /// @brief Graph-side acquire operation for a submitted streaming upload.
    struct RGStreamingAcquirePlan
    {
        Hardwares::StreamingUploadTicket Ticket           = {};
        uint32_t                         ResourceIndex    = UINT32_MAX;
        RGResourceState                  DestinationState = {};
        bool                             BarrierRecorded  = false;
        bool                             Acknowledged     = false;
    };

    /// @brief Synchronizes two distinct Vulkan objects that alias one allocation.
    /// @details The destination object's first image transition still discards its
    /// contents. This global memory dependency orders the old object's final use
    /// before that transition.
    struct RGAliasingBarrierPlan
    {
        uint32_t SourceResourceIndex      = UINT32_MAX;
        uint32_t DestinationResourceIndex = UINT32_MAX;
    };

    /// @brief A contiguous queue-local run in topological execution order.
    /// @details The compiler falls back to GRAPHIC_QUEUE when the selected GPU
    /// does not expose a distinct queue for the requested role.
    struct RGQueueBatch
    {
        Rendering::QueueType Queue          = Rendering::QueueType::GRAPHIC_QUEUE;
        uint32_t             FirstPassOrder = 0;
        uint32_t             PassCount      = 0;
    };

    /// @brief A cross-queue resource hazard resolved through a timeline wait.
    struct RGQueueDependency
    {
        uint32_t FromBatch = UINT32_MAX;
        uint32_t ToBatch   = UINT32_MAX;
    };

    /// @brief One exclusive-resource ownership hand-off between queue families.
    struct RGQueueOwnershipTransfer
    {
        uint32_t           ResourceIndex = UINT32_MAX;
        uint32_t           FromBatch     = UINT32_MAX;
        uint32_t           ToBatch       = UINT32_MAX;
        RGSubresourceRange Range         = {};
    };

    /// @brief One dependency edge in the frame's logical pass graph.
    struct RGPassDependency
    {
        uint32_t From = UINT32_MAX;
        uint32_t To   = UINT32_MAX;
    };

    /// @brief Passes with the same dependency depth.
    struct RGTopologyLevel
    {
        Core::Containers::Array<uint32_t> PassIndices = {};
    };

    /// @brief One declared range of an occlusion query pool written by a pass.
    struct RGQueryWrite
    {
        RGQueryHandle Pool       = {};
        uint32_t      FirstQuery = 0;
        uint32_t      Count      = 0;
    };

    struct RGPass
    {
        cstring                                         Name                  = nullptr;
        bool                                            Enabled               = true;
        bool                                            Culled                = false;
        RGPassFlags                                     Flags                 = RGPassFlags::None;
        RGPersistentPass*                               Persistent            = nullptr;
        IRenderGraphCallbackPass*                       Callback              = nullptr;
        RenderPasses::RenderPass*                       Handle                = nullptr;
        Buffers::FramebufferVNext*                      Framebuffer           = nullptr;
        VkRenderPass                                    FramebufferRenderPass = VK_NULL_HANDLE;
        VkImageView                                     FramebufferViews[16]  = {};
        uint32_t                                        FramebufferViewCount  = 0;
        uint32_t                                        FramebufferWidth      = 0;
        uint32_t                                        FramebufferHeight     = 0;
        uint32_t                                        FramebufferLayers     = 0;
        Core::Containers::Array<RGPassResource>         Reads                 = {};
        Core::Containers::Array<RGPassResource>         Writes                = {};
        /// @brief Explicit query-pool ranges used to derive graph dependencies.
        Core::Containers::Array<RGQueryWrite>           QueryWrites           = {};
        /// @brief Query pools reset before their first writer in this frame context.
        Core::Containers::Array<RGQueryHandle>          QueryResets           = {};
        Core::Containers::Array<RGImageBarrierPlan>     BarrierPlans          = {};
        Core::Containers::Array<RGBufferBarrierPlan>    BufferBarrierPlans    = {};
        Core::Containers::Array<RGAliasingBarrierPlan>  AliasingBarrierPlans  = {};
        Core::Containers::Array<RGStreamingAcquirePlan> StreamingAcquirePlans = {};
        bool                                            ReadsBindless         = false;
        RGConditionalState                              Conditional           = {};
        RGInternalPassOperation                         InternalOperation     = RGInternalPassOperation::None;
        uint32_t                                        InternalRequestIndex  = UINT32_MAX;
        RGQueryHandle                                   InternalQueryPool     = {};
        /// @brief False when a transfer callback records directly into a queue batch.
        bool                                            RequiresRenderPass    = true;
        /// @brief Requested and capability-resolved queue roles for this pass.
        Rendering::QueueType                            RequestedQueue        = Rendering::QueueType::GRAPHIC_QUEUE;
        Rendering::QueueType                            Queue                 = Rendering::QueueType::GRAPHIC_QUEUE;
        /// @brief Secondary-recording state written by the assigned worker.
        Hardwares::CommandBuffer*                       Secondary             = nullptr;
        uint32_t                                        SecondaryWorker       = UINT32_MAX;
        uint32_t                                        SecondaryOrdinal      = UINT32_MAX;
        bool                                            SecondaryRecorded     = false;

        bool                                            IsActive() const
        {
            return Enabled && !Culled;
        }
    };

    /// @brief One deferred CPU read attached to an exact graph buffer version.
    struct RGReadbackRequest
    {
        cstring          Name            = nullptr;
        RGResourceHandle Source          = {};
        VkDeviceSize     Offset          = 0;
        VkDeviceSize     Size            = VK_WHOLE_SIZE;
        RGReadbackFn     Callback        = nullptr;
        void*            Context         = nullptr;
        uint32_t         AllocationIndex = UINT32_MAX;
        bool             Recorded        = false;
        bool             Submitted       = false;
    };

    /// @brief Persistent Vulkan storage for one logical occlusion query pool.
    /// @details FramePools is indexed by the position of the active
    /// DeviceSwapchain::FrameContext, never FrameContext::Index. Several
    /// physical contexts intentionally share the latter index.
    struct RGQueryPoolStorage
    {
        cstring                              Name       = nullptr;
        uint32_t                             QueryCount = 0;
        Core::Containers::Array<VkQueryPool> FramePools = {};
    };

    /// @brief Deferred CPU delivery request for one occlusion query pool.
    struct RGQueryReadbackRequest
    {
        RGQueryHandle Pool                = {};
        RGReadbackFn  Callback            = nullptr;
        void*         Context             = nullptr;
        uint32_t      AllocationIndex     = UINT32_MAX;
        uint32_t      WriterCount         = 0;
        uint32_t      RecordedWriterCount = 0;
        bool          Recorded            = false;
        bool          Submitted           = false;
    };

    /// @brief An exact resource version consumed outside the graph.
    struct RGExportedResource
    {
        RGResourceHandle Handle    = {};
        uint32_t         PassIndex = UINT32_MAX;
    };

    /// @brief State retained while individual frame graphs are rebuilt.
    struct RGPersistentPass
    {
        cstring                    Name                  = nullptr;
        IRenderGraphCallbackPass*  Callback              = nullptr;
        RenderPasses::RenderPass*  Handle                = nullptr;
        Buffers::FramebufferVNext* Framebuffer           = nullptr;
        VkRenderPass               FramebufferRenderPass = VK_NULL_HANDLE;
        VkImageView                FramebufferViews[16]  = {};
        uint32_t                   FramebufferViewCount  = 0;
        uint32_t                   FramebufferWidth      = 0;
        uint32_t                   FramebufferHeight     = 0;
        uint32_t                   FramebufferLayers     = 0;
    };

    /// @brief Externally owned resource rebound into every frame graph.
    struct RGImportedResource
    {
        cstring                          Name               = nullptr;
        RGResourceKind                   Kind               = RGResourceKind::Attachment;
        Textures::TextureHandle          TextureHandle      = {};
        const Core::Memory::BufferView*  Buffer             = nullptr;
        RGResourceState                  InitialState       = {};
        bool                             HasStreamingTicket = false;
        Hardwares::StreamingUploadTicket StreamingTicket    = {};
    };

    /// @brief Immutable information for the graph declaration of one frame.
    ///
    /// Resource handles produced while registering are valid only until the next
    /// Register() call. Persistent callback objects must retain GPU state only.
    struct RenderGraphFrameContext
    {
        Scenes::SceneDataPtr Scene        = nullptr;
        uint8_t              FrameIndex   = 0;
        /// @brief Width of graph-owned render targets for this frame.
        uint32_t             RenderWidth  = 0;
        /// @brief Height of graph-owned render targets for this frame.
        uint32_t             RenderHeight = 0;
    };

    struct RGTransientSlot
    {
        Textures::TextureHandle              Handle        = {};
        Specifications::TextureSpecification Spec          = {};
        uint32_t                             FreeAfterPass = 0;
        struct Alias
        {
            cstring                              Name      = nullptr;
            Textures::TextureHandle              Handle    = {};
            Specifications::TextureSpecification Spec      = {};
            uint32_t                             FirstPass = UINT32_MAX;
            uint32_t                             LastPass  = 0;
            bool                                 Active    = false;
        };
        Core::Containers::Array<Alias> Aliases;
    };

    struct RGTransientPool
    {
        Core::Containers::Array<RGTransientSlot> Slots;

        void                                     Initialize(Core::Memory::ArenaAllocator* arena);
        void                                     BeginFrame();
        Textures::TextureHandle                  TryAlias(const Specifications::TextureSpecification& spec, uint32_t first_pass);
        void                                     Register(Textures::TextureHandle handle, const Specifications::TextureSpecification& spec, uint32_t last_pass);
        void                                     MarkInUse(Textures::TextureHandle handle, uint32_t last_pass);
        Textures::TextureHandle                  FindNamedAlias(cstring name, const Specifications::TextureSpecification& spec, uint32_t first_pass, uint32_t last_pass);
        RGTransientSlot*                         FindAliasingSlot(const Specifications::TextureSpecification& spec, uint32_t first_pass, uint32_t last_pass);
        void                                     RegisterAlias(RGTransientSlot* slot, cstring name, Textures::TextureHandle handle, const Specifications::TextureSpecification& spec, uint32_t first_pass, uint32_t last_pass);
        void                                     Clear();

    private:
        Core::Memory::ArenaAllocator* m_arena = nullptr;
    };

    struct RGTransientBufferSlot
    {
        Core::Memory::BufferView* Buffer        = nullptr;
        VkDeviceSize              Size          = 0;
        VkBufferUsageFlags        Usage         = 0;
        uint32_t                  FreeAfterPass = 0;
        struct Alias
        {
            cstring                   Name      = nullptr;
            Core::Memory::BufferView* Buffer    = nullptr;
            VkDeviceSize              Size      = 0;
            VkBufferUsageFlags        Usage     = 0;
            uint32_t                  FirstPass = UINT32_MAX;
            uint32_t                  LastPass  = 0;
            bool                      Active    = false;
        };
        Core::Containers::Array<Alias> Aliases;
    };

    /// @brief Reuses whole transient buffers whose lifetimes do not overlap.
    struct RGTransientBufferPool
    {
        Core::Containers::Array<RGTransientBufferSlot> Slots;

        void                                           Initialize(Core::Memory::ArenaAllocator* arena);
        void                                           BeginFrame();
        Core::Memory::BufferView*                      TryAlias(VkDeviceSize size, VkBufferUsageFlags usage, uint32_t first_pass);
        void                                           Register(Core::Memory::BufferView* buffer, VkDeviceSize size, VkBufferUsageFlags usage, uint32_t last_pass);
        void                                           MarkInUse(Core::Memory::BufferView* buffer, uint32_t last_pass);
        Core::Memory::BufferView*                      FindNamedAlias(cstring name, VkDeviceSize size, VkBufferUsageFlags usage, uint32_t first_pass, uint32_t last_pass);
        RGTransientBufferSlot*                         FindAliasingSlot(VkDeviceSize size, VkBufferUsageFlags usage, uint32_t first_pass, uint32_t last_pass);
        void                                           RegisterAlias(RGTransientBufferSlot* slot, cstring name, Core::Memory::BufferView* buffer, VkDeviceSize size, VkBufferUsageFlags usage, uint32_t first_pass, uint32_t last_pass);
        void                                           Clear();

    private:
        Core::Memory::ArenaAllocator* m_arena = nullptr;
    };

    /// @brief Per-compile transient-memory and allocator-pressure snapshot.
    /// @details Virtual bytes count every live graph resource. Physical bytes count
    /// each active backing allocation once; their difference is the aliasing saving.
    struct RGTransientStatistics
    {
        uint32_t     ImageBackingAllocationCount  = 0;
        uint32_t     ImageAliasObjectCount        = 0;
        uint32_t     BufferBackingAllocationCount = 0;
        uint32_t     BufferAliasObjectCount       = 0;
        VkDeviceSize VirtualImageBytes            = 0;
        VkDeviceSize PhysicalImageBytes           = 0;
        VkDeviceSize VirtualBufferBytes           = 0;
        VkDeviceSize PhysicalBufferBytes          = 0;
        float        PeakHeapPressure             = 0.0f;

        VkDeviceSize EstimatedAliasingSavings() const
        {
            const VkDeviceSize virtual_bytes  = VirtualImageBytes + VirtualBufferBytes;
            const VkDeviceSize physical_bytes = PhysicalImageBytes + PhysicalBufferBytes;
            return virtual_bytes > physical_bytes ? virtual_bytes - physical_bytes : 0;
        }
    };

    /// @brief GPU duration measured for one graph pass on its resolved queue.
    struct RGPassTiming
    {
        cstring              Name                 = nullptr;
        Rendering::QueueType Queue                = Rendering::QueueType::GRAPHIC_QUEUE;
        uint64_t             BeginTimestamp       = 0;
        uint64_t             EndTimestamp         = 0;
        float                DurationMilliseconds = 0.0f;
        bool                 Available            = false;
    };

    /// @brief Timestamp state owned by one reusable swapchain frame context.
    struct RGTimestampFrame
    {
        VkQueryPool                           QueryPool       = VK_NULL_HANDLE;
        uint32_t                              QueryCount      = 0;
        bool                                  Submitted       = false;
        Core::Containers::Array<RGPassTiming> RecordedTimings = {};
    };

    /// @brief Converts a possibly wrapping timestamp pair into milliseconds.
    float RGTimestampDurationMilliseconds(uint64_t begin_timestamp, uint64_t end_timestamp, uint32_t valid_bits, float timestamp_period);

    /// @brief Selects a portable graph-dump representation.
    enum class RGDebugDumpFormat : uint8_t
    {
        Dot,
        Json,
    };

    /// @brief Callback contract for a persistent render-graph pass.
    /// @details Register() runs once per graph frame. Returning false omits the
    /// pass, its resource declarations, and its synchronization work for that frame.
    struct IRenderGraphCallbackPass
    {
        /// @brief Registers this frame's virtual resources and pass declarations.
        /// @return False to omit this callback from the current frame graph.
        virtual bool                           Register(Hardwares::VulkanDevicePtr const device, cstring name, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) = 0;
        /// @brief Returns the backend pipeline kind required to compile this callback.
        virtual Specifications::RenderPassType GetPipelineType() const
        {
            return Specifications::RenderPassType::GRAPHIC;
        }
        /// @brief Builds static graphics-PSO state when the graph creates this pass.
        /// @details The graph owns attachment compatibility. Implementations must
        /// not include per-frame descriptors, viewport dimensions, or resources.
        virtual Specifications::GraphicsPipelineDesc BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* /*arena*/) const
        {
            return {};
        }
        /// @brief Returns the compute shader used when GetPipelineType() is COMPUTE.
        virtual cstring GetComputeShaderName() const
        {
            return nullptr;
        }
        /// @brief Returns the compute push-constant byte size for this pass.
        virtual uint32_t GetComputePushConstantSize() const
        {
            return 0;
        }
        /// @brief Refreshes frame-local bindings after graph compilation.
        /// @details `pass` is null only when RequiresRenderPass() returns false.
        virtual void Prepare(Hardwares::VulkanDevicePtr const /*device*/, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderGraphResourceInspectorPtr /*res_inspector*/, RenderPasses::RenderPass* const /*pass*/) {}
        /// @brief Records commands outside graph-managed dynamic rendering.
        virtual void Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) = 0;
        /// @brief Records graphics commands inside an already active rendering instance.
        /// @return True when the primary must execute the recorded secondary buffer.
        virtual bool RecordDraw(Hardwares::VulkanDevicePtr const /*device*/, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderPasses::RenderPass* const /*pass*/, Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const /*command_buffer*/)
        {
            return false;
        }
        /// @brief Returns whether RecordDraw() supports graph-managed rendering.
        /// @details A true result permits secondary or primary command recording inside
        /// the graph-owned dynamic-rendering scope.
        virtual bool SupportsSecondaryRecording() const
        {
            return false;
        }
        /// @brief Returns graph scheduling and culling metadata for this pass.
        virtual RGPassFlags GetPassFlags() const
        {
            return RGPassFlags::None;
        }
        /// @brief Returns the queue role requested by this callback pass.
        virtual Rendering::QueueType GetRequestedQueue() const
        {
            return Rendering::QueueType::GRAPHIC_QUEUE;
        }
        /// @brief Returns whether this callback requires a RenderPass object to execute.
        virtual bool RequiresRenderPass() const
        {
            return true;
        }
        /// @brief Releases persistent resources owned by the callback.
        virtual void Deinitialize(Hardwares::VulkanDevicePtr const device) {}
    };

    struct RenderGraph
    {
        RenderGraph()                                                          = default;
        ~RenderGraph()                                                         = default;

        Hardwares::VulkanDevicePtr                                Device       = nullptr;
        Scenes::SceneDataPtr                                      SceneData    = nullptr;
        /// @brief Persistent extent used by graph-owned viewport render targets.
        uint32_t                                                  RenderWidth  = 0;
        /// @brief Persistent extent used by graph-owned viewport render targets.
        uint32_t                                                  RenderHeight = 0;

        /// @brief Arena for the virtual graph rebuilt by Register() every frame.
        Core::Memory::ArenaAllocator                              FrameArena   = {};
        Core::Containers::Array<RGPass>                           Passes;
        Core::Containers::Array<RGResource>                       Resources;
        Core::Containers::Array<uint32_t>                         SortedPassIndices;
        Core::Containers::Array<RGPassDependency>                 PassDependencies;
        Core::Containers::Array<RGTopologyLevel>                  TopologyLevels;
        Core::Containers::Array<RGQueueBatch>                     QueueBatches;
        Core::Containers::Array<RGQueueDependency>                QueueDependencies;
        Core::Containers::Array<RGQueueOwnershipTransfer>         QueueOwnershipTransfers;
        Core::Containers::Array<RGExportedResource>               ExportedResources;
        Core::Containers::Array<Hardwares::StreamingUploadTicket> StreamingUploadTickets;
        Core::Containers::Array<RGReadbackRequest>                ReadbackRequests;
        Core::Containers::Array<RGQueryReadbackRequest>           QueryReadbackRequests;
        RGReadbackRing                                            ReadbackRing;
        /// @brief One timeline per resolved queue role to preserve independent ordering.
        static constexpr uint32_t                                 QueueTimelineCount                      = static_cast<uint32_t>(Rendering::QueueType::COUNT);
        Rendering::Primitives::Semaphore*                         QueueTimelines[QueueTimelineCount]      = {};
        uint64_t                                                  QueueTimelineValues[QueueTimelineCount] = {};

        /// @brief Setup and compile lookup tables; Execute uses typed handles instead.
        Core::Containers::UnorderedHashMap<cstring, uint32_t>     ResourceIndex;
        Core::Containers::UnorderedHashMap<cstring, uint32_t>     PassIndex;

        Core::Containers::Array<RGPersistentPass>                 PersistentPasses;
        Core::Containers::Array<RGImportedResource>               ImportedResources;
        Core::Containers::UnorderedHashMap<cstring, uint32_t>     ImportedResourceIndex;
        Core::Containers::Array<RGQueryPoolStorage>               QueryPools;
        Core::Containers::UnorderedHashMap<cstring, uint32_t>     QueryPoolIndex;

        RenderGraphResourceBuilderPtr                             ResourceBuilder   = nullptr;
        RenderGraphResourceInspectorPtr                           ResourceInspector = nullptr;
        bool                                                      m_compile_valid   = false;

        RGTransientPool                                           TransientPool;
        RGTransientBufferPool                                     TransientBufferPool;
        RGTransientStatistics                                     TransientStatistics;
        static constexpr uint32_t                                 TimestampPassCapacity          = 128;
        Core::Containers::Array<RGTimestampFrame>                 TimestampFrames                = {};
        Core::Containers::Array<RGPassTiming>                     LatestPassTimings              = {};
        bool                                                      TimestampProfilingEnabled      = false;
        bool                                                      TimestampCapacityWarningIssued = false;

        /// @brief Initializes graph-owned pools, frame storage, and the selected device.
        void                                                      Initialize(Hardwares::VulkanDevicePtr device, Scenes::SceneDataPtr data = nullptr);
        /// @brief Adds a persistent callback pass that is registered every graph frame.
        void                                                      AddCallbackPass(cstring pass_name, IRenderGraphCallbackPass* const cb);
        /// @brief Rebuilds this frame's virtual pass and resource declarations.
        void                                                      Register(const RenderGraphFrameContext& frame_context = {});
        /// @brief Registers the initial graph frame.
        void                                                      Setup();
        /// @brief Compiles topology, transient resources, barriers, and queue batches.
        void                                                      Compile();
        /// @brief Records and submits the compiled graph using `cb` as the graphics primary.
        Hardwares::CommandBuffer*                                 Execute(Hardwares::CommandBufferPtr const cb);
        /// @brief Invalidates graph state after an output-dimension change.
        void                                                      Resize(uint32_t width, uint32_t height);
        /// @brief Releases graph-owned resources and persistent callback state.
        void                                                      Dispose();

        /// @brief Imports an externally managed render target.
        RGResourceHandle                                          ImportRenderTarget(cstring name, Textures::TextureHandle handle);
        /// @brief Imports a texture with the layout it has before graph execution.
        RGResourceHandle                                          ImportTexture(cstring name, Textures::TextureHandle handle, VkImageLayout initial_layout);
        /// @brief Imports a submitted upload and defers its acquire barrier to the graph.
        RGResourceHandle                                          ImportStreamingTexture(cstring name, const Hardwares::StreamingUploadTicket& ticket);
        /// @brief Imports an externally managed buffer with graph-tracked synchronization.
        RGResourceHandle                                          ImportBuffer(cstring name, const Core::Memory::BufferView* buffer);
        /// @brief Rebinds an imported buffer for the active frame without recompiling declarations.
        bool                                                      UpdateImportedBuffer(cstring name, const Core::Memory::BufferView* buffer);
        /// @brief Returns the latest compiled transient-memory snapshot.
        const RGTransientStatistics&                              GetTransientStatistics() const;
        /// @brief Returns timings from the most recently recycled frame context.
        const Core::Containers::Array<RGPassTiming>&              GetLatestPassTimings() const;
        /// @brief Returns whether graph timestamp profiling is active on this device.
        bool                                                      IsTimestampProfilingEnabled() const;
        /// @brief Appends the latest graph topology and compile diagnostics to `output`.
        void                                                      WriteDebugDump(Core::Containers::String& output, RGDebugDumpFormat format = RGDebugDumpFormat::Dot) const;

        /// @brief Returns a persistent pass slot by name for setup-time configuration.
        RGPass*                                                   GetPass(cstring name);

    private:
        void                                    InitializeFrameStorage();
        void                                    AddImportedResourceToFrame(const RGImportedResource& resource);
        RGResourceHandle                        SetImportedResource(const RGImportedResource& resource);
        Specifications::RenderPassSpecification BuildRenderPassSpecification(const RGPass& pass) const;
        void                                    SynchronizeCompiledPassResources(RGPass& pass);
        void                                    BindDeclaredBufferResources(RGPass& pass);
        void                                    BuildLifetimes();
        void                                    AllocateTransientResources();
        void                                    BuildAliasingBarriers();
        void                                    UpdateTransientStatistics();
        void                                    InitializeTimestampFrames();
        void                                    DisposeTimestampFrames();
        void                                    DisposeQueryPools();
        RGTimestampFrame*                       PrepareTimestampFrame();
        uint32_t                                AllocateTimestampPair(RGTimestampFrame& frame, cstring pass_name, Rendering::QueueType queue);
        void                                    BuildBarriers();
        void                                    BuildQueueSchedule();
        void                                    AddReadbackPasses();
        void                                    AddQueryReadbackPasses();
        bool                                    PrepareReadbacks();
        bool                                    PrepareQueryReadbacks();
        void                                    SubmitReadbacks(uint32_t first_pass_order, uint32_t pass_count, Rendering::Primitives::Semaphore* timeline, uint64_t timeline_value);
        void                                    CancelUnsubmittedReadbacks();
        void                                    MarkQueryWritesRecorded(const RGPass& pass);
        void                                    BuildStreamingAcquirePlans();
        void                                    AcknowledgeStreamingAcquires(uint32_t first_pass_order, uint32_t pass_count);
        static void                             OnRenderWorkSubmitted(void* context, Rendering::Primitives::Semaphore* timeline, uint64_t timeline_value);
        // Returns false when the enabled subgraph has a dependency cycle. A cycle
        // has no valid execution order and therefore makes this compile invalid.
        bool                                    BuildTopology();
        void                                    AllocateFramebuffers();
        bool                                    ValidateDeclarations();
        bool                                    ValidateCallbackContracts() const;
    };

    /// @brief Pass-facing resource declaration API used during Register().
    struct RenderGraphResourceBuilder
    {
        RenderGraph*     Graph       = nullptr;
        uint32_t         CurrentPass = UINT32_MAX;

        /// @brief Attaches this declaration builder to a graph.
        void             Initialize(RenderGraph* graph);

        /// @brief Declares a transient color-attachment write.
        RGResourceHandle WriteColorAttachment(cstring name, const Specifications::TextureSpecification& spec, RGSubresourceRange range = {});

        /// @brief Declares a transient depth-attachment write.
        RGResourceHandle WriteDepthAttachment(cstring name, const Specifications::TextureSpecification& spec, RGSubresourceRange range = {});

        /// @brief Declares a color-attachment update that preserves previous contents.
        /// @details `spec.LoadOp` must be LOAD.
        RGResourceHandle UpdateColorAttachment(cstring name, const Specifications::TextureSpecification& spec, RGSubresourceRange range = {});

        /// @brief Writes a new storage-image version in GENERAL layout.
        RGResourceHandle WriteStorageImage(cstring name, const Specifications::TextureSpecification& spec, cstring binding_key = nullptr, RGSubresourceRange range = {});

        /// @brief Reads and writes a new storage-image version in GENERAL layout.
        RGResourceHandle ReadWriteStorageImage(cstring name, cstring binding_key = nullptr, RGSubresourceRange range = {});

        /// @brief Declares a sampled-texture read by resource name.
        RGResourceHandle ReadTexture(cstring name, cstring binding_key = nullptr, RGSubresourceRange range = {});

        /// @brief Declares a sampled-texture read of one exact logical version.
        RGResourceHandle ReadTexture(RGResourceHandle handle, cstring binding_key = nullptr, RGSubresourceRange range = {});

        /// @brief Declares a read-only depth-resource read by name.
        RGResourceHandle ReadDepth(cstring name, RGSubresourceRange range = {});

        /// @brief Declares a read-only depth-resource read of one exact logical version.
        RGResourceHandle ReadDepth(RGResourceHandle handle, RGSubresourceRange range = {});

        /// @brief Imports an allocator-owned buffer without taking ownership.
        RGResourceHandle ImportBuffer(cstring name, const Core::Memory::BufferView* buffer);
        /// @brief Creates a transient storage-buffer version owned by the graph.
        RGResourceHandle WriteBuffer(cstring name, VkDeviceSize size, VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, cstring binding_key = nullptr);
        /// @brief Declares a shader buffer read by resource name.
        RGResourceHandle ReadBuffer(cstring name, cstring binding_key = nullptr);
        /// @brief Declares a shader buffer read of one exact logical version.
        RGResourceHandle ReadBuffer(RGResourceHandle handle, cstring binding_key = nullptr);
        /// @brief Declares a shader buffer read/write access and returns its new version.
        RGResourceHandle ReadWriteBuffer(cstring name, cstring binding_key = nullptr);
        /// @brief Declares an indirect-draw argument buffer read.
        RGResourceHandle ReadIndirectBuffer(cstring name);

        /// @brief Keeps an exact written version alive for external consumption.
        void             Export(RGResourceHandle handle);

        /// @brief Imports an externally managed render target without taking ownership.
        RGResourceHandle ImportRenderTarget(cstring name, Textures::TextureHandle handle);

        /// @brief Imports a texture with its externally established initial layout.
        RGResourceHandle ImportTexture(cstring name, Textures::TextureHandle handle, VkImageLayout initial_layout);

        /// @brief Imports a submitted upload and defers its acquire barrier to the graph.
        RGResourceHandle ImportStreamingTexture(cstring name, const Hardwares::StreamingUploadTicket& ticket);

        /// @brief Declares an exact graph image version consumed through the bindless array.
        /// @details The declaration creates the writer-to-reader edge and derives its
        /// shader-read layout transition. `slot` is the descriptor-array index.
        void             ReadBindless(RGResourceHandle image, uint32_t slot, RGShaderStages stages);

        /// @brief Declares an opaque bindless-array read of externally managed textures.
        /// @details Use the exact-image overload whenever the graph can identify the
        /// image. This form is retained for material tables whose per-draw texture
        /// selection is not available during graph registration.
        void             ReadBindless();

        /// @brief Gates the current pass on a GPU-written 32-bit condition-buffer value.
        /// @details `condition` identifies the exact produced buffer version. On devices
        /// without VK_EXT_conditional_rendering, `spec.Fallback` must explicitly select
        /// the unconditional implementation or compilation rejects the pass.
        void             UseConditional(RGResourceHandle condition, const ConditionalSpec& spec = {});

        /// @brief Declares deferred CPU delivery of an exact GPU buffer version.
        /// @details The graph records a transfer copy to a mapped staging allocation and
        /// invokes `callback` on the render thread after the exact queue timeline reaches
        /// the copy submission. `size == VK_WHOLE_SIZE` copies through the end of the buffer.
        /// `data` is valid only for the duration of the callback.
        RGReadbackHandle DeclareReadback(cstring name, RGResourceHandle gpu_buffer, RGReadbackFn callback, void* context, VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

        /// @brief Declares a graph-owned occlusion query pool.
        /// @details The same named declaration may be made by several passes,
        /// provided that each requests the same query count. Every actual frame
        /// context receives a distinct VkQueryPool object.
        RGQueryHandle    DeclareOcclusionQueryPool(cstring name, uint32_t query_count);

        /// @brief Declares that the current graphics pass writes an occlusion-query range.
        /// @details The callback must record matching CommandBuffer::BeginQuery()
        /// and EndQuery() calls for this range. A query readback depends on every
        /// pass that declares a write to its pool.
        void             WriteQueryPool(RGQueryHandle pool, uint32_t first_query, uint32_t count);

        /// @brief Declares deferred CPU delivery of all 64-bit results in an occlusion pool.
        /// @details The graph records vkCmdCopyQueryPoolResults with
        /// VK_QUERY_RESULT_WAIT_BIT into its mapped readback ring. `callback`
        /// receives a uint64_t array on the render thread after the exact copy
        /// submission completes; the data is valid only during that callback.
        RGReadbackHandle DeclareQueryReadback(RGQueryHandle pool, RGReadbackFn callback, void* context);

        /// @brief Attaches an already imported render target by name.
        RGResourceHandle AttachRenderTarget(cstring name, const Textures::TextureHandle& texture);

        /// @brief Declares the acquired swapchain image as the current pass output.
        RGResourceHandle WriteSwapchain();
    };

    /// @brief Pass-facing resource lookup API used while recording commands.
    struct RenderGraphResourceInspector
    {
        RenderGraph*                    Graph = nullptr;

        /// @brief Attaches this lookup helper to a graph.
        void                            Initialize(RenderGraph* graph);

        /// @brief Returns the texture for one exact logical resource version.
        Textures::TextureHandle         GetTextureHandle(RGResourceHandle handle) const;
        /// @brief Returns the buffer for one exact logical resource version.
        const Core::Memory::BufferView* GetBuffer(RGResourceHandle handle) const;
        /// @brief Returns the active frame context's Vulkan query pool.
        /// @details Valid only while recording the current graph frame. Returns
        /// VK_NULL_HANDLE for an invalid handle or outside active frame recording.
        VkQueryPool                     GetQueryPool(RGQueryHandle handle) const;

        /// @brief Returns a render target by its registered name.
        Textures::TextureHandle         GetRenderTarget(cstring name) const;
        /// @brief Returns a texture by its registered name.
        Textures::TextureHandle         GetTexture(cstring name) const;
    };

} // namespace ZEngine::Rendering::Renderers
