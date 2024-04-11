#pragma once
#include <Rendering/Shaders/ShaderEnums.h>
#include <vulkan/vulkan.h>
#include <string>

namespace ZEngine::Rendering::Shaders
{

    struct ShaderInformation
    {
        VkPipelineShaderStageCreateInfo ShaderStageCreateInfo;
        VkShaderModule                  ShaderModule;
        std::vector<uint32_t>           BinarySource;
        /**
         * Wether the shader has been compiled once
         */
        bool CompiledOnce;
        /**
         * Graphic Shader type
         * @see ShaderType
         */
        ShaderType Type;
        /**
         * Name of shader
         */
        std::string Name;
        /**
         * Source of shader
         */
        std::string Source;
    };
} // namespace ZEngine::Rendering::Shaders
