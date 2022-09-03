#include <pch.h>
#include <Rendering/Renderers/GraphicRenderer3D.h>
#include <Rendering/Buffers/FrameBuffers/FrameBufferSpecification.h>

namespace ZEngine::Rendering::Renderers {

    GraphicRenderer3D::GraphicRenderer3D() : GraphicRenderer() {
        m_renderer_pipeline_context                = std::make_unique<Pipelines::GraphicRendererPipelineContext>(this);
        m_renderer_information->GraphicStorageType = Storages::GraphicRendererStorageType::GRAPHIC_3D_STORAGE_TYPE;

        Buffers::FrameBufferSpecification spec;
        spec.HasDepth   = true;
        spec.HasStencil = true;
        spec.Width      = 1080;
        spec.Height     = 800;

        m_framebuffer.reset(new Buffers::FrameBuffer(spec));
    }

    void GraphicRenderer3D::Initialize() {
        // enable management of transparent background image (RGBA-8)
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // enable Z-depth and stencil testing
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_STENCIL_TEST);
    }

    void GraphicRenderer3D::EndScene() {
        m_renderer_pipeline_context->SetPipelineData(std::move(m_mesh_collection));
        m_renderer_pipeline_context->Execute();
    }
} // namespace ZEngine::Rendering::Renderers
