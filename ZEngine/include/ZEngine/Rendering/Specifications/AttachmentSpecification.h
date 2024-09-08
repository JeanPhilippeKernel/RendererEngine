#pragma once
#include <Rendering/Specifications/FormatSpecification.h>
#include <vulkan/vulkan.h>
#include <map>
#include <vector>

namespace ZEngine::Rendering::Specifications
{

    struct SubPassSpecification
    {
        std::vector<VkAttachmentReference> ColorAttachementReferences;
        VkAttachmentReference              DepthStencilAttachementReference;
        VkSubpassDescription               SubpassDescription;
    };

    struct ColorAttachment
    {
        ImageFormat    Format;
        LoadOperation  Load;
        StoreOperation Store;
        ImageLayout    Initial;
        ImageLayout    Final;
        ImageLayout    ReferenceLayout;
    };

    struct AttachmentSpecification
    {
        std::map<uint32_t, ColorAttachment>     ColorsMap;
        std::map<uint32_t, VkSubpassDependency> DependenciesMap;
        PipelineBindPoint                       BindPoint;
        std::vector<VkAttachmentDescription>    ColorAttachements;
        std::vector<SubPassSpecification>       SubpassSpecifications;
        std::vector<VkSubpassDependency>        SubpassDependencies;
    };
} // namespace ZEngine::Rendering::Specifications
