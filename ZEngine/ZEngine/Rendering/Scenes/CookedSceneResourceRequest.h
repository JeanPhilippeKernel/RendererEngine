#pragma once
#include <ZEngine/Managers/CookedCpuClosureCache.h>
#include <ZEngine/Rendering/CookedSceneGpuMailbox.h>
#include <ZEngine/Rendering/Scenes/SceneResourcePreparation.h>

namespace ZEngine::ECS
{
    class SceneInstance;
}

namespace ZEngine::Rendering::Scenes
{
    /// Owner-thread transaction for an unpublished cooked scene. The caller
    /// keeps the scene, render adapter, mesh span and services alive until a
    /// terminal result or TakeReady; cancellation must drain its mailbox ticket.
    class CookedSceneResourceRequest final
    {
    public:
        enum class State : uint8_t
        {
            Empty,
            Collecting,
            GpuPending,
            ResourcePending,
            Cancelling,
            Ready,
            Failed,
            Cancelled
        };
        CookedSceneResourceRequest() = default;
        ~CookedSceneResourceRequest();
        CookedSceneResourceRequest(const CookedSceneResourceRequest&)            = delete;
        CookedSceneResourceRequest& operator=(const CookedSceneResourceRequest&) = delete;

        [[nodiscard]] bool          Begin(ECS::SceneInstance&, RenderScene&, SceneResourcePreparation&, std::span<const uuids::uuid>, Managers::CookedCpuClosureCache::Lease&&, ECS::SceneRequestId, uint64_t, const SceneInitialView&, Core::Memory::IAllocationRoute&, Core::Memory::TLSFSlab&, RenderResourceManager&, Hardwares::VulkanDevice&, CookedSceneGpuMailbox&, uint32_t, bool pause_after_mesh_record = false);
        [[nodiscard]] State         Tick();
        void                        Cancel() noexcept;
        [[nodiscard]] bool          TakeReady() noexcept;
        /// Call after retiring a failed/cancelled candidate; preserves status.
        void                        ReleaseTerminalAssets() noexcept;
        [[nodiscard]] bool          Reset() noexcept;
        [[nodiscard]] State         Status() const noexcept
        {
            return m_state;
        }
        [[nodiscard]] bool MeshUploadRecorded() const noexcept
        {
            return m_mesh_upload_recorded;
        }

    private:
        void                                   Finish(State) noexcept;
        ECS::SceneInstance*                    m_instance                = nullptr;
        RenderScene*                           m_render                  = nullptr;
        SceneResourcePreparation*              m_preparation             = nullptr;
        std::span<const uuids::uuid>           m_meshes                  = {};
        Managers::CookedCpuClosureCache::Lease m_assets                  = {};
        Core::Memory::IAllocationRoute*        m_route                   = nullptr;
        Core::Memory::TLSFSlab*                m_metadata                = nullptr;
        RenderResourceManager*                 m_resources               = nullptr;
        Hardwares::VulkanDevice*               m_device                  = nullptr;
        CookedSceneGpuMailbox*                 m_mailbox                 = nullptr;
        CookedSceneGpuMailbox::Ticket          m_ticket                  = {};
        ECS::SceneRequestId                    m_request                 = {};
        SceneInitialView                       m_view                    = {};
        uint64_t                               m_generation              = 0;
        uint32_t                               m_max_textures            = 0;
        bool                                   m_pause_after_mesh_record = false;
        bool                                   m_mesh_upload_recorded    = false;
        State                                  m_after_cancel            = State::Cancelled;
        State                                  m_state                   = State::Empty;
    };
} // namespace ZEngine::Rendering::Scenes
