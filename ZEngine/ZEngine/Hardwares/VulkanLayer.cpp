#include <pch.h>
#include <Hardwares/VulkanLayer.h>
#include <Helpers/MemoryOperations.h>
#include <Logging/LoggerDefinition.h>

using namespace ZEngine::Core::Containers;

namespace ZEngine::Hardwares
{
    void VulkanLayer::QueryInstanceLayerProperties(Core::Memory::ArenaAllocator* arena)
    {
        uint32_t instance_layer_count{0};

        VkResult result = vkEnumerateInstanceLayerProperties(&instance_layer_count, nullptr);
        if ((result == VK_INCOMPLETE) || (instance_layer_count <= 0))
        {
            return;
        }

        size_t byte_size               = instance_layer_count * sizeof(VkLayerProperties);
        auto   vulkan_layer_properties = (VkLayerProperties*) malloc(byte_size);
        if (vulkan_layer_properties == nullptr)
        {
            return;
        }

        Helpers::secure_memset(vulkan_layer_properties, 0, byte_size, byte_size);

        result = vkEnumerateInstanceLayerProperties(&instance_layer_count, vulkan_layer_properties);
        if (result == VK_INCOMPLETE)
        {
            free(vulkan_layer_properties);
            return;
        }

        InstanceLayers.init(arena, instance_layer_count);
        for (unsigned i = 0; i < instance_layer_count; ++i)
        {
            auto property = *(vulkan_layer_properties + i);
            ZENGINE_CORE_INFO("Description: {0} --- LayerName: {1}", property.description, property.layerName)

            auto& layer_property      = InstanceLayers.push_use(LayerProperty{});
            layer_property.Properties = property;

            VkResult extension_result = GetExtensionProperties(arena, layer_property);
            if (extension_result != VK_SUCCESS)
            {
                ZENGINE_CORE_ERROR("LayerName: {}\nError Message: Failed to find layer extensions", property.layerName)
            }
        }

        free(vulkan_layer_properties);
    }

    VkResult VulkanLayer::GetExtensionProperties(Core::Memory::ArenaAllocator* arena, LayerProperty& layer_property, const VkPhysicalDevice* physical_gpu_device)
    {
        uint32_t extension_count{0};
        VkResult result;

        if (physical_gpu_device)
        {
            result = vkEnumerateDeviceExtensionProperties(*physical_gpu_device, layer_property.Properties.layerName, &extension_count, nullptr);
            if (extension_count > 0)
            {
                layer_property.DeviceExtensionCollection.init(arena, extension_count, extension_count);
                result = vkEnumerateDeviceExtensionProperties(*physical_gpu_device, layer_property.Properties.layerName, &extension_count, layer_property.DeviceExtensionCollection.data());
            }
        }
        else
        {
            result = vkEnumerateInstanceExtensionProperties(layer_property.Properties.layerName, &extension_count, nullptr);
            if (extension_count > 0)
            {
                layer_property.ExtensionCollection.init(arena, extension_count, extension_count);
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
