#include <ZEngine/ECS/SceneInstance.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Scenes/CookedSceneResourceRequest.h>
#include <limits>
#include <utility>

namespace ZEngine::Rendering::Scenes
{
    CookedSceneResourceRequest::~CookedSceneResourceRequest()
    {
        const bool reset = Reset();
        ZENGINE_VALIDATE_ASSERT(reset, "Cooked scene request destroyed before cancellation completed")
        (void) reset;
    }

    bool CookedSceneResourceRequest::Begin(
        ZEngine::ECS::SceneInstance&                        instance,
        RenderScene&                                        render,
        SceneResourcePreparation&                           preparation,
        std::span<const uuids::uuid>                        meshes,
        ZEngine::Managers::CookedCpuClosureCache::Lease&&   assets,
        ZEngine::ECS::SceneRequestId                        request,
        uint64_t                                            generation,
        const ZEngine::Rendering::Scenes::SceneInitialView& view,
        ZEngine::Core::Memory::IAllocationRoute&            route,
        ZEngine::Core::Memory::TLSFSlab&                    metadata,
        ZEngine::Rendering::RenderResourceManager&          resources,
        ZEngine::Hardwares::VulkanDevice&                   device,
        ZEngine::Rendering::CookedSceneGpuMailbox&          mailbox,
        uint32_t                                            max_textures,
        bool                                                pause_after_mesh_record)
    {
        ZEngine::Managers::CookedCpuClosureCache::Result result = {};
        if (m_state != State::Empty || !request || !generation || generation == std::numeric_limits<uint64_t>::max() || device.RRM != &resources || !assets.Poll(&result) || result.StateValue != ZEngine::Managers::CookedCpuClosureCache::State::Ready || !result.Assets || !instance.MatchesCookedClosure(result.Assets->ManifestDigest(), result.Assets->SceneUUID()) || preparation.HasPreparation() || max_textures > ZEngine::Rendering::RenderResourceManager::SceneCookedTextureCapacity)
        {
            return false;
        }
        m_instance                = &instance;
        m_render                  = &render;
        m_preparation             = &preparation;
        m_meshes                  = meshes;
        m_assets                  = std::move(assets);
        m_request                 = request;
        m_generation              = generation;
        m_view                    = view;
        m_route                   = &route;
        m_metadata                = &metadata;
        m_resources               = &resources;
        m_device                  = &device;
        m_mailbox                 = &mailbox;
        m_max_textures            = max_textures;
        m_pause_after_mesh_record = pause_after_mesh_record;
        m_mesh_upload_recorded    = false;
        m_state                   = State::Collecting;
        return true;
    }

    void CookedSceneResourceRequest::Finish(State outcome) noexcept
    {
        m_state = outcome;
    }

    void CookedSceneResourceRequest::ReleaseTerminalAssets() noexcept
    {
        if (m_state == State::Failed || m_state == State::Cancelled)
            m_assets.Reset();
    }

    void CookedSceneResourceRequest::Cancel() noexcept
    {
        if (m_state == State::Empty || m_state == State::Failed || m_state == State::Cancelled || m_state == State::Cancelling)
            return;
        if (m_ticket)
        {
            (void) m_mailbox->Cancel(m_ticket);
            m_after_cancel = State::Cancelled;
            m_state        = State::Cancelling;
            return;
        }
        Finish(State::Cancelled);
    }

    CookedSceneResourceRequest::State CookedSceneResourceRequest::Tick()
    {
        using Mailbox  = ZEngine::Rendering::CookedSceneGpuMailbox;
        using Resource = ZEngine::Rendering::RenderResourceManager;
        if (m_state == State::GpuPending || m_state == State::Cancelling)
        {
            m_mesh_upload_recorded   |= m_mailbox->MeshUploadRecorded(m_ticket);
            Mailbox::Outcome outcome  = {};
            if (!m_mailbox->Poll(m_ticket, &outcome))
            {
                // Ticket loss violates the owner contract; do not discard a
                // candidate that might still be borrowed by the render owner.
                return m_state;
            }
            if (outcome == Mailbox::Outcome::Pending)
                return m_state;
            if (!m_mailbox->Acknowledge(m_ticket))
                return m_state;
            m_ticket = {};
            if (m_state == State::Cancelling)
            {
                Finish(m_after_cancel);
            }
            else if (outcome == Mailbox::Outcome::Submitted)
            {
                m_state = State::ResourcePending;
            }
            else
            {
                Finish(State::Failed);
            }
        }
        if (m_state == State::Collecting)
        {
            ZEngine::Managers::CookedCpuClosureCache::Result result = {};
            if (m_resources->SceneResourceAdmissionSealed() || !m_assets.Poll(&result) || result.StateValue != ZEngine::Managers::CookedCpuClosureCache::State::Ready || !result.Assets)
            {
                Finish(State::Failed);
                return m_state;
            }
            bool terminal_failure = false;
            if (!m_preparation->Prepare(*m_instance, *m_render, m_meshes, *m_resources, m_request, m_generation, m_view, result.Assets, &terminal_failure))
            {
                if (terminal_failure)
                    Finish(State::Failed);
                return m_state;
            }
            Resource::SceneResourcePreparationResult collecting = {};
            if (!m_preparation->Poll(&collecting) || collecting.State == Resource::SceneResourcePreparationState::Failed || collecting.State == Resource::SceneResourcePreparationState::Releasing)
            {
                Finish(State::Failed);
                return m_state;
            }
            if (!m_preparation->InstancesCollected(m_render->Instances.size()))
                return m_state;
            const auto&                            sky         = m_render->Sky;
            const ZEngine::Cook::CookAssetIdentity environment = sky.IsHDRI() ? ZEngine::Cook::CookAssetIdentity{ZEngine::Cook::CookAssetKind::EnvironmentMap, sky.EnvironmentMap} : ZEngine::Cook::CookAssetIdentity{};
            const auto                             admission   = m_mailbox->TryBegin(m_assets, m_meshes, environment, *m_route, *m_metadata, *m_resources, *m_device, m_preparation->InitialResources(), m_max_textures, &m_ticket, m_pause_after_mesh_record);
            if (admission == Mailbox::Admission::Failed)
            {
                Finish(State::Failed);
            }
            else if (admission == Mailbox::Admission::Queued)
            {
                m_state = State::GpuPending;
            }
        }
        if (m_state == State::ResourcePending)
        {
            Resource::SceneResourcePreparationResult result = {};
            if (!m_preparation->Poll(&result) || result.State == Resource::SceneResourcePreparationState::Failed || result.State == Resource::SceneResourcePreparationState::Releasing)
            {
                ZENGINE_CORE_ERROR("[CookedSceneResourceRequest] Resource preparation failed: state={} error={} material_error={} environment={}", static_cast<unsigned>(result.State), static_cast<unsigned>(result.Error), static_cast<unsigned>(result.MaterialError), static_cast<unsigned>(result.Environment))
                Finish(State::Failed);
            }
            else if (result.State == Resource::SceneResourcePreparationState::ResourcesRetained)
            {
                m_state = State::Ready;
            }
        }
        return m_state;
    }

    bool CookedSceneResourceRequest::TakeReady() noexcept
    {
        if (m_state != State::Ready)
            return false;
        m_assets.Reset();
        m_state = State::Empty;
        return Reset();
    }

    bool CookedSceneResourceRequest::Reset() noexcept
    {
        if (m_ticket || (m_state != State::Empty && m_state != State::Failed && m_state != State::Cancelled))
            return false;
        m_assets.Reset();
        m_instance                = nullptr;
        m_render                  = nullptr;
        m_preparation             = nullptr;
        m_meshes                  = {};
        m_route                   = nullptr;
        m_metadata                = nullptr;
        m_resources               = nullptr;
        m_device                  = nullptr;
        m_mailbox                 = nullptr;
        m_request                 = {};
        m_generation              = 0;
        m_max_textures            = 0;
        m_pause_after_mesh_record = false;
        m_mesh_upload_recorded    = false;
        m_state                   = State::Empty;
        return true;
    }
} // namespace ZEngine::Rendering::Scenes
