#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/HashSet.h>
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Rendering/Buffers/Framebuffer.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/Base/DescriptorPreparation.h>
#include <ZEngine/Rendering/Renderers/Base/PassDescriptorBank.h>
#include <ZEngine/Rendering/Renderers/Pipelines/RendererPipeline.h>
#include <ZEngine/Rendering/Specifications/RenderPassSpecification.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    inline constexpr uint32_t kMaxDescriptorReplayBindings  = 64;
    inline constexpr uint32_t kMaxDescriptorReplayNameBytes = 4096;

    enum class DescriptorReplayKind : uint8_t
    {
        DynamicUniform = 0,
        StorageBuffer,
        Texture,
        StorageImage,
        Sampler,
        TextureArray,
    };

    /// @brief One pass-owned descriptor write that can be restored after shader hot reload.
    struct DescriptorReplayRecord
    {
        cstring                                    Name            = nullptr;
        Specifications::LayoutBindingSpecification Binding         = {};
        const Core::Memory::BufferView*            Buffer          = nullptr;
        Textures::TextureHandle                    Texture         = {};
        VkDescriptorImageInfo                      Sampler         = {};
        VkImageSubresourceRange                    ImageRange      = {};
        VkImageLayout                              ImageLayout     = VK_IMAGE_LAYOUT_MAX_ENUM;
        VkDeviceSize                               Range           = 0;
        uint32_t                                   FrameIndex      = UINT32_MAX;
        uint32_t                                   ArrayElement    = 0;
        bool                                       RetainedTexture = false;
        DescriptorReplayKind                       Kind            = DescriptorReplayKind::StorageBuffer;
    };

    /// @brief Backend pass owned by a render-graph persistent-pass slot.
    /// @details VulkanDevice creates the concrete subtype from Specification.Type.
    struct RenderPass
    {
        RenderPass()                                                                                                                               = default;
        virtual ~RenderPass()                                                                                                                      = default;

        Specifications::RenderPassSpecification Specification                                                                                      = {};

        virtual void                            Initialize(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification) = 0;
        virtual void                            Dispose()                                                                                          = 0;
        virtual void                            Bake()                                                                                             = 0;
        virtual bool                            Verify()
        {
            return true;
        }
        /// Arena-owned backends may not be destructed during graph shutdown.
        virtual void ReleaseDescriptorStorage() {}
    };
    ZDEFINE_PTR(RenderPass);

    /// @brief Shared descriptor binding and hot-reload state for Vulkan-backed passes.
    /// @details Keeps graphics and compute descriptor writes identical and allocation-free
    /// after pass construction. Derived passes provide their concrete pipeline only.
    struct DescriptorBoundPass : RenderPass
    {
    public:
        /// Borrowed only while callbacks populate a bank. Successful writes
        /// clone exact leases; the pass never keeps the caller's span.
        class TextureSourceScope final
        {
        public:
            TextureSourceScope(DescriptorBoundPass* pass, std::span<const RenderResourceManager::TextureVersionPin> sources);
            ~TextureSourceScope();
            TextureSourceScope(const TextureSourceScope&)            = delete;
            TextureSourceScope& operator=(const TextureSourceScope&) = delete;

        private:
            DescriptorBoundPass*                                      m_pass     = nullptr;
            std::span<const RenderResourceManager::TextureVersionPin> m_previous = {};
        };

        /// Selects the retained initial camera only for the scene's UBCamera
        /// writes. Other uniforms (including asset previews) keep their heaps.
        class CameraSourceScope final
        {
        public:
            CameraSourceScope(DescriptorBoundPass* pass, const Core::Memory::BufferView* camera);
            ~CameraSourceScope();
            CameraSourceScope(const CameraSourceScope&)            = delete;
            CameraSourceScope& operator=(const CameraSourceScope&) = delete;

        private:
            DescriptorBoundPass*            m_pass     = nullptr;
            const Core::Memory::BufferView* m_previous = nullptr;
        };

        /// Starts a fresh proof before graph-synchronized and callback writes.
        void BeginDescriptorPreparation();
        /// Writes only this unpublished bank; never mutates CurrentFrame or
        /// admits the candidate's images as submitted frame consumers.
        void BeginPreparedDescriptorPreparation(uint32_t frame);
        bool AdoptPreparedDescriptors(DescriptorBoundPass& prepared);
        /// Only for an unpublished, exclusively owned backend being reused.
        void ResetSceneBindings();
        void ReleaseDescriptorStorage() override;
        void SetStorageBuffer(cstring name, const Core::Memory::BufferView* buffer);
        void SetStorageBufferForFrame(cstring name, uint32_t frame_index, const Core::Memory::BufferView* buffer);
        /// An explicit source is borrowed through descriptor replay and must
        /// outlive its banks and accepted GPU consumers. Null uses the scoped
        /// scene camera (UBCamera only), otherwise the ordinary per-frame heap.
        void SetDynamicUniform(cstring name, VkDeviceSize range, const Core::Memory::BufferView* source = nullptr);
        void SetTexture(cstring name, const Textures::TextureHandle& texture, VkImageLayout image_layout = VK_IMAGE_LAYOUT_MAX_ENUM);
        /// Pins the exact sampled-image version in this buffered frame's bank.
        /// Verification transfers a separate hold to accepted-submission
        /// retirement; replacing/discarding the bank cannot drop that hold.
        void SetRetainedTexture(cstring name, const RenderResourceManager::TextureVersionPin& texture, VkImageLayout image_layout = VK_IMAGE_LAYOUT_MAX_ENUM);
        void SetStorageImage(cstring name, const Textures::TextureHandle& texture, const VkImageSubresourceRange& range = {}, uint32_t array_element = 0);
        /// Retains the exact storage image independently for each array element.
        /// Writes still require graph barriers; this lease preserves storage only.
        void SetRetainedStorageImage(cstring name, const RenderResourceManager::TextureVersionPin& texture, const VkImageSubresourceRange& range = {}, uint32_t array_element = 0);
        /// @brief Binds one storage-image view to each consecutive element of an image array.
        void SetStorageImageArray(cstring name, const Textures::TextureHandle& texture, const VkImageSubresourceRange* ranges, uint32_t range_count);
        void SetRetainedStorageImageArray(cstring name, const RenderResourceManager::TextureVersionPin& texture, const VkImageSubresourceRange* ranges, uint32_t range_count);
        void SetSampler(cstring name, const VkDescriptorImageInfo& sampler_info);
        void UseTextureArray(cstring name);

    protected:
        void                               InitializeDescriptorBindings(Hardwares::VulkanDevice* device);
        [[nodiscard]] bool                 TryInitializeDescriptorBindings(Hardwares::VulkanDevice* device);
        void                               AttachPipelineCallbacks();
        bool                               VerifyDescriptorBindings();

        static bool                        ReplayDescriptorBindings(void* context);
        static VkDescriptorSet             ResolveDescriptorSet(void* context, uint32_t set, uint32_t frame);

        Core::Containers::HashSet<cstring> BoundBindings                                         = {};
        DescriptorReplayRecord             DescriptorReplayRecords[kMaxDescriptorReplayBindings] = {};
        uint32_t                           DescriptorReplayRecordCount                           = 0;
        char                               DescriptorReplayNames[kMaxDescriptorReplayNameBytes]  = {};
        uint32_t                           DescriptorReplayNameBytes                             = 0;
        bool                               DescriptorReplayValid                                 = true;
        Hardwares::VulkanDevice*           m_device                                              = nullptr;

    private:
        friend struct DescriptorReplayTestAccess;
        virtual Pipelines::IPipeline*                               GetPipeline() const = 0;
        std::pair<bool, Specifications::LayoutBindingSpecification> ValidateInput(cstring key, DescriptorReplayKind kind);
        DescriptorPreparation                                       m_descriptor_preparation = {};
        PassDescriptorBank                                          m_descriptor_bank        = {};
        std::span<const RenderResourceManager::TextureVersionPin>   m_texture_sources        = {};
        const Core::Memory::BufferView*                             m_scene_camera_source    = nullptr;
        struct RetainedTextureBinding
        {
            RenderResourceManager::TextureVersionPin Pin          = {};
            uint32_t                                 Set          = UINT32_MAX;
            uint32_t                                 Binding      = UINT32_MAX;
            uint32_t                                 ArrayElement = 0;
        };
        RetainedTextureBinding                          m_retained_textures[PassDescriptorBank::MaxFrames][kMaxDescriptorReplayBindings] = {};
        void                                            SetTextureBinding(cstring name, const Textures::TextureHandle& texture, VkImageLayout image_layout, const RenderResourceManager::TextureVersionPin* retained);
        const RenderResourceManager::TextureVersionPin* FindTextureSource(const Textures::TextureHandle& texture) const;
        bool                                            PrepareImageRetention(const Specifications::LayoutBindingSpecification& binding, const Textures::TextureHandle& texture, uint32_t frame, uint32_t array_element, const RenderResourceManager::TextureVersionPin* retained, std::span<const uint32_t> reserved_indices, uint32_t* retained_index, RenderResourceManager::TextureVersionPin* pin);
        void                                            SetStorageImageBindings(cstring name, const Textures::TextureHandle& texture, const VkImageSubresourceRange* ranges, uint32_t range_count, uint32_t first_element, bool complete_array, const RenderResourceManager::TextureVersionPin* retained);
        std::span<const VkDescriptorSet>                PrepareDescriptorSets(uint32_t set);
        struct DescriptorWriteScope
        {
            DescriptorPreparation& Preparation;
            bool                   Accepted = false;
            ~DescriptorWriteScope()
            {
                if (!Accepted)
                {
                    Preparation.Reject();
                }
            }
        };
        bool             ReplayDescriptorBindings();
        bool             m_descriptor_bindings_verified       = false;
        bool             m_unpublished_descriptor_preparation = false;
        uint32_t         m_descriptor_frame_index             = UINT32_MAX;
        uint32_t         m_prepared_descriptor_frames         = 0;
        Shaders::Shader* m_prepared_descriptor_shader         = nullptr;
        uint32_t         m_prepared_descriptor_generation     = UINT32_MAX;
        uint32_t         DescriptorFrameIndex() const;
        void             BeginDescriptorPreparation(uint32_t frame, bool unpublished);
        void             RecordDescriptorBinding(const DescriptorReplayRecord& record);
    };

    /// Descriptor-only candidate owner. Borrows a generation-leased pipeline;
    /// never owns/disposes it or changes its command-binding callbacks.
    struct PreparedDescriptorPass final : DescriptorBoundPass
    {
        void               InitializeForPipeline(Hardwares::VulkanDevice* device, Pipelines::IPipeline* pipeline);
        [[nodiscard]] bool TryInitializeForPipeline(Hardwares::VulkanDevice* device, Pipelines::IPipeline* pipeline);
        void               Initialize(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification) override;
        void               Dispose() override;
        void               Bake() override {}
        bool               Verify() override;

    private:
        Pipelines::IPipeline* m_pipeline = nullptr;
        Pipelines::IPipeline* GetPipeline() const override
        {
            return m_pipeline;
        }
    };

    struct GraphicPass : DescriptorBoundPass
    {
    public:
        ~GraphicPass();

        uint32_t                          RenderAreaWidth  = 0;
        uint32_t                          RenderAreaHeight = 0;

        Core::Containers::Array<uint32_t> RenderTargets    = {};
        struct Attachment*                Attachment       = {nullptr};
        Pipelines::GraphicPipeline*       Pipeline         = {nullptr};

        void                              Initialize(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification) override;
        /// Candidate backend: the graph owns the exact prepared attachment.
        void                              InitializePrepared(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification, struct Attachment* attachment);
        [[nodiscard]] bool                TryInitializePrepared(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification, struct Attachment* attachment, Shaders::Shader* shader);
        void                              Dispose() override;
        void                              Bake() override;
        bool                              Verify() override;

        struct Attachment*                GetAttachment() const;
        void                              UpdateRenderTargets();
        uint32_t                          GetRenderAreaWidth() const;
        uint32_t                          GetRenderAreaHeight() const;

    private:
        bool                  m_owns_attachment = true;
        void                  InitializeWithAttachment(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification, struct Attachment* attachment);
        Pipelines::IPipeline* GetPipeline() const override
        {
            return Pipeline;
        }
    };
    ZDEFINE_PTR(GraphicPass);

    struct ComputePass : DescriptorBoundPass
    {
    public:
        ~ComputePass();

        Pipelines::ComputePipeline* Pipeline = {nullptr};
        [[nodiscard]] bool          TryInitializePrepared(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification, Shaders::Shader* shader);
        void                        Initialize(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification) override;
        void                        Dispose() override;
        void                        Bake() override;
        bool                        Verify() override;

    private:
        Pipelines::IPipeline* GetPipeline() const override
        {
            return Pipeline;
        }
    };
    ZDEFINE_PTR(ComputePass);

} // namespace ZEngine::Rendering::Renderers::RenderPasses
