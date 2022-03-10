#pragma once
#include <Rendering/Buffers/FrameBuffers/Framebuffer.h>
#include <Core/IInitializable.h>
#include <Rendering/Meshes/Mesh.h>
#include <Rendering/Cameras/Camera.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererPipelineContext.h>
#include <Rendering/Renderers/GraphicRendererInformation.h>

namespace ZEngine::Rendering::Renderers::Pipelines {
    class GraphicRendererPipelineContext;
}

namespace ZEngine::Rendering::Renderers {

    class GraphicRenderer : public Core::IInitializable {
    public:
        GraphicRenderer();
        virtual ~GraphicRenderer() = default;

        virtual void AddMesh(Meshes::Mesh& mesh) = 0;

        virtual void AddMesh(Ref<Meshes::Mesh>& mesh);
        virtual void AddMesh(std::vector<Meshes::Mesh>& meshes);
        virtual void AddMesh(std::vector<Ref<Meshes::Mesh>>& meshes);

    public:
        const Ref<Buffers::FrameBuffer>&       GetFrameBuffer() const;
        const Ref<Rendering::Cameras::Camera>& GetCamera() const;
        virtual void                           StartScene(const Ref<Rendering::Cameras::Camera>& camera);
        virtual void                           EndScene() = 0;

        virtual const Ref<GraphicRendererInformation>& GetRendererInformation() const;

    protected:
        Ref<Rendering::Cameras::Camera>                  m_camera;
        Ref<Buffers::FrameBuffer>                        m_framebuffer;
        Ref<GraphicRendererInformation>                  m_renderer_information;
        Scope<Pipelines::GraphicRendererPipelineContext> m_renderer_pipeline_context;
    };
} // namespace ZEngine::Rendering::Renderers
