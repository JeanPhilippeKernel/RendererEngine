#pragma once
#include <vulkan/vulkan.h>
#include <vector>

namespace ZEngine::Hardwares
{
    struct LayerProperty
    {
        LayerProperty()  = default;
        ~LayerProperty() = default;

        VkLayerProperties                  Properties;
        std::vector<VkExtensionProperties> ExtensionCollection;
        std::vector<VkExtensionProperties> DeviceExtensionCollection;
    };

    class VulkanLayer
    {
    public:
        VulkanLayer()  = default;
        ~VulkanLayer() = default;

        std::vector<LayerProperty> GetInstanceLayerProperties();
        VkResult                   GetExtensionProperties(LayerProperty& layer_property, const VkPhysicalDevice* physical_device = nullptr);
        VkResult                   GetDeviceExtensionProperties(const VkPhysicalDevice* physical_device);
    };
} // namespace ZEngine::Hardwares