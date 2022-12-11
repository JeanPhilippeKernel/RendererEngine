#pragma once
#include <string>
#include <vulkan/vulkan.h>
#include <Hardwares/VulkanLayer.h>
#include <Hardwares/VulkanDevice.h>

namespace ZEngine::Hardwares
{
    class VulkanInstance
    {
    public:
        VulkanInstance(std::string_view app_name);
        ~VulkanInstance();

        void CreateInstance();

        void ConfigureDevices(const VkSurfaceKHR* surface = nullptr);

        const std::vector<VulkanDevice>& GetDevices() const;
        const VulkanDevice&              GetHighPerformantDevice() const;
        const VkInstance                 GetNativeHandle() const;

    private:
        std::string               m_application_name;
        VulkanLayer               m_layer;
        VkInstance                m_vulkan_instance;
        VkDebugUtilsMessengerEXT  m_debug_messenger;
        std::vector<VulkanDevice> m_device_collection;

    private:
        PFN_vkCreateDebugUtilsMessengerEXT  __createDebugMessengerPtr{nullptr};
        PFN_vkDestroyDebugUtilsMessengerEXT __destroyDebugMessengerPtr{nullptr};

        static VKAPI_ATTR VkBool32 VKAPI_CALL __debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);
    };
} // namespace ZEngine::Hardwares
