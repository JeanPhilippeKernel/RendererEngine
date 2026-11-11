#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Specifications/ShaderSpecification.h>
#include <ZEngine/ZEngineDef.h>
#include <array>

namespace ZEngine::Core::VFS
{
    struct IVFSContext;
}

namespace ZEngine::Rendering::Shaders
{
    struct Shader;

    struct ShaderInitializationServices
    {
        Core::VFS::IVFSContext*         VFS          = nullptr;
        PFN_vkCreateShaderModule        CreateModule = vkCreateShaderModule;
        PFN_vkCreateDescriptorSetLayout CreateLayout = vkCreateDescriptorSetLayout;
        PFN_vkCreateDescriptorPool      CreatePool   = vkCreateDescriptorPool;
        PFN_vkAllocateDescriptorSets    AllocateSets = vkAllocateDescriptorSets;
    };

    /// @brief Render-owner lease that keeps reflection, descriptor layouts and
    /// shader modules on one generation while a scene pipeline is prepared.
    /// Reload requests coalesce until the last lease is released. Release never
    /// reloads inline; VulkanDevice drains deferred reloads before recording.
    class ShaderGenerationLease
    {
    public:
        ShaderGenerationLease() = default;
        ~ShaderGenerationLease();
        ShaderGenerationLease(const ShaderGenerationLease&)            = delete;
        ShaderGenerationLease& operator=(const ShaderGenerationLease&) = delete;
        ShaderGenerationLease(ShaderGenerationLease&& other) noexcept;
        ShaderGenerationLease& operator=(ShaderGenerationLease&& other) noexcept;

        explicit               operator bool() const
        {
            return m_shader != nullptr;
        }
        Shader* GetShader() const
        {
            return m_shader;
        }
        uint32_t GetGeneration() const
        {
            return m_generation;
        }
        void Reset();

    private:
        friend struct Shader;
        ShaderGenerationLease(Shader* shader, uint32_t generation);
        Shader*  m_shader     = nullptr;
        uint32_t m_generation = 0;
    };

    struct Shader
    {
        Shader();
        ~Shader();

        void                                Initialize(Hardwares::VulkanDevice* device, const Specifications::ShaderSpecification& spec);
        /// Unpublished initialization. Native/read refusal cleans partial state
        /// and retains one bounded storage slot for retry, never another parent carve.
        bool                                TryInitialize(Hardwares::VulkanDevice* device, const Specifications::ShaderSpecification& spec, VkResult* status = nullptr, const ShaderInitializationServices& services = {});
        static constexpr size_t             StorageBytes       = ZMega(5);
        static constexpr size_t             BinaryScratchBytes = ZMega(1);
        static constexpr uint32_t           MaxFrames          = 3;
        /// @brief Rebuilds the module and reflection state on the render thread.
        void                                Reload();
        [[nodiscard]] ShaderGenerationLease RetainGeneration();
        /// @brief Render-owner observation; leases prevent module/layout replacement.
        [[nodiscard]] bool                  HasGenerationLeases() const
        {
            return m_generation_pins != 0;
        }
        /// @brief Applies a coalesced reload only at the render-owner safe point.
        bool               FlushDeferredReload();
        [[nodiscard]] bool HasDeferredReload() const
        {
            return m_reload_deferred;
        }
        void                                                                                                              Dispose();
        Specifications::LayoutBindingSpecification                                                                        GetLayoutBindingSpecification(cstring name);
        /// Counts descriptor elements once per set/binding, not once per reflected stage.
        [[nodiscard]] uint32_t                                                                                            DynamicDescriptorCount() const;
        [[nodiscard]] bool                                                                                                HasStages(VkShaderStageFlags required) const;
        VkDescriptorPool                                                                                                  m_descriptor_pool              = VK_NULL_HANDLE;
        /// @brief Render-thread-owned module generation; increments when hot reload replaces this module.
        uint32_t                                                                                                          Generation                     = 0;
        Specifications::ShaderSpecification                                                                               m_specification                = {};
        Core::Memory::ArenaAllocator                                                                                      LocalArena                     = {};

        Core::Containers::Array<Specifications::PushConstantSpecification>                                                PushConstantSpecifications     = {};
        Core::Containers::Array<VkPipelineShaderStageCreateInfo>                                                          ShaderCreateInfos              = {};
        Core::Containers::Array<VkShaderModule>                                                                           ShaderModules                  = {};
        Core::Containers::Array<VkDescriptorSetLayout>                                                                    SetLayouts                     = {};
        Core::Containers::Array<Specifications::LayoutBindingSpecification>                                               LayoutBindingSpecifications    = {};
        Core::Containers::UnorderedHashMap<const char*, Specifications::LayoutBindingSpecification>                       BindingsByName                 = {};
        Core::Containers::Array<VkPushConstantRange>                                                                      PushConstants                  = {};
        Core::Containers::UnorderedHashMap<uint32_t, Core::Containers::Array<VkDescriptorSet>>                            DescriptorSetMap               = {}; //<set, vec<descriptorSet>>
        Core::Containers::UnorderedHashMap<uint32_t, VkDescriptorSetLayout>                                               InternalDescriptorSetLayoutMap = {}; // <set, layout>
        Core::Containers::UnorderedHashMap<uint32_t, Core::Containers::Array<Specifications::LayoutBindingSpecification>> LayoutBindingSpecificationMap  = {};

    private:
        bool CreateModule(VkResult* status, const ShaderInitializationServices& services);
        bool CreateDescriptorSetLayouts(VkResult* status, const ShaderInitializationServices& services);
        void CreatePushConstantRange();
        void DestroyModules();
        void RetireDescriptorPool();
        void ResetInitialization();

    private:
        friend class ShaderGenerationLease;
        uint32_t                                                              m_generation_pins = 0;
        bool                                                                  m_reload_deferred = false;
        Hardwares::VulkanDevice*                                              m_device{nullptr};
        Core::Memory::ArenaAllocator                                          m_binary_arena   = {};
        Core::Memory::ArenaAllocator*                                         m_storage_parent = nullptr;
        Core::VFS::IVFSContext*                                               m_source_vfs     = nullptr;
        std::array<std::array<char, Hardwares::kMaxShaderReloadPathBytes>, 4> m_source_strings = {};
    };

    Shader* CreateShader(const char* filename, bool defer_program_creation = false);
} // namespace ZEngine::Rendering::Shaders

namespace ZEngine::Helpers
{
    template <>
    inline void HandleManager<Rendering::Shaders::Shader>::Dispose()
    {
        for (size_t i = 0; i < m_count; ++i)
        {
            m_memory[i].Dispose();
        }
    }
} // namespace ZEngine::Helpers
