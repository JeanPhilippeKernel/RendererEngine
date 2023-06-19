#include <pch.h>
#include <Rendering/Shaders/Compilers/LinkageStage.h>
#include <Rendering/Shaders/Compilers/ValidationStage.h>
#include <Logging/LoggerDefinition.h>
#include <Core/Coroutine.h>
#include <vulkan/vulkan.h>
#include <ZEngine.h>

namespace ZEngine::Rendering::Shaders::Compilers
{

    LinkageStage::LinkageStage()
    {
#ifdef __APPLE__
        // we dont perform validation stage on macOs for the moment as it considers generated shader program as invalid
#else
        m_next_stage = std::make_shared<ValidationStage>();
#endif
    }

    LinkageStage::~LinkageStage() {}

    void LinkageStage::Run(std::vector<ShaderInformation>& information_list) {}

    std::future<void> LinkageStage::RunAsync(std::vector<ShaderInformation>& information_list)
    {
        std::unique_lock lock(m_mutex);

        auto& performant_device = Engine::GetVulkanInstance()->GetHighPerformantDevice();

        for (ShaderInformation& shader_info : information_list)
        {
            VkShaderModuleCreateInfo shader_module_create_info = {};
            shader_module_create_info.sType                    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            shader_module_create_info.codeSize                 = sizeof(uint32_t) * shader_info.BinarySource.size();
            shader_module_create_info.pCode                    = shader_info.BinarySource.data();

            VkResult create_shader_result = vkCreateShaderModule(performant_device.GetNativeDeviceHandle(), &shader_module_create_info, nullptr, &(shader_info.ShaderModule));

            if (create_shader_result != VK_SUCCESS)
            {
                this->m_information.IsSuccess    = this->m_information.IsSuccess && false;
                this->m_information.ErrorMessage = "Failed to create ShaderModule";
                this->m_information.ErrorMessage.append(" for " + shader_info.Name);
                ZENGINE_CORE_ERROR("------> Failed to create ShaderModule for {}", shader_info.Name)
            }

            shader_info.ShaderStageCreateInfo       = {};
            shader_info.ShaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            if (shader_info.Type == ShaderType::VERTEX)
            {
                shader_info.ShaderStageCreateInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
            }
            else if (shader_info.Type == ShaderType::FRAGMENT)
            {
                shader_info.ShaderStageCreateInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            }
            shader_info.ShaderStageCreateInfo.module = shader_info.ShaderModule;
            shader_info.ShaderStageCreateInfo.pName  = "main";
        }

        if (!this->m_information.IsSuccess)
        {
            ZENGINE_CORE_ERROR("------> Linking stage completed with errors")
            ZENGINE_CORE_ERROR("------> {}", this->m_information.ErrorMessage)
        }

        co_return;
    }
} // namespace ZEngine::Rendering::Shaders::Compilers
