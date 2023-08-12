#pragma once
#include <ZEngineDef.h>
#include <Rendering/Swapchain.h>
#include <Rendering/Pools/CommandPool.h>

namespace ZEngine::Rendering::Renderers
{
    struct ImGUIRenderer
    {
        ImGUIRenderer()                     = delete;
        ImGUIRenderer(const ImGUIRenderer&) = delete;
        ~ImGUIRenderer()                    = delete;

        static void Initialize(void* window, const Ref<Swapchain>& swapchain);
        static void Deinitialize();

        static void BeginFrame();
        static void Draw(uint32_t window_width, uint32_t window_height, const Ref<Swapchain>& swapchain, void* draw_data);
        static void Submit();
        static void EndFrame();

    private:
        static void __ImGUIDestroyFontUploadObjects();
        static void __ImGUIRenderData(void* data, VkCommandBuffer command_buffer, VkPipeline pipeline);
        static void __ImGUICreateOrResizeBuffer(VkBuffer& buffer, VkDeviceMemory& buffer_memory, VkDeviceSize& p_buffer_size, size_t new_size, VkBufferUsageFlagBits usage);
        static Rendering::Buffers::CommandBuffer* s_command_buffer;
        static Rendering::Pools::CommandPool*     s_ui_command_pool;
    };

}