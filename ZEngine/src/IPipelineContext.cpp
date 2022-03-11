#include <Core/IPipeline.h>

namespace ZEngine::Core {

    void IPipelineContext::UpdateStage(Ref<IPipelineStage> stage) {
        m_stage = stage;
        m_stage->SetContext(this);
    }

    void IPipelineContext::UpdateStage(IPipelineStage* const stage) {
        m_stage.reset(stage);
        m_stage->SetContext(this);
    }
} // namespace ZEngine::Core
