#include <pch.h>
#include <Hardwares/VulkanLayer.h>
#include <Logging/LoggerDefinition.h>

namespace ZEngine::Hardwares
{
    std::vector<LayerProperty> VulkanLayer::GetInstanceLayerProperties()
    {
        uint32_t                       instance_layer_count{0};
        std::vector<VkLayerProperties> layer_properties;

        VkResult result;
        result = vkEnumerateInstanceLayerProperties(&instance_layer_count, nullptr);
        if ((result == VK_INCOMPLETE) || (instance_layer_count <= 0))
        {
            return std::vector<LayerProperty>{};
        }

        std::vector<VkLayerProperties> vulkan_layer_properties(instance_layer_count);
        result = vkEnumerateInstanceLayerProperties(&instance_layer_count, vulkan_layer_properties.data());
        if (result == VK_INCOMPLETE)
        {
            return std::vector<LayerProperty>{};
        }

        std::vector<LayerProperty> layer_property_collection;
        for (const VkLayerProperties& property : vulkan_layer_properties)
        {
            ZENGINE_CORE_INFO("Description: {0} --- LayerName: {1}", property.description, property.layerName)

            LayerProperty layer_property{};
            layer_property.Properties = property;

            VkResult extension_result = GetExtensionProperties(layer_property);

            if (extension_result != VK_SUCCESS)
            {
                ZENGINE_CORE_ERROR("LayerName: {}\nError Message: Failed to find layer extensions", property.layerName)
            }
            layer_property_collection.push_back(std::move(layer_property));
        }

        return layer_property_collection;
    }

    VkResult VulkanLayer::GetExtensionProperties(LayerProperty& layer_property, const VkPhysicalDevice* physical_gpu_device)
    {
        uint32_t extension_count{0};
        VkResult result;

        if (physical_gpu_device)
        {
            result = vkEnumerateDeviceExtensionProperties(*physical_gpu_device, layer_property.Properties.layerName, &extension_count, nullptr);
            if (extension_count > 0)
            {
                layer_property.DeviceExtensionCollection.resize(extension_count);
                result = vkEnumerateDeviceExtensionProperties(
                    *physical_gpu_device, layer_property.Properties.layerName, &extension_count, layer_property.DeviceExtensionCollection.data());
            }
        }
        else
        {
            result = vkEnumerateInstanceExtensionProperties(layer_property.Properties.layerName, &extension_count, nullptr);
            if (extension_count > 0)
            {
                layer_property.ExtensionCollection.resize(extension_count);
                result = vkEnumerateInstanceExtensionProperties(layer_property.Properties.layerName, &extension_count, layer_property.ExtensionCollection.data());
            }
        }

        if (!layer_property.ExtensionCollection.empty())
        {
            for (const auto& extension : layer_property.ExtensionCollection)
            {
                ZENGINE_CORE_TRACE("ExtensionName: {0} --- SpecificationVersion: {1}", extension.extensionName, extension.specVersion)
            }
        }

        if (!layer_property.DeviceExtensionCollection.empty())
        {
            for (const auto& extension : layer_property.DeviceExtensionCollection)
            {
                ZENGINE_CORE_TRACE("ExtensionName: {0} --- SpecificationVersion: {1}", extension.extensionName, extension.specVersion)
            }
        }

        return result;
    }

    VkResult VulkanLayer::GetDeviceExtensionProperties(const VkPhysicalDevice* physical_device)
    {
        return VK_INCOMPLETE;
    }
} // namespace ZEngine::Hardwares
