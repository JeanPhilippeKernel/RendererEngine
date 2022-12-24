#pragma once
#include <Rendering/Buffers/FrameBuffers/Framebuffer.h>
#include <Core/IInitializable.h>
#include <Rendering/Meshes/Mesh.h>
#include <Rendering/Cameras/Camera.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererPipelineContext.h>
#include <Rendering/Renderers/GraphicRendererInformation.h>
// #include <Rendering/Buffers/UniformBuffer.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    class GraphicRendererPipelineContext;
}

namespace ZEngine::Rendering::Renderers
{

    class GraphicRenderer : public Core::IInitializable
    {
    public:
        GraphicRenderer();
        virtual ~GraphicRenderer() = default;

        virtual void AddMesh(std::vector<Meshes::Mesh>&& meshes);

    public:
        Ref<Buffers::FrameBuffer>       GetFrameBuffer() const;
        Ref<Rendering::Cameras::Camera> GetCamera() const;
        virtual void                    StartScene(const Ref<Rendering::Cameras::Camera>& camera);
        virtual void                    EndScene() = 0;

        virtual const Ref<GraphicRendererInformation>& GetRendererInformation() const;

    protected:
        std::vector<Rendering::Meshes::Mesh>             m_mesh_collection;
        Ref<Rendering::Cameras::Camera>                  m_camera;
        Ref<Buffers::FrameBuffer>                        m_framebuffer;
        Ref<GraphicRendererInformation>                  m_renderer_information;
        Scope<Pipelines::GraphicRendererPipelineContext> m_renderer_pipeline_context;

    private:
        // Scope<Buffers::UniformBuffer<Maths::Vector4>> m_uniform_camera_properties_buffer;
    };
} // namespace ZEngine::Rendering::Renderers
