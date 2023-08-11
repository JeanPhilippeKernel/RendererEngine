#pragma once
#include <Rendering/Cameras/Camera.h>
#include <Rendering/Renderers/GraphicRendererInformation.h>
#include <Rendering/Renderers/Contracts/RendererDataContract.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>

namespace ZEngine::Rendering::Renderers
{
    struct GraphicRenderer
    {
        GraphicRenderer()  = default;
        ~GraphicRenderer() = default;
        static void StartScene(Ref<Cameras::Camera> scene_camera);
        static void Submit(uint32_t mesh_idx);
        static void EndScene();

        static void Initialize();
        static void Deinitialize();

        static void BeginRenderPass(const Buffers::CommandBuffer& command_buffer, const Ref<RenderPasses::RenderPass>&);
        static void EndRenderPass(const Buffers::CommandBuffer& command_buffer);

        virtual const Ref<GraphicRendererInformation>& GetRendererInformation() const;

        virtual Contracts::FramebufferViewLayout GetOutputImage(uint32_t frame_index);

    private:
        static Pools::CommandPool*                                                                         m_command_pool;
        static Ref<Buffers::UniformBuffer>                                                                 m_UBOCamera;
        static Ref<RenderPasses::RenderPass>                                                               m_final_color_output_pass;
        static Contracts::GraphicSceneLayout                                                               m_scene_information;
        Ref<GraphicRendererInformation>                                                                    m_renderer_information;
    };
} // namespace ZEngine::Rendering::Renderers
