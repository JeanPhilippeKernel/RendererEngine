#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Textures/CookedSceneTextureBatch.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <new>
#include <utility>

namespace ZEngine::Rendering::Textures
{
    struct CookedSceneTextureBatch::Entry
    {
        Cook::CookAssetIdentity Identity    = {};
        CookedTextureUpload     Upload      = {};
        TextureHandle           Target      = {};
        bool                    Transferred = false;
    };

    CookedSceneTextureBatch::~CookedSceneTextureBatch()
    {
        Reset();
    }

    bool CookedSceneTextureBatch::Initialize(Managers::CookedCpuClosureCache::Lease&& lease, Core::Memory::IAllocationRoute& route, RenderResourceManager& manager, Hardwares::VulkanDevice& device, RenderResourceManager::SceneResourcePreparationId scene, uint32_t max_textures)
    {
        Managers::CookedCpuClosureCache::Result               result      = {};
        RenderResourceManager::SceneResourcePreparationResult preparation = {};
        if (m_state != State::Empty || !scene || device.RRM != &manager || !lease.Poll(&result) || result.StateValue != Managers::CookedCpuClosureCache::State::Ready || !result.Assets || max_textures > RenderResourceManager::SceneCookedTextureCapacity || !manager.PollSceneResourcePreparation(scene, &preparation) || !preparation.Cooked || preparation.CookedManifest != result.Assets->ManifestDigest() || preparation.CookedScene != result.Assets->SceneUUID() || preparation.State != RenderResourceManager::SceneResourcePreparationState::Preparing)
        {
            return false;
        }
        uint32_t count = 0;
        for (size_t i = 0; i < result.Assets->Size(); ++i)
        {
            const auto* version = result.Assets->VersionAt(i);
            if (!version)
            {
                return false;
            }
            if (version->Identity.Kind == Cook::CookAssetKind::Texture && ++count > max_textures)
            {
                return false;
            }
        }
        static_assert(alignof(Entry) <= alignof(std::max_align_t));
        if (count && size_t(count) > std::numeric_limits<size_t>::max() / sizeof(Entry))
        {
            return false;
        }
        auto* entries = count ? static_cast<Entry*>(route.Allocate(size_t(count) * sizeof(Entry))) : nullptr;
        if (count && !entries)
        {
            return false;
        }
        uint32_t index = 0;
        for (size_t i = 0; i < result.Assets->Size(); ++i)
        {
            const auto* version = result.Assets->VersionAt(i);
            if (version->Identity.Kind == Cook::CookAssetKind::Texture)
            {
                new (entries + index) Entry{};
                entries[index++].Identity = version->Identity;
            }
        }
        m_route   = &route;
        m_manager = &manager;
        m_device  = &device;
        m_scene   = scene;
        m_entries = entries;
        m_count   = count;
        m_lease   = std::move(lease);
        m_state   = count ? State::Pending : State::Ready;
        return true;
    }

    CookedSceneTextureBatch::State CookedSceneTextureBatch::Step(uint8_t frame_index, uint8_t thread_index)
    {
        if (m_state != State::Pending)
        {
            return m_state;
        }
        const auto fail = [this](const char* reason) {
            ZENGINE_CORE_ERROR("[CookedTextureBatch] {} submit={} transfer={} count={}", reason, m_submit_cursor, m_transfer_cursor, m_count)
            return m_state = State::Failed;
        };
        Managers::CookedCpuClosureCache::Result               result = {};
        RenderResourceManager::SceneResourcePreparationResult scene  = {};
        if (!m_lease.Poll(&result) || result.StateValue != Managers::CookedCpuClosureCache::State::Ready || !result.Assets || m_device->IsDeviceLost.load(std::memory_order_acquire) || !m_manager->PollSceneResourcePreparation(m_scene, &scene) || !scene.Cooked || scene.State != RenderResourceManager::SceneResourcePreparationState::Preparing)
        {
            return fail("closure, device, or scene is unavailable");
        }
        if (m_submit_cursor < m_count)
        {
            auto& entry = m_entries[m_submit_cursor];
            switch (entry.Upload.State())
            {
                case CookedTextureUpload::Phase::Empty:
                {
                    const auto*                            image    = result.Assets->FindTexture(entry.Identity);
                    Managers::CookedCpuClosureCache::Lease retained = {};
                    if (!image || !m_lease.Retain(&retained) || !entry.Upload.Capture(std::move(retained), entry.Identity))
                        return fail("decoded image or closure lease unavailable");
                    Specifications::TextureSpecification spec = {};
                    spec.Width                                = image->Description().Width;
                    spec.Height                               = image->Description().Height;
                    spec.BytePerPixel                         = 4;
                    spec.Format                               = Specifications::ImageFormat::R8G8B8A8_SRGB;
                    spec.IsCookedAsset                        = true;
                    VkResult status                           = VK_SUCCESS;
                    entry.Target                              = m_device->TryCreateTexture(spec, "cooked-scene-texture", 0, &status);
                    if (!entry.Target.Valid())
                    {
                        ZENGINE_CORE_ERROR("[CookedTextureBatch] texture allocation status={} live_textures={} live_images={} texture_limit={}", static_cast<int32_t>(status), m_device->GlobalTextures.Size(), m_device->ImageBufferManager.Size(), m_device->MaxGlobalTexture)
                        return fail("texture allocation failed");
                    }
                    if (!entry.Upload.Prepare(*m_manager, &status))
                    {
                        ZENGINE_CORE_ERROR("[CookedTextureBatch] staging allocation status={}", static_cast<int32_t>(status))
                        return fail("staging preparation failed");
                    }
                    break;
                }
                case CookedTextureUpload::Phase::Staging:
                {
                    // Each write stays bounded by UploadStaging::MaxSliceBytes.
                    // Use a small time and byte budget per render-owner visit
                    // so large textures do not require one frame per 64 KiB.
                    const auto         start             = std::chrono::steady_clock::now();
                    const auto         deadline          = start + std::chrono::milliseconds(1);
                    constexpr uint32_t MaxWritesPerVisit = 16;
                    for (uint32_t writes = 0; writes < MaxWritesPerVisit && entry.Upload.State() == CookedTextureUpload::Phase::Staging; ++writes)
                    {
                        if (!entry.Upload.StageOne())
                            return fail("staging write failed");
                        ++m_staging_writes;
                        if (std::chrono::steady_clock::now() >= deadline)
                            break;
                    }
                    ++m_staging_visits;
                    const auto elapsed = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
                    m_staging_max_ns   = std::max(m_staging_max_ns, elapsed);
                    break;
                }
                case CookedTextureUpload::Phase::Staged:
                    if (entry.Upload.Submit(frame_index, thread_index, entry.Target))
                        ++m_submit_cursor;
                    break;
                default:
                    return fail("unexpected upload phase");
            }
            return m_state;
        }
        auto&      entry = m_entries[m_transfer_cursor];
        const auto ready = entry.Upload.Poll();
        if (ready == CookedTextureUpload::Readiness::Failed)
            return fail("submitted texture readiness failed");
        if (ready != CookedTextureUpload::Readiness::Ready)
            return m_state;
        if (!m_manager->AppendCookedSceneTexture(m_scene, *result.Assets, entry.Upload))
            return fail("texture transfer to scene failed");
        entry.Transferred = true;
        if (++m_transfer_cursor == m_count)
        {
            ZENGINE_CORE_INFO("[CookedTextureTiming] textures={} staging_visits={} staging_writes={} max_visit_ns={}", m_count, m_staging_visits, m_staging_writes, m_staging_max_ns)
            m_state = State::Ready;
        }
        return m_state;
    }

    CookedTextureUpload* CookedSceneTextureBatch::Find(const Cook::CookAssetIdentity& identity) noexcept
    {
        if (identity.Kind != Cook::CookAssetKind::Texture || !m_count)
        {
            return nullptr;
        }
        auto* found = std::lower_bound(m_entries, m_entries + m_count, identity, [](const Entry& entry, const Cook::CookAssetIdentity& key) { return entry.Identity < key; });
        return found != m_entries + m_count && found->Identity == identity ? &found->Upload : nullptr;
    }

    CookedSceneTextureBatch::State CookedSceneTextureBatch::StepMaterials()
    {
        if (m_material_state != State::Pending)
        {
            return m_material_state;
        }
        const auto fail = [this](const char* reason) {
            ZENGINE_CORE_ERROR("[CookedMaterialBatch] {} cursor={}", reason, m_material_cursor)
            return m_material_state = State::Failed;
        };
        if (m_state == State::Failed || m_state == State::Empty)
        {
            return fail("texture batch unavailable");
        }
        if (m_state != State::Ready)
        {
            return State::Pending;
        }
        Managers::CookedCpuClosureCache::Result               result = {};
        RenderResourceManager::SceneResourcePreparationResult scene  = {};
        if (!m_lease.Poll(&result) || result.StateValue != Managers::CookedCpuClosureCache::State::Ready || !result.Assets || !m_manager->PollSceneResourcePreparation(m_scene, &scene) || !scene.Cooked || scene.State != RenderResourceManager::SceneResourcePreparationState::Preparing)
        {
            return fail("closure or scene unavailable");
        }
        if (m_material_cursor == result.Assets->Size())
        {
            return m_material_state = State::Ready;
        }
        const auto* version = result.Assets->VersionAt(m_material_cursor);
        if (!version)
        {
            return fail("asset version unavailable");
        }
        if (version->Identity.Kind == Cook::CookAssetKind::Material)
        {
            const auto* material = result.Assets->FindMaterial(version->Identity);
            if (!material)
            {
                return fail("material unavailable");
            }
            std::array<CookedTextureUpload*, 5> uploads = {};
            size_t                              count   = 0;
            for (const auto& map : material->Textures)
            {
                if (map.UUID.is_nil())
                {
                    continue;
                }
                auto* upload = Find({Cook::CookAssetKind::Texture, map.UUID});
                if (!upload)
                {
                    return fail("material texture unavailable");
                }
                if (std::find(uploads.begin(), uploads.begin() + count, upload) == uploads.begin() + count)
                {
                    uploads[count++] = upload;
                }
            }
            std::sort(uploads.begin(), uploads.begin() + count, [](const auto* left, const auto* right) { return left->Identity() < right->Identity(); });
            Materials::CookedMaterialVersion prepared = {};
            const auto                       state    = Materials::PrepareCookedMaterial(*result.Assets, version->Identity, {uploads.data(), count}, *m_manager, &prepared);
            if (state == SceneTextureDescriptorState::Pending)
            {
                return State::Pending;
            }
            if (state != SceneTextureDescriptorState::Ready || !m_manager->AppendCookedSceneMaterial(m_scene, *result.Assets, prepared))
            {
                return fail("material preparation or scene transfer failed");
            }
        }
        ++m_material_cursor;
        if (m_material_cursor == result.Assets->Size())
        {
            m_material_state = State::Ready;
        }
        return m_material_state;
    }

    void CookedSceneTextureBatch::Reset() noexcept
    {
        for (uint32_t i = 0; i < m_count; ++i)
        {
            auto& entry = m_entries[i];
            entry.Upload.Reset();
            if (entry.Target.Valid() && !entry.Transferred)
                m_manager->ReleaseTexture(entry.Target);
            entry.~Entry();
        }
        if (m_entries)
            m_route->Free(m_entries);
        m_lease.Reset();
        m_route   = nullptr;
        m_manager = nullptr;
        m_device  = nullptr;
        m_scene   = {};
        m_entries = nullptr;
        m_count = m_submit_cursor = m_transfer_cursor = 0;
        m_staging_visits = m_staging_writes = m_staging_max_ns = 0;
        m_material_cursor                                      = 0;
        m_material_state                                       = State::Pending;
        m_state                                                = State::Empty;
    }
} // namespace ZEngine::Rendering::Textures
