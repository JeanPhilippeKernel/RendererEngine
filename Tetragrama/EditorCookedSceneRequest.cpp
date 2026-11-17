#include <Tetragrama/EditorCookedSceneRequest.h>

namespace Tetragrama
{
    EditorCookedSceneRequest::~EditorCookedSceneRequest()
    {
        const bool reset = Reset();
        ZENGINE_VALIDATE_ASSERT(reset, "Cooked scene request destroyed before cancellation completed")
        (void) reset;
    }

    bool EditorCookedSceneRequest::Begin(ZEngine::Helpers::Scope<EditorSceneCandidate>& candidate, ZEngine::Managers::CookedCpuClosureCache::Lease&& assets, ZEngine::ECS::SceneRequestId request, uint64_t generation, const ZEngine::Rendering::Scenes::SceneInitialView& view, ZEngine::Core::Memory::IAllocationRoute& route, ZEngine::Core::Memory::TLSFSlab& metadata, ZEngine::Rendering::RenderResourceManager& resources, ZEngine::Hardwares::VulkanDevice& device, ZEngine::Rendering::CookedSceneGpuMailbox& mailbox, uint32_t max_textures, bool pause_after_mesh_record)
    {
        if (!candidate || m_candidate || !m_request.Begin(candidate->Instance(), candidate->Render(), candidate->ResourcePreparation(), candidate->RequiredMeshes(), std::move(assets), request, generation, view, route, metadata, resources, device, mailbox, max_textures, pause_after_mesh_record))
            return false;
        m_candidate = std::move(candidate);
        return true;
    }

    EditorCookedSceneRequest::State EditorCookedSceneRequest::Tick()
    {
        const auto state = m_request.Tick();
        if (state == State::Failed || state == State::Cancelled)
        {
            m_candidate.reset();
            m_request.ReleaseTerminalAssets();
        }
        return state;
    }

    void EditorCookedSceneRequest::Cancel() noexcept
    {
        m_request.Cancel();
        if (m_request.Status() == State::Cancelled)
        {
            m_candidate.reset();
            m_request.ReleaseTerminalAssets();
        }
    }

    bool EditorCookedSceneRequest::Take(ZEngine::Helpers::Scope<EditorSceneCandidate>* output)
    {
        if (!output || *output || !m_request.TakeReady())
            return false;
        *output = std::move(m_candidate);
        return true;
    }

    bool EditorCookedSceneRequest::Reset() noexcept
    {
        if (!m_request.Reset())
            return false;
        m_candidate.reset();
        return true;
    }
} // namespace Tetragrama
