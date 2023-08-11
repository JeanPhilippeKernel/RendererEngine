#pragma once
#include <vector>
#include <vulkan/vulkan.h>
#include <Rendering/Buffers/Image2DBuffer.h>
#include <Rendering/Primitives/Semaphore.h>

namespace ZEngine::Rendering
{
    class Swapchain
    {
    public:
        Swapchain(void* native_window);
        ~Swapchain();

        void Resize();

        void AcquireNextImage();
        void Present();

        uint32_t      GetMinImageCount() const;
        uint32_t      GetImageCount() const;
        VkRenderPass  GetRenderPass() const;
        VkFramebuffer GetCurrentFramebuffer();

    private:
        void Create();
        void Dispose();

        void*                                   m_native_window{nullptr};
        uint32_t                                m_current_frame_index{0};
        uint32_t                                m_current_frame_image_index{0};
        uint32_t                                m_last_frame_image_index{0};
        VkSwapchainKHR                          m_handle{VK_NULL_HANDLE};
        uint32_t                                m_image_width;
        uint32_t                                m_image_height;
        uint32_t                                m_min_image_count;
        VkSurfaceCapabilitiesKHR                m_capabilities;
        VkRenderPass                            m_render_pass{VK_NULL_HANDLE};
        Ref<Buffers::Image2DBuffer>             m_depth_buffer;
        std::vector<uint32_t>                   m_queue_family_index_collection;
        std::vector<VkImage>                    m_image_collection;
        std::vector<VkImageView>                m_image_view_collection;
        std::vector<VkFramebuffer>              m_framebuffer_collection;
        std::vector<Ref<Primitives::Semaphore>> m_acquired_semaphore_collection;
        std::vector<Primitives::Semaphore*>     m_wait_semaphore_collection;
        std::mutex                              m_image_mutex;
    };
}