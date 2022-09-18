#include <pch.h>
#include <Rendering/Renderers/GraphicRenderer3D.h>
#include <Rendering/Buffers/FrameBuffers/FrameBufferSpecification.h>

using namespace ZEngine::Rendering::Buffers::FrameBuffers;

namespace ZEngine::Rendering::Renderers
{

    GraphicRenderer3D::GraphicRenderer3D() : GraphicRenderer()
    {
        m_renderer_pipeline_context                = std::make_unique<Pipelines::GraphicRendererPipelineContext>(this);
        m_renderer_information->GraphicStorageType = Storages::GraphicRendererStorageType::GRAPHIC_3D_STORAGE_TYPE;

        Buffers::FrameBufferSpecification framebuffer_specification;
        framebuffer_specification.Width                         = 1080;
        framebuffer_specification.Height                        = 800;
        framebuffer_specification.Samples                       = 1;
        framebuffer_specification.DepthAttachmentSpecification  = {FrameBufferDepthAttachmentTextureFormat::DEPTH32FSTENCIL8};
        framebuffer_specification.ColorAttachmentSpecifications = {
            {FrameBufferColorAttachmentTextureFormat::RGBA8, nullptr, GL_RGBA}, {FrameBufferColorAttachmentTextureFormat::RED_INTEGER, nullptr, GL_INT}};

        m_framebuffer = CreateRef<Buffers::FrameBuffer>(framebuffer_specification);
    }

    void GraphicRenderer3D::Initialize()
    {
        // enable management of transparent background image (RGBA-8)
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // enable Z-depth and stencil testing
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_STENCIL_TEST);
    }

    void GraphicRenderer3D::EndScene()
    {
        m_renderer_pipeline_context->SetPipelineData(std::move(m_mesh_collection));
        m_renderer_pipeline_context->Execute();
    }
} // namespace ZEngine::Rendering::Renderers
