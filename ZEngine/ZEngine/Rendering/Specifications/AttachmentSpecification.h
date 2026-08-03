#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Rendering/Specifications/FormatSpecification.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Specifications
{

    struct SubPassSpecification
    {
        VkAttachmentReference                          DepthStencilAttachementReference;
        VkSubpassDescription                           SubpassDescription;
        Core::Containers::Array<VkAttachmentReference> ColorAttachementReferences;
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
        PipelineBindPoint                                                 BindPoint;
        Core::Containers::UnorderedHashMap<uint32_t, ColorAttachment>     ColorsMap;
        Core::Containers::UnorderedHashMap<uint32_t, VkSubpassDependency> DependenciesMap;
        Core::Containers::Array<VkAttachmentDescription>                  ColorAttachements;
        Core::Containers::Array<SubPassSpecification>                     SubpassSpecifications;
        Core::Containers::Array<VkSubpassDependency>                      SubpassDependencies;
    };
} // namespace ZEngine::Rendering::Specifications
