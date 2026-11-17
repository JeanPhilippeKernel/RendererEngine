#include <ZEngine/Rendering/Textures/CookedTextureUpload.h>
#include <limits>
#include <utility>

namespace ZEngine::Rendering::Textures
{
    bool CookedTextureUpload::Capture(Managers::CookedCpuClosureCache::Lease&& lease, const Cook::CookAssetIdentity& identity)
    {
        Managers::CookedCpuClosureCache::Result result = {};
        if (m_phase != Phase::Empty || identity.Kind != Cook::CookAssetKind::Texture || !identity.IsValid() || !lease.Poll(&result) || result.StateValue != Managers::CookedCpuClosureCache::State::Ready || !result.Assets)
        {
            return false;
        }
        const auto* version = result.Assets->FindVersion(identity);
        const auto* image   = result.Assets->FindTexture(identity);
        if (!version || !image)
        {
            return false;
        }
        const auto&  description = image->Description();
        const size_t width       = description.Width;
        const size_t height      = description.Height;
        if (!width || !height || width > std::numeric_limits<size_t>::max() / height || width * height > std::numeric_limits<size_t>::max() / 4 || image->Pixels().size() != width * height * 4)
        {
            return false;
        }
        m_identity = identity;
        m_digest   = version->ObjectDigest;
        m_image    = image;
        m_lease    = std::move(lease);
        m_phase    = Phase::SourceReady;
        return true;
    }

    bool CookedTextureUpload::Prepare(RenderResourceManager& manager, VkResult* status)
    {
        if (m_phase != Phase::SourceReady)
        {
            if (status)
            {
                *status = VK_ERROR_INITIALIZATION_FAILED;
            }
            return false;
        }
        if (!manager.PrepareUploadStaging(m_image->Pixels().size(), &m_staging, status))
        {
            return false;
        }
        m_manager = &manager;
        m_phase   = Phase::Staging;
        return true;
    }

    bool CookedTextureUpload::StageOne(VkResult* status)
    {
        if (m_phase != Phase::Staging || !m_image)
        {
            if (status)
            {
                *status = VK_ERROR_INITIALIZATION_FAILED;
            }
            return false;
        }
        const auto   pixels = m_image->Pixels();
        const size_t offset = static_cast<size_t>(m_staging.Written());
        if (offset >= pixels.size())
        {
            return false;
        }
        size_t consumed = 0;
        if (!m_staging.Append({pixels.data() + offset, pixels.size() - offset}, &consumed, status))
        {
            return false;
        }
        if (m_staging.Ready())
        {
            m_phase = Phase::Staged;
        }
        return true;
    }

    bool CookedTextureUpload::Submit(uint8_t frame_index, uint8_t thread_index, TextureHandle target)
    {
        if (m_phase != Phase::Staged || !m_manager || !m_image || !target.Valid() || target.Index >= INVALID_MAP_HANDLE)
        {
            return false;
        }
        const auto* texture     = m_manager->GetTexture(target);
        const auto& description = m_image->Description();
        if (!texture || texture->Width != description.Width || texture->Height != description.Height || texture->BytePerPixel != 4 || texture->Specification.Format != Specifications::ImageFormat::R8G8B8A8_SRGB || texture->Specification.Depth != 1 || texture->Specification.LayerCount != 1 || texture->Specification.MipLevelCount != 1 || texture->Specification.Is3D || texture->Specification.IsCubemap || texture->Specification.IsAliasable || !texture->Specification.IsUsageSampled || !texture->Specification.IsUsageTransfert)
        {
            return false;
        }
        if (!m_manager->UploadPreparedTextureBuffer(frame_index, thread_index, target, m_staging).Valid())
        {
            return false;
        }
        m_binding = {m_identity, m_digest, target};
        // UploadPreparedTextureBuffer transfers the fully written staging buffer
        // to the upload owner. No later phase reads decoded pixels, so retaining
        // one closure lease per submitted texture would unnecessarily exhaust the
        // bounded per-closure lease budget before large batches can finish.
        m_image   = nullptr;
        m_lease.Reset();
        m_phase = Phase::Submitted;
        return true;
    }

    CookedTextureUpload::Readiness CookedTextureUpload::Poll()
    {
        m_producer_descriptor_ready = false;
        m_consumer_ready            = false;
        if (m_phase == Phase::Empty || m_phase == Phase::Failed)
        {
            return Readiness::Failed;
        }
        if (m_phase != Phase::Submitted)
        {
            return Readiness::Pending;
        }
        const auto state = m_manager->PrepareSubmittedTextureVersion(m_binding.Handle, &m_texture_pin, &m_descriptor_publication);
        if (state == SceneTextureDescriptorState::Failed)
        {
            m_phase = Phase::Failed;
            return Readiness::Failed;
        }
        if (state == SceneTextureDescriptorState::Pending)
        {
            return Readiness::Pending;
        }
        m_producer_descriptor_ready = true;
        const auto acquire          = m_manager->PollSubmittedTextureAcquire(m_texture_pin);
        if (acquire == SceneTextureDescriptorState::Failed)
        {
            m_phase                     = Phase::Failed;
            m_producer_descriptor_ready = false;
            return Readiness::Failed;
        }
        if (acquire == SceneTextureDescriptorState::Pending)
        {
            return Readiness::AwaitingConsumer;
        }
        m_consumer_ready = true;
        return Readiness::Ready;
    }

    const Materials::CookedTextureBinding* CookedTextureUpload::ProducerDescriptorBinding()
    {
        if (m_phase != Phase::Submitted || !m_producer_descriptor_ready || !m_manager->IsSubmittedTextureDescriptorCurrent(m_texture_pin, m_descriptor_publication))
        {
            return nullptr;
        }
        return &m_binding;
    }

    RenderResourceManager::TextureVersionPin CookedTextureUpload::RetainPreparedVersion()
    {
        return ProducerDescriptorBinding() ? m_manager->RetainTextureVersion(m_texture_pin) : RenderResourceManager::TextureVersionPin{};
    }

    const Materials::CookedTextureBinding* CookedTextureUpload::ReadyBinding()
    {
        return m_consumer_ready && ProducerDescriptorBinding() && m_manager->PollSubmittedTextureAcquire(m_texture_pin) == SceneTextureDescriptorState::Ready ? &m_binding : nullptr;
    }

    void CookedTextureUpload::Reset() noexcept
    {
        m_texture_pin.Reset();
        m_staging.Reset();
        m_lease.Reset();
        m_image                     = nullptr;
        m_manager                   = nullptr;
        m_identity                  = {};
        m_digest                    = {};
        m_binding                   = {};
        m_descriptor_publication    = 0;
        m_producer_descriptor_ready = false;
        m_consumer_ready            = false;
        m_phase                     = Phase::Empty;
    }
} // namespace ZEngine::Rendering::Textures
