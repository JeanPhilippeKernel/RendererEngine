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

    struct GraphicPass : RenderPass
    {
    public:
        ~GraphicPass();

        uint32_t                           RenderAreaWidth                                       = 0;
        uint32_t                           RenderAreaHeight                                      = 0;

        Core::Containers::HashSet<cstring> BoundBindings                                         = {};
        Core::Containers::Array<uint32_t>  RenderTargets                                         = {};
        DescriptorReplayRecord             DescriptorReplayRecords[kMaxDescriptorReplayBindings] = {};
        uint32_t                           DescriptorReplayRecordCount                           = 0;
        struct Attachment*                 Attachment                                            = {nullptr};
        Pipelines::GraphicPipeline*        Pipeline                                              = {nullptr};

        void                               Initialize(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification) override;
        void                               Dispose() override;
        void                               Bake() override;
        bool                               Verify() override;

        void                               SetStorageBuffer(std::string_view name, const Core::Memory::BufferView* buffer);
        void                               SetStorageBufferForFrame(cstring name, uint32_t frame_index, const Core::Memory::BufferView* buffer);
        void                               SetDynamicUniform(std::string_view name, VkDeviceSize range);
        void                               SetTexture(std::string_view name, const Textures::TextureHandle& texture);
        void                               SetSampler(cstring name, const VkDescriptorImageInfo& sampler_info);
        void                               UseTextureArray(std::string_view name);

        struct Attachment*                 GetAttachment() const;
        void                               UpdateRenderTargets();
        uint32_t                           GetRenderAreaWidth() const;
        uint32_t                           GetRenderAreaHeight() const;

    private:
        std::pair<bool, Specifications::LayoutBindingSpecification> ValidateInput(std::string_view key);
        static bool                                                 ReplayDescriptorBindings(void* context);
        bool                                                        ReplayDescriptorBindings();
        void                                                        RecordDescriptorBinding(const DescriptorReplayRecord& record);

    private:
        Hardwares::VulkanDevice* m_device = nullptr;
    };
    ZDEFINE_PTR(GraphicPass);

    struct ComputePass : RenderPass
    {
    public:
        ~ComputePass();

        Pipelines::ComputePipeline* Pipeline                                              = {nullptr};
        DescriptorReplayRecord      DescriptorReplayRecords[kMaxDescriptorReplayBindings] = {};
        uint32_t                    DescriptorReplayRecordCount                           = 0;

        void                        Initialize(Hardwares::VulkanDevice* device, Specifications::RenderPassSpecification specification) override;
        void                        Dispose() override;
        void                        Bake() override;
        void                        SetStorageBuffer(cstring name, const Core::Memory::BufferView* buffer);
        void                        SetStorageBufferForFrame(cstring name, uint32_t frame_index, const Core::Memory::BufferView* buffer);

    private:
        static bool              ReplayDescriptorBindings(void* context);
        bool                     ReplayDescriptorBindings();
        void                     RecordDescriptorBinding(const DescriptorReplayRecord& record);
        Hardwares::VulkanDevice* m_device = nullptr;
    };
    ZDEFINE_PTR(ComputePass);

} // namespace ZEngine::Rendering::Renderers::RenderPasses
