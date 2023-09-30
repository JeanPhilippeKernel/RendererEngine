#include <pch.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    RenderPass::~RenderPass()
    {
        Dispose();
    }

    void RenderPass::Dispose()
    {
        m_pipeline->Dispose();
    }

    Ref<Pipelines::GraphicPipeline> RenderPass::GetPipeline() const
    {
        return m_pipeline;
    }

    void RenderPass::Bake()
    {
        m_pipeline->Bake();
    }

    void RenderPass::SetInput(std::string_view key_name, const Ref<Rendering::Buffers::UniformBuffer>& buffer)
    {
        m_pipeline->SetUniformBuffer(key_name, buffer);
    }

    void RenderPass::SetInput(std::string_view key_name, const Ref<Rendering::Buffers::StorageBuffer>& buffer)
    {
        m_pipeline->SetStorageBuffer(key_name, buffer);
    }

    Ref<RenderPass> RenderPass::Create(const RenderPassSpecification& specification)
    {
        Ref<RenderPass> render_pass = CreateRef<RenderPass>();
        render_pass->m_pipeline     = specification.Pipeline;
        return render_pass;
    }
} // namespace ZEngine::Rendering::Renderers::RenderPasses