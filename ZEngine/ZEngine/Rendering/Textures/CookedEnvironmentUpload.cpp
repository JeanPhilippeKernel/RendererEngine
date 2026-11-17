#include <ZEngine/Rendering/Textures/CookedEnvironmentUpload.h>
#include <utility>

namespace ZEngine::Rendering::Textures
{
    bool CookedEnvironmentUpload::Capture(Managers::CookedCpuClosureCache::Lease&& lease, const Cook::CookAssetIdentity& identity)
    {
        Managers::CookedCpuClosureCache::Result result = {};
        if (m_phase != Phase::Empty || identity.Kind != Cook::CookAssetKind::EnvironmentMap || !identity.IsValid() || !lease.Poll(&result) || result.StateValue != Managers::CookedCpuClosureCache::State::Ready || !result.Assets)
        {
            return false;
        }
        const auto* version = result.Assets->FindVersion(identity);
        const auto* header  = result.Assets->FindEnvironment(identity);
        const auto  object  = result.Assets->EnvironmentObject(identity);
        if (!version || !header || !Importers::AssetCodec::IsEnvironmentMapFileHeaderValid(*header) || header->HeaderByteSize > object.size() || header->BufferByteSize != object.size() - header->HeaderByteSize)
        {
            return false;
        }
        m_header   = header;
        m_pixels   = object.subspan(header->HeaderByteSize);
        m_identity = identity;
        m_digest   = version->ObjectDigest;
        m_lease    = std::move(lease);
        m_phase    = Phase::SourceReady;
        return true;
    }

    bool CookedEnvironmentUpload::Prepare(RenderResourceManager& manager, VkResult* status)
    {
        if (m_phase != Phase::SourceReady)
        {
            if (status)
            {
                *status = VK_ERROR_INITIALIZATION_FAILED;
            }
            return false;
        }
        if (!manager.PrepareUploadStaging(m_pixels.size(), &m_staging, status))
        {
            return false;
        }
        m_manager = &manager;
        m_phase   = Phase::Staging;
        return true;
    }

    bool CookedEnvironmentUpload::StageOne(VkResult* status)
    {
        if (m_phase != Phase::Staging || m_staging.Written() >= m_pixels.size())
        {
            if (status)
            {
                *status = VK_ERROR_INITIALIZATION_FAILED;
            }
            return false;
        }
        size_t consumed = 0;
        if (!m_staging.Append(m_pixels.subspan(static_cast<size_t>(m_staging.Written())), &consumed, status))
        {
            return false;
        }
        if (m_staging.Ready())
        {
            m_phase = Phase::Staged;
        }
        return true;
    }

    bool CookedEnvironmentUpload::Submit(uint8_t frame_index, uint8_t thread_index, TextureHandle target)
    {
        if (m_phase != Phase::Staged || !m_manager || !m_header || !target.Valid())
        {
            return false;
        }
        const auto* texture = m_manager->GetTexture(target);
        if (!texture || texture->Width != m_header->FaceWidth || texture->Height != m_header->FaceHeight || texture->BytePerPixel != sizeof(float) * 4 || texture->Specification.Format != Specifications::ImageFormat::R32G32B32A32_SFLOAT || texture->Specification.Depth != 1 || texture->Specification.LayerCount != m_header->LayerCount || texture->Specification.MipLevelCount != m_header->MipCount || texture->Specification.Is3D || !texture->Specification.IsCubemap || texture->Specification.IsAliasable || !texture->Specification.IsUsageSampled ||
            !texture->Specification.IsUsageTransfert || !texture->Specification.IsUsageStorage)
        {
            return false;
        }
        if (!m_manager->UploadPreparedTextureBuffer(frame_index, thread_index, target, m_staging).Valid())
        {
            return false;
        }
        m_target = target;
        m_phase  = Phase::Submitted;
        return true;
    }

    void CookedEnvironmentUpload::Reset() noexcept
    {
        m_staging.Reset();
        m_lease.Reset();
        m_header   = nullptr;
        m_pixels   = {};
        m_manager  = nullptr;
        m_identity = {};
        m_digest   = {};
        m_target   = {};
        m_phase    = Phase::Empty;
    }
} // namespace ZEngine::Rendering::Textures
