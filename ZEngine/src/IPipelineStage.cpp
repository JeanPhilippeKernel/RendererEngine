#include <Core/IPipeline.h>

namespace ZEngine::Core
{

    void IPipelineStage::SetContext(IPipelineContext* const context)
    {
        if (context)
        {
            m_context = context;
        }
    }

    void IPipelineStage::Next()
    {
        if (m_context)
        {
            m_context->UpdateStage(m_next_stage);
        }
    }

    bool IPipelineStage::HasNext()
    {
        return m_next_stage != nullptr;
    }
} // namespace ZEngine::Core
