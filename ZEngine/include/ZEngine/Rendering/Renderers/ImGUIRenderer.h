#pragma once
#include <ZEngineDef.h>
#include <Rendering/Swapchain.h>
#include <Rendering/Pools/CommandPool.h>

namespace ZEngine::Rendering::Renderers
{
    struct ImguiViewPortWindowChild
    {
        ImguiViewPortWindowChild(void* viewport_handle);
        ~ImguiViewPortWindowChild() = default;
        void*                     RendererUserData{nullptr};
        Ref<Rendering::Swapchain> Swapchain;
        Pools::CommandPool*       CommandPool{nullptr};
    };

    struct ImGUIRenderer
    {
        ImGUIRenderer()                     = delete;
        ImGUIRenderer(const ImGUIRenderer&) = delete;
        ~ImGUIRenderer()                    = delete;

        static void Initialize(void* window, const Ref<Swapchain>& swapchain);
        static void Deinitialize();

        static void BeginFrame();
        static void Draw(uint32_t window_width, uint32_t window_height, const Ref<Swapchain>& swapchain, void** user_data, void* draw_data);
        static void DrawChildWindow(uint32_t width, uint32_t height, ImguiViewPortWindowChild** window_child, void* draw_data);
        static void EndFrame();

    private:
        static void __ImGUIDestroyFontUploadObjects();
        static void __ImGUIRenderData(void* data, VkCommandBuffer command_buffer, VkPipeline pipeline);
        static void __ImGUICreateOrResizeBuffer(VkBuffer& buffer, VkDeviceMemory& buffer_memory, VkDeviceSize& p_buffer_size, size_t new_size, VkBufferUsageFlagBits usage);
        static void __ImGUIRenderDataChildViewport(void* data, void** user_data, VkCommandBuffer command_buffer, VkPipeline pipeline);
        static Rendering::Buffers::CommandBuffer* s_command_buffer;
        static Rendering::Pools::CommandPool*     s_ui_command_pool;
        static VkDescriptorPool                   s_descriptor_pool;
    };

} // namespace ZEngine::Rendering::Renderers
