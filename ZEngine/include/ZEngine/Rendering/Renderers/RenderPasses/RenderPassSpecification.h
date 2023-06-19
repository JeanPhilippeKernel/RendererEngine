#pragma once
#include <vector>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct SubPassSpecification
    {
        std::vector<VkAttachmentReference> ColorAttachementReferences;
        VkAttachmentReference              DepthStencilAttachementReference;
        VkSubpassDescription               SubpassDescription;
    };

    struct RenderPassSpecification
    {
        std::vector<VkAttachmentDescription> ColorAttachements;
        std::vector<SubPassSpecification>    SubpassSpecifications;
        std::vector<VkSubpassDependency>     SubpassDependencies;
    };
} // namespace ZEngine::Rendering::Renderers::RenderPasses
