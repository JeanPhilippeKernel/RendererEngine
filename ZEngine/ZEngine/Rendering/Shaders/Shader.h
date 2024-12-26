#pragma once
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Specifications/ShaderSpecification.h>
#include <ZEngineDef.h>
#include <map>
#include <string>

namespace ZEngine::Rendering::Shaders
{
    class Shader : public Helpers::RefCounted
    {
    public:
        Shader(Hardwares::VulkanDevice* device, const Specifications::ShaderSpecification& spec);
        ~Shader();

        const std::vector<VkPipelineShaderStageCreateInfo>&                                GetStageCreateInfoCollection() const;
        const std::map<uint32_t, std::vector<Specifications::LayoutBindingSpecification>>& GetLayoutBindingSetMap() const;
        const std::vector<VkDescriptorSetLayoutBinding>&                                   GetLayoutBindingCollection() = delete;
        const Specifications::ShaderSpecification&                                         GetSpecification() const     = delete;
        Specifications::ShaderSpecification                                                GetSpecification()           = delete;
        Specifications::LayoutBindingSpecification                                         GetLayoutBindingSpecification(std::string_view name) const;
        std::vector<VkDescriptorSetLayout>                                                 GetDescriptorSetLayout() const;
        std::vector<Specifications::LayoutBindingSpecification>                            GetLayoutBindingSpecificationCollection() const;
        const std::map<uint32_t, std::vector<VkDescriptorSet>>&                            GetDescriptorSetMap() const;
        std::map<uint32_t, std::vector<VkDescriptorSet>>&                                  GetDescriptorSetMap();
        VkDescriptorPool                                                                   GetDescriptorPool() const;
        const std::vector<VkPushConstantRange>&                                            GetPushConstants() const;
        void                                                                               Dispose();

    private:
        void CreateModule();
        void CreateDescriptorSetLayouts();
        void CreatePushConstantRange();

    private:
        Specifications::ShaderSpecification                                         m_specification;
        std::vector<VkDescriptorSetLayoutBinding>                                   m_layout_binding_collection;
        std::vector<VkShaderModule>                                                 m_shader_module_collection;
        std::vector<VkPipelineShaderStageCreateInfo>                                m_shader_create_info_collection;
        std::map<uint32_t, std::vector<Specifications::LayoutBindingSpecification>> m_layout_binding_specification_map;
        std::map<uint32_t, VkDescriptorSetLayout>                                   m_descriptor_set_layout_map; // <set, layout>
        std::map<uint32_t, std::vector<VkDescriptorSet>>                            m_descriptor_set_map;        //<set, vec<descriptorSet>>
        std::vector<Specifications::PushConstantSpecification>                      m_push_constant_specification_collection;
        std::vector<VkPushConstantRange>                                            m_push_constant_collection;
        VkDescriptorPool                                                            m_descriptor_pool{VK_NULL_HANDLE};
        Hardwares::VulkanDevice*                                                    m_device{nullptr};
    };

    Shader* CreateShader(const char* filename, bool defer_program_creation = false);
} // namespace ZEngine::Rendering::Shaders
