#pragma once
#include <ZEngine/Managers/CookedCpuClosureCache.h>
#include <ZEngine/Rendering/RenderResourceManager.h>

namespace ZEngine::Rendering::Textures
{
    /// Exclusive staging transaction for the verified base level of a cooked
    /// HDR cubemap. Cache, manager and device must outlive this object. The
    /// caller owns the destination and must retain it through upload and bake.
    class CookedEnvironmentUpload final
    {
    public:
        enum class Phase : uint8_t
        {
            Empty,
            SourceReady,
            Staging,
            Staged,
            Submitted
        };
        CookedEnvironmentUpload()                                          = default;
        CookedEnvironmentUpload(const CookedEnvironmentUpload&)            = delete;
        CookedEnvironmentUpload& operator=(const CookedEnvironmentUpload&) = delete;

        [[nodiscard]] bool       Capture(Managers::CookedCpuClosureCache::Lease&&, const Cook::CookAssetIdentity&);
        [[nodiscard]] bool       Prepare(RenderResourceManager&, VkResult* status = nullptr);
        [[nodiscard]] bool       StageOne(VkResult* status = nullptr);
        /// Submission is not proof of GPU completion or consumer acquisition.
        [[nodiscard]] bool       Submit(uint8_t frame_index, uint8_t thread_index, TextureHandle target);
        void                     Reset() noexcept;

        [[nodiscard]] Phase      State() const noexcept
        {
            return m_phase;
        }
        [[nodiscard]] const Cook::CookAssetIdentity& Identity() const noexcept
        {
            return m_identity;
        }
        [[nodiscard]] const Cook::Digest256& Digest() const noexcept
        {
            return m_digest;
        }
        [[nodiscard]] const Importers::AssetCodec::EnvironmentMapFileHeader* Header() const noexcept
        {
            return m_header;
        }
        [[nodiscard]] VkDeviceSize StagedBytes() const noexcept
        {
            return m_staging.Written();
        }
        [[nodiscard]] TextureHandle SubmittedTexture() const noexcept
        {
            return m_phase == Phase::Submitted ? m_target : TextureHandle{};
        }
        [[nodiscard]] bool BelongsTo(const RenderResourceManager& manager) const noexcept
        {
            return m_manager == &manager;
        }

    private:
        Managers::CookedCpuClosureCache::Lease                 m_lease    = {};
        const Importers::AssetCodec::EnvironmentMapFileHeader* m_header   = nullptr;
        std::span<const uint8_t>                               m_pixels   = {};
        RenderResourceManager*                                 m_manager  = nullptr;
        RenderResourceManager::UploadStaging                   m_staging  = {};
        Cook::CookAssetIdentity                                m_identity = {};
        Cook::Digest256                                        m_digest   = {};
        TextureHandle                                          m_target   = {};
        Phase                                                  m_phase    = Phase::Empty;
    };
} // namespace ZEngine::Rendering::Textures
