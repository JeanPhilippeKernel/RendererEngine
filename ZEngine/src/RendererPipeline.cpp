#include <pch.h>
#include <Engine.h>
#include <Rendering/Renderers/Pipelines/RendererPipeline.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    void StandardGraphicPipeline::Dispose()
    {
        auto device_handle = Engine::GetVulkanInstance()->GetHighPerformantDevice().GetNativeDeviceHandle();

        {
            UBOCameraPropertiesCollection.clear();
            UBOCameraPropertiesCollection.shrink_to_fit();
            UBOModelPropertiesCollection.clear();
            UBOModelPropertiesCollection.shrink_to_fit();
        }

        if (DescriptorPool)
        {
            vkDestroyDescriptorPool(device_handle, DescriptorPool, nullptr);
            DescriptorPool = VK_NULL_HANDLE;
        }
        if (DescriptorSetLayout)
        {
            vkDestroyDescriptorSetLayout(device_handle, DescriptorSetLayout, nullptr);
            DescriptorSetLayout = VK_NULL_HANDLE;
        }
        if (Pipeline)
        {
            vkDestroyPipeline(device_handle, Pipeline, nullptr);
            Pipeline = VK_NULL_HANDLE;
        }
        if (PipelineLayout)
        {
            vkDestroyPipelineLayout(device_handle, PipelineLayout, nullptr);
            PipelineLayout = VK_NULL_HANDLE;
        }
        if (RenderPass)
        {
            vkDestroyRenderPass(device_handle, RenderPass, nullptr);
            RenderPass = VK_NULL_HANDLE;
        }
    }
} // namespace ZEngine::Rendering::Renderers::Pipelines