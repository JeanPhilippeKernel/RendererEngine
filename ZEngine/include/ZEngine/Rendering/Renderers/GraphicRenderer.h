#pragma once
#include <vulkan/vulkan.h>
#include <Rendering/Swapchain.h>
#include <Rendering/Buffers/Framebuffer.h>

namespace ZEngine::Rendering::Renderers
{
    enum RenderTarget : uint32_t
    {
        FRAME_OUTPUT = 0,
        COUNT
    };

    struct DrawData
    {
        uint32_t Index{0xFFFFFFFF};
        uint32_t TransformIndex{0xFFFFFFFF};
        uint32_t MaterialIndex{0xFFFFFFFF};
        uint32_t VertexOffset;
        uint32_t IndexOffset;
        uint32_t VertexCount;
        uint32_t IndexCount;
    };

    struct RendererInformation
    {
        uint32_t FrameCount{0xFFFFFFFF};
        uint32_t CurrentFrameIndex{0xFFFFFFFF};
        uint64_t SwapchainIdentifier{0xFFFFFFFF};
    };

    struct GraphicRenderer
    {
        GraphicRenderer()                       = delete;
        GraphicRenderer(const GraphicRenderer&) = delete;
        ~GraphicRenderer()                      = delete;

        static void Initialize();
        static void Deinitialize();

        static Ref<Buffers::FramebufferVNext> GetRenderTarget(RenderTarget target);
        static Ref<Buffers::FramebufferVNext> GetFrameOutput();

        static void SetViewportSize(uint32_t width, uint32_t height);
        static void SetMainSwapchain(const Ref<Rendering::Swapchain>& swapchain);

        static const RendererInformation& GetRendererInformation();

    private:
        static void RebuildRenderTargets();

    private:
        static uint32_t                                                        s_viewport_width;
        static uint32_t                                                        s_viewport_height;
        static RendererInformation                                             s_renderer_information;
        static WeakRef<Rendering::Swapchain>                                   s_main_window_swapchain;
        static std::array<Ref<Buffers::FramebufferVNext>, RenderTarget::COUNT> s_render_target_collection;
    };
} // namespace ZEngine::Rendering::Renderers
