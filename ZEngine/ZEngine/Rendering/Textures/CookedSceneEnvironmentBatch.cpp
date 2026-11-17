#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Textures/CookedSceneEnvironmentBatch.h>
#include <utility>

namespace ZEngine::Rendering::Textures
{
    CookedSceneEnvironmentBatch::~CookedSceneEnvironmentBatch()
    {
        Reset();
    }

    bool CookedSceneEnvironmentBatch::Initialize(Managers::CookedCpuClosureCache::Lease&& lease, const Cook::CookAssetIdentity& identity, RenderResourceManager& manager, Hardwares::VulkanDevice& device, RenderResourceManager::SceneResourcePreparationId scene, bool transfer_source)
    {
        Managers::CookedCpuClosureCache::Result               result      = {};
        RenderResourceManager::SceneResourcePreparationResult preparation = {};
        RenderResourceManager::SceneEnvironmentPreparation    environment = {};
        if (m_state != State::Empty || !scene || device.RRM != &manager || identity.Kind != Cook::CookAssetKind::EnvironmentMap || !identity.IsValid() || !lease.Poll(&result) || result.StateValue != Managers::CookedCpuClosureCache::State::Ready || !result.Assets || !result.Assets->FindEnvironment(identity) || !manager.PollSceneResourcePreparation(scene, &preparation) || !preparation.Cooked || preparation.CookedManifest != result.Assets->ManifestDigest() || preparation.CookedScene != result.Assets->SceneUUID() ||
            preparation.State != RenderResourceManager::SceneResourcePreparationState::Preparing || !manager.GetSceneEnvironmentPreparation(scene, &environment) || environment.CookedSource.Valid() || !environment.Input.Sky.IsHDRI() || environment.Input.Sky.EnvironmentMap != identity.UUID)
        {
            return false;
        }
        m_lease           = std::move(lease);
        m_manager         = &manager;
        m_device          = &device;
        m_scene           = scene;
        m_identity        = identity;
        m_transfer_source = transfer_source;
        m_state           = State::Pending;
        return true;
    }

    CookedSceneEnvironmentBatch::State CookedSceneEnvironmentBatch::Step(uint8_t frame_index, uint8_t thread_index)
    {
        if (m_state != State::Pending)
        {
            return m_state;
        }
        Managers::CookedCpuClosureCache::Result               result = {};
        RenderResourceManager::SceneResourcePreparationResult scene  = {};
        if (!m_lease.Poll(&result) || result.StateValue != Managers::CookedCpuClosureCache::State::Ready || !result.Assets || m_device->IsDeviceLost.load(std::memory_order_acquire) || !m_manager->PollSceneResourcePreparation(m_scene, &scene) || !scene.Cooked || scene.State != RenderResourceManager::SceneResourcePreparationState::Preparing)
        {
            return m_state = State::Failed;
        }
        switch (m_upload.State())
        {
            case CookedEnvironmentUpload::Phase::Empty:
            {
                Managers::CookedCpuClosureCache::Lease retained = {};
                if (!m_lease.Retain(&retained) || !m_upload.Capture(std::move(retained), m_identity))
                {
                    return m_state = State::Failed;
                }
                const auto*                          header = m_upload.Header();
                Specifications::TextureSpecification spec   = {};
                spec.Width                                  = header->FaceWidth;
                spec.Height                                 = header->FaceHeight;
                spec.BytePerPixel                           = sizeof(float) * 4;
                spec.LayerCount                             = header->LayerCount;
                spec.MipLevelCount                          = header->MipCount;
                spec.Format                                 = Specifications::ImageFormat::R32G32B32A32_SFLOAT;
                spec.IsCubemap                              = true;
                spec.IsCookedAsset                          = true;
                spec.IsUsageStorage                         = true;
                spec.IsUsageTransferSource                  = m_transfer_source;
                m_target                                    = m_device->TryCreateTexture(spec, "cooked-scene-environment");
                if (!m_target.Valid() || !m_upload.Prepare(*m_manager))
                {
                    return m_state = State::Failed;
                }
                break;
            }
            case CookedEnvironmentUpload::Phase::Staging:
                if (!m_upload.StageOne())
                {
                    return m_state = State::Failed;
                }
                break;
            case CookedEnvironmentUpload::Phase::Staged:
                (void) m_upload.Submit(frame_index, thread_index, m_target);
                break;
            case CookedEnvironmentUpload::Phase::Submitted:
            {
                const auto state = m_manager->GetTextureDecodeState(m_target);
                if (state == RenderResourceManager::TextureDecodeState::Failed)
                {
                    return m_state = State::Failed;
                }
                if (state != RenderResourceManager::TextureDecodeState::Succeeded || !m_manager->AppendCookedSceneEnvironment(m_scene, *result.Assets, m_upload))
                {
                    return m_state;
                }
                m_transferred = true;
                m_state       = State::Ready;
                break;
            }
            default:
                return m_state = State::Failed;
        }
        return m_state;
    }

    void CookedSceneEnvironmentBatch::Reset() noexcept
    {
        m_upload.Reset();
        if (m_target.Valid() && !m_transferred)
        {
            m_manager->ReleaseTexture(m_target);
        }
        m_lease.Reset();
        m_manager         = nullptr;
        m_device          = nullptr;
        m_scene           = {};
        m_identity        = {};
        m_target          = {};
        m_transfer_source = false;
        m_transferred     = false;
        m_state           = State::Empty;
    }
} // namespace ZEngine::Rendering::Textures
