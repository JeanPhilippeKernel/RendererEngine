#pragma once
#include <Rendering/Buffers/Image2DBuffer.h>
#include <Rendering/Primitives/Semaphore.h>
#include <Rendering/Renderers/RenderPasses/Attachment.h>
#include <vulkan/vulkan.h>
#include <vector>

namespace ZEngine::Rendering
{
    class Swapchain : public Helpers::RefCounted
    {
    public:
        Swapchain(void* native_window, bool is_surface_from_device = true);
        ~Swapchain();

        void Resize();
        void Present();

        uint32_t       GetMinImageCount() const;
        uint32_t       GetImageCount() const;
        VkRenderPass   GetRenderPass() const;
        VkFramebuffer  GetCurrentFramebuffer();
        uint32_t       GetCurrentFrameIndex();
        VkSwapchainKHR GetHandle() const;
        uint64_t       GetIdentifier() const;

        Ref<Renderers::RenderPasses::Attachment> GetAttachment() const;

    private:
        void Create();
        void Dispose();

        void*                                    m_native_window{nullptr};
        VkSurfaceKHR                             m_surface{VK_NULL_HANDLE};
        VkSurfaceFormatKHR                       m_surface_format;
        bool                                     m_is_surface_from_device;
        uint32_t                                 m_current_frame_index{0};
        uint32_t                                 m_last_frame_image_index{0};
        VkSwapchainKHR                           m_handle{VK_NULL_HANDLE};
        uint32_t                                 m_image_width{0};
        uint32_t                                 m_image_height{0};
        uint32_t                                 m_min_image_count{0};
        uint32_t                                 m_image_count{0};
        VkSurfaceCapabilitiesKHR                 m_capabilities{};
        Ref<Renderers::RenderPasses::Attachment> m_attachment;
        std::vector<uint32_t>                    m_queue_family_index_collection;
        std::vector<VkImage>                     m_image_collection;
        std::vector<VkImageView>                 m_image_view_collection;
        std::vector<VkFramebuffer>               m_framebuffer_collection;
        std::vector<Ref<Primitives::Semaphore>>  m_acquired_semaphore_collection;
        std::vector<Ref<Primitives::Semaphore>>  m_render_complete_semaphore_collection;
        std::vector<Ref<Primitives::Fence>>      m_frame_signal_fence_collection;
        std::vector<Primitives::Semaphore*>      m_wait_semaphore_collection;
        std::mutex                               m_image_mutex;
        uint64_t                                 m_identifier{0};
    };
} // namespace ZEngine::Rendering