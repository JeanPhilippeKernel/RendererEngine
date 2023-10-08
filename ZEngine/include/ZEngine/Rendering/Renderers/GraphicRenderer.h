#pragma once
#include <vulkan/vulkan.h>
#include <Rendering/Renderers/GraphicRendererInformation.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>
#include <Rendering/Buffers/Framebuffer.h>
#include <Rendering/Buffers/IndirectBuffer.h>

namespace ZEngine::Rendering::Renderers
{
    enum RenderTarget : uint32_t
    {
        FRAME_OUTPUT = 0,
        Count
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

    struct GraphicRenderer
    {
        GraphicRenderer()                       = delete;
        GraphicRenderer(const GraphicRenderer&) = delete;
        ~GraphicRenderer()                      = delete;

        static void Initialize();
        static void Deinitialize();

        static void                           BeginRenderPass(Buffers::CommandBuffer* const command_buffer, const Ref<RenderPasses::RenderPass>&);
        static void                           EndRenderPass(Buffers::CommandBuffer* const command_buffer);
        static void                           RenderGeometry(Buffers::CommandBuffer* const command_buffer, const Ref<Buffers::IndirectBuffer>& buffer, uint32_t count);
        static Ref<Buffers::FramebufferVNext> GetRenderTarget(RenderTarget target);
        static Ref<Buffers::FramebufferVNext> GetFrameOutput();

        static void SetViewportSize(uint32_t width, uint32_t height);

        virtual const Ref<GraphicRendererInformation>& GetRendererInformation() const;

    private:
        static void RebuildRenderTargets();

    private:
        static uint32_t                                                        s_viewport_width;
        static uint32_t                                                        s_viewport_height;
        static std::array<Ref<Buffers::FramebufferVNext>, RenderTarget::Count> s_render_target_collection;
        Ref<GraphicRendererInformation>                                        m_renderer_information;
    };
} // namespace ZEngine::Rendering::Renderers
