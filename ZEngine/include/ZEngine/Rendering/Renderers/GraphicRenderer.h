#pragma once
#include <vulkan/vulkan.h>
#include <Rendering/Swapchain.h>
#include <Rendering/Buffers/Framebuffer.h>
#include <Rendering/Renderers/SceneRenderer.h>
#include <Rendering/Renderers/ImGUIRenderer.h>

namespace ZEngine::Rendering::Renderers
{
    enum RenderTarget : uint32_t
    {
        FRAME_OUTPUT = 0,
        ENVIROMENT_CUBEMAP,
        COUNT
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

        static void Update();
        static void Upload();

        static void DrawScene(const Ref<Rendering::Cameras::Camera>& camera, const Ref<Rendering::Scenes::SceneRawData>& data);

        static void BeginImguiFrame();
        static void DrawUIFrame();
        static void EndImguiFrame();
        static VkDescriptorSet GetImguiFrameOutput();

    private:
        static uint32_t                                                        s_viewport_width;
        static uint32_t                                                        s_viewport_height;
        static RendererInformation                                             s_renderer_information;
        static WeakRef<Rendering::Swapchain>                                   s_main_window_swapchain;
        static std::array<Ref<Buffers::FramebufferVNext>, RenderTarget::COUNT> s_render_target_collection;
        static Ref<Buffers::UniformBufferSet>                                  s_UBCamera;
        static Pools::CommandPool*                                             s_command_pool;
        static Buffers::CommandBuffer*                                         s_current_command_buffer;
        static Buffers::CommandBuffer*                                         s_current_command_buffer_ui;
        static Ref<SceneRenderer>                                              s_scene_renderer;
        static Ref<ImGUIRenderer>                                              s_imgui_renderer;
    };
} // namespace ZEngine::Rendering::Renderers
