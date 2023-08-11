#pragma once
#include <vector>
#include <ZEngineDef.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    struct GraphicPipeline;
}

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct SubPassSpecification
    {
        std::vector<VkAttachmentReference> ColorAttachementReferences;
        VkAttachmentReference              DepthStencilAttachementReference;
        VkSubpassDescription               SubpassDescription;
    };

    struct AttachmentSpecification
    {
        std::vector<VkAttachmentDescription> ColorAttachements;
        std::vector<SubPassSpecification>    SubpassSpecifications;
        std::vector<VkSubpassDependency>     SubpassDependencies;
    };

    struct RenderPassSpecification
    {
        std::string                     DebugName;
        Ref<Pipelines::GraphicPipeline> Pipeline;
    };
} // namespace ZEngine::Rendering::Renderers::RenderPasses
