#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Hardwares
{
    struct LayerProperty
    {
        VkLayerProperties                              Properties;
        Core::Containers::Array<VkExtensionProperties> ExtensionCollection;
        Core::Containers::Array<VkExtensionProperties> DeviceExtensionCollection;
    };

    struct VulkanLayer
    {
        Core::Containers::Array<LayerProperty> InstanceLayers;
        void                                   QueryInstanceLayerProperties(Core::Memory::ArenaAllocator* arena);
        VkResult                               GetExtensionProperties(Core::Memory::ArenaAllocator* arena, LayerProperty& layer_property, const VkPhysicalDevice* physical_device = nullptr);
        VkResult                               GetDeviceExtensionProperties(const VkPhysicalDevice* physical_device);
    };
} // namespace ZEngine::Hardwares