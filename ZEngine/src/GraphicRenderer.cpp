#include <pch.h>
#include <Rendering/Renderers/GraphicRenderer.h>

namespace ZEngine::Rendering::Renderers
{
    GraphicRenderer::GraphicRenderer()
    {
        m_renderer_information             = CreateRef<GraphicRendererInformation>();
        // m_uniform_camera_properties_buffer = CreateScope<Buffers::UniformBuffer<float>>();
    }

    void GraphicRenderer::AddMesh(std::vector<Meshes::Mesh>&& meshes)
    {
        m_mesh_collection = std::move(meshes);
    }

    Ref<Buffers::FrameBuffer> GraphicRenderer::GetFrameBuffer() const
    {
        return m_framebuffer;
    }

    Ref<Rendering::Cameras::Camera> GraphicRenderer::GetCamera() const
    {
        return m_camera;
    }

    void GraphicRenderer::StartScene(const Ref<Rendering::Cameras::Camera>& camera)
    {
        m_camera = camera;

        // {
        //     struct internal_camera_data
        //     {
        //         Maths::Vector4 position;
        //         Maths::Matrix4 View;
        //         Maths::Matrix4 Projection;
        //     };
        //     internal_camera_data internal_camera = {Maths::Vector4(camera->GetPosition(), 1.0f), m_camera->GetViewMatrix(), m_camera->GetProjectionMatrix()};
        //     m_uniform_camera_properties_buffer->SetData(reinterpret_cast<void*>(&internal_camera), sizeof(internal_camera_data));
        //     m_uniform_camera_properties_buffer->Bind();
        // }
    }

    const Ref<GraphicRendererInformation>& GraphicRenderer::GetRendererInformation() const
    {
        return m_renderer_information;
    }
} // namespace ZEngine::Rendering::Renderers
