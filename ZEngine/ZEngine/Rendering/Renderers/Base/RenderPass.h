#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/HashSet.h>
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Rendering/Buffers/Framebuffer.h>
#include <ZEngine/Rendering/Renderers/Pipelines/RendererPipeline.h>
#include <ZEngine/Rendering/Specifications/RenderPassSpecification.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    inline constexpr uint32_t kMaxDescriptorReplayBindings = 64;

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
        cstring                                    Name       = nullptr;
        Specifications::LayoutBindingSpecification Binding    = {};
        const Core::Memory::BufferView*            Buffer     = nullptr;
        Textures::TextureHandle                    Texture    = {};
        VkDescriptorImageInfo                      Sampler    = {};
        VkImageSubresourceRange                    ImageRange = {};
        VkDeviceSize                               Range      = 0;
        uint32_t                                   FrameIndex = UINT32_MAX;
        DescriptorReplayKind                       Kind       = DescriptorReplayKind::StorageBuffer;
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
    };
    ZDEFINE_PTR(RenderPass);

    /// @brief Shared descriptor binding and hot-reload state for Vulkan-backed passes.
    /// @details Keeps graphics and compute descriptor writes identical and allocation-free
    /// after pass construction. Derived passes provide their concrete pipeline only.
    struct DescriptorBoundPass : RenderPass
    {
    public:
        void SetStorageBuffer(cstring name, const Core::Memory::BufferView* buffer);
        void SetStorageBufferForFrame(cstring name, uint32_t frame_index, const Core::Memory::BufferView* buffer);
        void SetDynamicUniform(cstring name, VkDeviceSize range);
        void SetTexture(cstring name, const Textures::TextureHandle& texture);
        void SetStorageImage(cstring name, const Textures::TextureHandle& texture, const VkImageSubresourceRange& range = {});
        void SetSampler(cstring name, const VkDescriptorImageInfo& sampler_info);
        void UseTextureArray(cstring name);

    protected:
        void                               InitializeDescriptorBindings(Hardwares::VulkanDevice* device);
        bool                               VerifyDescriptorBindings();

        static bool                        ReplayDescriptorBindings(void* context);

        Core::Containers::HashSet<cstring> BoundBindings                                         = {};
        DescriptorReplayRecord             DescriptorReplayRecords[kMaxDescriptorReplayBindings] = {};
        uint32_t                           DescriptorReplayRecordCount                           = 0;
        Hardwares::VulkanDevice*           m_device                                              = nullptr;

    private:
        virtual Pipelines::IPipeline*                               GetPipeline() const = 0;
        std::pair<bool, Specifications::LayoutBindingSpecification> ValidateInput(cstring key);
        bool                                                        ReplayDescriptorBindings();
        void                                                        RecordDescriptorBinding(const DescriptorReplayRecord& record);
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
        void                              Dispose() override;
        void                              Bake() override;
        bool                              Verify() override;

        struct Attachment*                GetAttachment() const;
        void                              UpdateRenderTargets();
        uint32_t                          GetRenderAreaWidth() const;
        uint32_t                          GetRenderAreaHeight() const;

    private:
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
