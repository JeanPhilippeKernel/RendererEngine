#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <span>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
}
namespace ZEngine::Rendering::Shaders
{
    struct Shader;
}

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    /// Render-owner storage independent of the shader's legacy descriptor sets.
    /// Owns descriptor storage only: callers must retain bound resources through
    /// their accepted submissions and only rewrite a frame after its fence.
    class PassDescriptorBank
    {
    public:
        static constexpr uint32_t MaxSets     = 8;
        static constexpr uint32_t MaxFrames   = 3;
        static constexpr uint32_t MaxBindings = 64;

        PassDescriptorBank()                  = default;
        ~PassDescriptorBank();
        PassDescriptorBank(const PassDescriptorBank&)                                       = delete;
        PassDescriptorBank&                            operator=(const PassDescriptorBank&) = delete;

        /// Transactional: refusal leaves any previous bank intact but does not
        /// make it usable for another shader generation. No Vulkan waits.
        bool                                           Prepare(Hardwares::VulkanDevice* device, Shaders::Shader* shader);
        [[nodiscard]] std::span<const VkDescriptorSet> GetSets(uint32_t set) const;
        void                                           Reset();
        void                                           Swap(PassDescriptorBank& other) noexcept;

    private:
        Hardwares::VulkanDevice*                                    m_device                 = nullptr;
        Shaders::Shader*                                            m_shader                 = nullptr;
        VkDescriptorPool                                            m_pool                   = VK_NULL_HANDLE;
        uint64_t                                                    m_retirement_reservation = 0;
        uint32_t                                                    m_generation             = UINT32_MAX;
        uint32_t                                                    m_frame_count            = 0;
        uint32_t                                                    m_set_count              = 0;
        std::array<std::array<VkDescriptorSet, MaxFrames>, MaxSets> m_sets                   = {};
    };
} // namespace ZEngine::Rendering::Renderers::RenderPasses
