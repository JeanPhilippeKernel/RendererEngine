#pragma once
#include <string>
#include <map>
#include <ZEngineDef.h>
#include <Rendering/Specifications/ShaderSpecification.h>

namespace ZEngine::Rendering::Shaders
{
    class Shader
    {
    public:
        Shader(const Specifications::ShaderSpecification& spec);
        ~Shader();

        const std::vector<VkPipelineShaderStageCreateInfo>&                                GetStageCreateInfoCollection() const;
        const std::map<uint32_t, std::vector<Specifications::LayoutBindingSpecification>>& GetLayoutBindingSetMap() const;
        const std::vector<VkDescriptorSetLayoutBinding>&                                   GetLayoutBindingCollection() = delete;
        const Specifications::ShaderSpecification&                                         GetSpecification() const     = delete;
        Specifications::ShaderSpecification                                                GetSpecification()           = delete;
        Specifications::LayoutBindingSpecification                                         GetLayoutBindingSpecification(std::string_view name) const;
        std::vector<VkDescriptorSetLayout>                                                 GetDescriptorSetLayout() const;
        const std::map<uint32_t, std::vector<VkDescriptorSet>>&                            GetDescriptorSetMap() const;
        void                                                                               Dispose();

        static Ref<Shader> Create(Specifications::ShaderSpecification&& spec);
        static Ref<Shader> Create(const Specifications::ShaderSpecification& spec);

    private:
        void CreateModule();
        void CreateDescriptorSetLayouts();

    private:
        Specifications::ShaderSpecification                                         m_specification;
        std::vector<VkDescriptorSetLayoutBinding>                                   m_layout_binding_collection;
        std::vector<VkShaderModule>                                                 m_shader_module_collection;
        std::vector<VkPipelineShaderStageCreateInfo>                                m_shader_create_info_collection;
        std::map<uint32_t, std::vector<Specifications::LayoutBindingSpecification>> m_layout_binding_specification_map;
        std::map<uint32_t, VkDescriptorSetLayout>                                   m_descriptor_set_layout_map; // <set, layout>
        std::map<uint32_t, std::vector<VkDescriptorSet>>                            m_descriptor_set_map;        //<set, vec<descriptorSet>>
        VkDescriptorPool                                                            m_descriptor_pool{VK_NULL_HANDLE};
    };

    Shader* CreateShader(const char* filename, bool defer_program_creation = false);
} // namespace ZEngine::Rendering::Shaders
