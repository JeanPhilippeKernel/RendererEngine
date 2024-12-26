#pragma once
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Shaders/Shader.h>
#include <Rendering/Specifications/GraphicRendererPipelineSpecification.h>
#include <ZEngineDef.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    struct GraphicPipeline : public Helpers::RefCounted
    {
    public:
        GraphicPipeline(Hardwares::VulkanDevice* device, Specifications::GraphicRendererPipelineSpecification&& spec);
        ~GraphicPipeline() = default;

        Specifications::GraphicRendererPipelineSpecification& GetSpecification();
        void                                                  SetSpecification(Specifications::GraphicRendererPipelineSpecification& spec);
        void                                                  Bake();
        void                                                  Dispose();
        VkPipeline                                            GetHandle() const;
        VkPipelineLayout                                      GetPipelineLayout() const;
        Helpers::Ref<Shaders::Shader>                         GetShader() const;

    protected:
        VkPipeline                                           m_pipeline_handle{VK_NULL_HANDLE};
        VkPipelineLayout                                     m_pipeline_layout{VK_NULL_HANDLE};
        Specifications::GraphicRendererPipelineSpecification m_pipeline_specification;
        Helpers::Ref<Shaders::Shader>                        m_shader;
        Hardwares::VulkanDevice*                             m_device{nullptr};
    };
} // namespace ZEngine::Rendering::Renderers::Pipelines