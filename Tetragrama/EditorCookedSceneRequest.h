#pragma once
#include <Tetragrama/EditorSceneCandidate.h>
#include <ZEngine/Rendering/Scenes/CookedSceneResourceRequest.h>

namespace Tetragrama
{
    /// Owns the editor candidate while the engine transaction borrows it.
    class EditorCookedSceneRequest final
    {
    public:
        using State                = ZEngine::Rendering::Scenes::CookedSceneResourceRequest::State;
        EditorCookedSceneRequest() = default;
        ~EditorCookedSceneRequest();
        EditorCookedSceneRequest(const EditorCookedSceneRequest&)            = delete;
        EditorCookedSceneRequest& operator=(const EditorCookedSceneRequest&) = delete;
        [[nodiscard]] bool        Begin(ZEngine::Helpers::Scope<EditorSceneCandidate>&, ZEngine::Managers::CookedCpuClosureCache::Lease&&, ZEngine::ECS::SceneRequestId, uint64_t, const ZEngine::Rendering::Scenes::SceneInitialView&, ZEngine::Core::Memory::IAllocationRoute&, ZEngine::Core::Memory::TLSFSlab&, ZEngine::Rendering::RenderResourceManager&, ZEngine::Hardwares::VulkanDevice&, ZEngine::Rendering::CookedSceneGpuMailbox&, uint32_t, bool pause_after_mesh_record = false);
        [[nodiscard]] State       Tick();
        void                      Cancel() noexcept;
        [[nodiscard]] bool        Take(ZEngine::Helpers::Scope<EditorSceneCandidate>*);
        [[nodiscard]] bool        Reset() noexcept;
        /// @brief Return the current resource request state.
        [[nodiscard]] State       Status() const noexcept;
        /// @brief Report whether the mesh upload was recorded.
        [[nodiscard]] bool        MeshUploadRecorded() const noexcept;

    private:
        ZEngine::Helpers::Scope<EditorSceneCandidate>          m_candidate = {};
        ZEngine::Rendering::Scenes::CookedSceneResourceRequest m_request   = {};
    };
} // namespace Tetragrama
