#pragma once
#include <ZEngine/Managers/CookedCpuClosureCache.h>
#include <ZEngine/Rendering/Materials/CookedMaterialBinding.h>
#include <ZEngine/Rendering/RenderResourceManager.h>

namespace ZEngine::Rendering::Textures
{
    /// One unpublished cooked texture's source and staging transaction. The
    /// closure lease keeps decoded pixels alive through accepted staging submission;
    /// identity and digest are copied for the remaining GPU lifetime. Each
    /// instance is exclusively owned; externally synchronize thread handoffs.
    /// The cache, resource manager and device outlive the transaction.
    class CookedTextureUpload final
    {
    public:
        enum class Phase : uint8_t
        {
            Empty,
            SourceReady,
            Staging,
            Staged,
            Submitted,
            Failed
        };
        enum class Readiness : uint8_t
        {
            Pending,
            AwaitingConsumer,
            Ready,
            Failed
        };

        CookedTextureUpload()                                                                        = default;
        ~CookedTextureUpload()                                                                       = default;
        CookedTextureUpload(const CookedTextureUpload&)                                              = delete;
        CookedTextureUpload&                                   operator=(const CookedTextureUpload&) = delete;

        /// Consumes a ready closure lease only on success. Source assets and
        /// registries are never consulted; the selected version is immutable.
        [[nodiscard]] bool                                     Capture(Managers::CookedCpuClosureCache::Lease&& lease, const Cook::CookAssetIdentity& identity);
        /// Render-owner allocation. Failure preserves the source for retry.
        [[nodiscard]] bool                                     Prepare(RenderResourceManager& manager, VkResult* status = nullptr);
        /// Copies one bounded slice. A failed write does not advance the cursor.
        [[nodiscard]] bool                                     StageOne(VkResult* status = nullptr);
        /// Render-owner submission into a caller-owned, compatible image reserved
        /// exclusively for this version until it is pinned or retired. A
        /// refusal preserves the staged data. The returned binding represents
        /// submission only: it does not establish GPU, descriptor, or consumer
        /// readiness, nor does it retain the target image.
        [[nodiscard]] bool                                     Submit(uint8_t frame_index, uint8_t thread_index, TextureHandle target);
        /// Render-owner poll. Ready additionally requires completion of an
        /// accepted graphics-family acquire. It is texture readiness, not a
        /// claim that the rest of the scene is ready for publication.
        [[nodiscard]] Readiness                                Poll();
        /// A clone of the retained version, only while its descriptor is current.
        /// Its caller must retain it through every CPU and GPU consumer.
        [[nodiscard]] RenderResourceManager::TextureVersionPin RetainPreparedVersion();
        void                                                   Reset() noexcept;

        [[nodiscard]] Phase                                    State() const noexcept
        {
            return m_phase;
        }
        [[nodiscard]] VkDeviceSize StagedBytes() const noexcept
        {
            return m_staging.Written();
        }
        [[nodiscard]] const Materials::CookedTextureBinding* SubmittedBinding() const noexcept
        {
            return m_phase == Phase::Submitted ? &m_binding : nullptr;
        }
        [[nodiscard]] const Materials::CookedTextureBinding* ProducerDescriptorBinding();
        [[nodiscard]] const Materials::CookedTextureBinding* ReadyBinding();
        [[nodiscard]] const Cook::CookAssetIdentity&         Identity() const noexcept
        {
            return m_identity;
        }
        [[nodiscard]] const Cook::Digest256& Digest() const noexcept
        {
            return m_digest;
        }
        [[nodiscard]] bool BelongsTo(const RenderResourceManager& manager) const noexcept
        {
            return m_manager == &manager;
        }

    private:
        Managers::CookedCpuClosureCache::Lease     m_lease                     = {};
        const Importers::ImageCodec::DecodedImage* m_image                     = nullptr;
        RenderResourceManager*                     m_manager                   = nullptr;
        RenderResourceManager::UploadStaging       m_staging                   = {};
        RenderResourceManager::TextureVersionPin   m_texture_pin               = {};
        Cook::CookAssetIdentity                    m_identity                  = {};
        Cook::Digest256                            m_digest                    = {};
        Materials::CookedTextureBinding            m_binding                   = {};
        uint64_t                                   m_descriptor_publication    = 0;
        bool                                       m_producer_descriptor_ready = false;
        bool                                       m_consumer_ready            = false;
        Phase                                      m_phase                     = Phase::Empty;
    };
} // namespace ZEngine::Rendering::Textures
