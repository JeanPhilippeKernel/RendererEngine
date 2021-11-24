#include <pch.h>
#include <Rendering/Shaders/Compilers/ICompilerStage.h>

namespace ZEngine::Rendering::Shaders::Compilers {

    void ICompilerStage::Next() {
        if (m_compiler_context) {
            m_compiler_context->UpdateStage(m_next_stage);
        }
    }

    bool ICompilerStage::HasNext() {
        return m_next_stage != nullptr;
    }
} // namespace ZEngine::Rendering::Shaders::Compilers
