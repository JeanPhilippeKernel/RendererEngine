#pragma once
#include <Core/Containers/Array.h>
#include <Core/Containers/HashMap.h>
#include <Core/Memory/Allocator.h>
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Specifications/ShaderSpecification.h>
#include <ZEngineDef.h>

namespace ZEngine::Rendering::Shaders
{
    struct Shader
    {
        Shader();
        ~Shader();

        void                                                                                                     Initialize(Hardwares::VulkanDevice* device, const Specifications::ShaderSpecification& spec);
        void                                                                                                     Dispose();
        Specifications::LayoutBindingSpecification                                                               GetLayoutBindingSpecification(const char* name);

        VkDescriptorPool                                                                                         m_descriptor_pool             = VK_NULL_HANDLE;
        Specifications::ShaderSpecification                                                                      m_specification               = {};
        Core::Memory::ArenaAllocator                                                                             LocalArena                    = {};

        Core::Containers::Array<Specifications::PushConstantSpecification>                                       PushConstantSpecifications    = {};
        Core::Containers::Array<VkPipelineShaderStageCreateInfo>                                                 ShaderCreateInfos             = {};
        Core::Containers::Array<VkDescriptorSetLayout>                                                           SetLayouts                    = {};
        Core::Containers::Array<Specifications::LayoutBindingSpecification>                                      LayoutBindingSpections        = {};
        Core::Containers::Array<VkPushConstantRange>                                                             PushConstants                 = {};
        Core::Containers::HashMap<uint32_t, Core::Containers::Array<VkDescriptorSet>>                            DescriptorSetMap              = {}; //<set, vec<descriptorSet>>
        Core::Containers::HashMap<uint32_t, VkDescriptorSetLayout>                                               DescriptorSetLayoutMap        = {}; // <set, layout>
        Core::Containers::HashMap<uint32_t, Core::Containers::Array<Specifications::LayoutBindingSpecification>> LayoutBindingSpecificationMap = {};

    private:
        void CreateModule();
        void CreateDescriptorSetLayouts();
        void CreatePushConstantRange();

    private:
        Hardwares::VulkanDevice* m_device{nullptr};
    };

    Shader* CreateShader(const char* filename, bool defer_program_creation = false);
} // namespace ZEngine::Rendering::Shaders
