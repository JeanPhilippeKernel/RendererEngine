#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Specifications/ShaderSpecification.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::Rendering::Shaders
{
    struct Shader
    {
        Shader();
        ~Shader();

        void                                                                                                              Initialize(Hardwares::VulkanDevice* device, const Specifications::ShaderSpecification& spec);
        void                                                                                                              Dispose();
        Specifications::LayoutBindingSpecification                                                                        GetLayoutBindingSpecification(cstring name);

        VkDescriptorPool                                                                                                  m_descriptor_pool              = VK_NULL_HANDLE;
        Specifications::ShaderSpecification                                                                               m_specification                = {};
        Core::Memory::ArenaAllocator                                                                                      LocalArena                     = {};

        Core::Containers::Array<Specifications::PushConstantSpecification>                                                PushConstantSpecifications     = {};
        Core::Containers::Array<VkPipelineShaderStageCreateInfo>                                                          ShaderCreateInfos              = {};
        Core::Containers::Array<VkShaderModule>                                                                           ShaderModules                  = {};
        Core::Containers::Array<VkDescriptorSetLayout>                                                                    SetLayouts                     = {};
        Core::Containers::Array<Specifications::LayoutBindingSpecification>                                               LayoutBindingSpecifications    = {};
        Core::Containers::Array<VkPushConstantRange>                                                                      PushConstants                  = {};
        Core::Containers::UnorderedHashMap<uint32_t, Core::Containers::Array<VkDescriptorSet>>                            DescriptorSetMap               = {}; //<set, vec<descriptorSet>>
        Core::Containers::UnorderedHashMap<uint32_t, VkDescriptorSetLayout>                                               InternalDescriptorSetLayoutMap = {}; // <set, layout>
        Core::Containers::UnorderedHashMap<uint32_t, Core::Containers::Array<Specifications::LayoutBindingSpecification>> LayoutBindingSpecificationMap  = {};

    private:
        void CreateModule();
        void CreateDescriptorSetLayouts();
        void CreatePushConstantRange();

    private:
        Hardwares::VulkanDevice* m_device{nullptr};
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
