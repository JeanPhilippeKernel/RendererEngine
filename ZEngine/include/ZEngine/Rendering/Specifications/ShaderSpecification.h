#pragma once 
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Specifications
{
    enum class DescriptorType : uint32_t
    {
        SAMPLER = 0,
        COMBINED_IMAGE_SAMPLER,
        SAMPLED_IMAGE,
        STORAGE_IMAGE,
        UNIFORM_TEXEL_BUFFER,
        STORAGE_TEXEL_BUFFER,
        UNIFORM_BUFFER,
        STORAGE_BUFFER,
        UNIFORM_BUFFER_DYNAMIC,
        STORAGE_BUFFER_DYNAMIC,
        INPUT_ATTACHMENT
    };

    static VkDescriptorType DescriptorTypeMap[] = {
        VK_DESCRIPTOR_TYPE_SAMPLER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
        VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT};

    enum class ShaderStageFlags : uint32_t
    {
        VERTEX = 0,
        TESSELLATION_CONTROL,
        TESSELLATION_EVALUATION,
        GEOMETRY,
        FRAGMENT,
        COMPUTE,
        ALL_GRAPHICS
    };

    static VkShaderStageFlags ShaderStageFlagsMap[] = {
        VK_SHADER_STAGE_VERTEX_BIT,
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
        VK_SHADER_STAGE_GEOMETRY_BIT,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        VK_SHADER_STAGE_COMPUTE_BIT,
        VK_SHADER_STAGE_ALL_GRAPHICS};

    struct LayoutBindingSpecification
    {
        uint32_t         Set{0xFFFFFFFF};
        uint32_t         Binding{0xFFFFFFFF};
        uint32_t         Count{1};
        std::string      Name;
        DescriptorType   DescriptorType;
        ShaderStageFlags Flags;
    };

    struct PushConstantSpecification
    {
        std::string      Name;
        uint32_t         Size;
        uint32_t         Offset;
        ShaderStageFlags Flags;
    };

    struct ShaderSpecification
    {
        uint32_t    OverloadMaxSet   = 1;
        uint32_t    OverloadPoolSize = 0;
        std::string VertexFilename   = {};
        std::string FragmentFilename = {};
    };
} // ZEngine::Rendering::Specifications

