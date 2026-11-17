#include <ZEngine/Rendering/Meshes/CookedSceneMeshBatch.h>
#include <ZEngine/ZEngineDef.h>
#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <utility>

namespace ZEngine::Rendering::Meshes
{
    CookedSceneMeshBatch::~CookedSceneMeshBatch()
    {
        const bool reset = Reset();
        ZENGINE_VALIDATE_ASSERT(reset, "Cooked mesh batch destroyed before accepted uploads drained")
        (void) reset;
    }

    bool CookedSceneMeshBatch::Initialize(Managers::CookedCpuClosureCache::Lease&& lease, std::span<const uuids::uuid> meshes, Core::Memory::IAllocationRoute& route, RenderResourceManager& manager, Core::Memory::TLSFSlab& metadata, RenderResourceManager::SceneResourcePreparationId scene)
    {
        Managers::CookedCpuClosureCache::Result               result      = {};
        RenderResourceManager::SceneResourcePreparationResult preparation = {};
        if (m_state != State::Empty || !scene || meshes.size() > RenderResourceManager::SceneGeometryMeshCapacity || !lease.Poll(&result) || result.StateValue != Managers::CookedCpuClosureCache::State::Ready || !result.Assets || !manager.PollSceneResourcePreparation(scene, &preparation) || !preparation.Cooked || preparation.CookedManifest != result.Assets->ManifestDigest() || preparation.CookedScene != result.Assets->SceneUUID() || preparation.State != RenderResourceManager::SceneResourcePreparationState::Preparing)
        {
            return false;
        }
        for (size_t i = 0; i < meshes.size(); ++i)
        {
            const Cook::CookAssetIdentity identity = {Cook::CookAssetKind::Mesh, meshes[i]};
            if (!identity.IsValid() || (i && !(meshes[i - 1] < meshes[i])) || !result.Assets->FindMesh(identity))
            {
                return false;
            }
        }
        static_assert(alignof(uuids::uuid) <= alignof(std::max_align_t));
        if (meshes.size() > std::numeric_limits<size_t>::max() / sizeof(uuids::uuid))
            return false;
        auto* copy = meshes.empty() ? nullptr : static_cast<uuids::uuid*>(route.Allocate(meshes.size() * sizeof(uuids::uuid)));
        if (!meshes.empty() && !copy)
            return false;
        if (copy)
        {
            std::uninitialized_copy(meshes.begin(), meshes.end(), copy);
        }
        m_lease    = std::move(lease);
        m_route    = &route;
        m_manager  = &manager;
        m_metadata = &metadata;
        m_scene    = scene;
        m_meshes   = copy;
        m_count    = static_cast<uint32_t>(meshes.size());
        m_state    = m_count ? State::Pending : State::Ready;
        return true;
    }

    void CookedSceneMeshBatch::Fail() noexcept
    {
        m_after_drain = State::Failed;
        if (m_upload.Serial)
        {
            (void) m_manager->CancelCookedMeshUpload(m_upload);
            m_state = State::Draining;
        }
        else
        {
            m_state = State::Failed;
        }
    }

    void CookedSceneMeshBatch::Cancel() noexcept
    {
        if (m_state != State::Pending)
            return;
        m_after_drain = State::Cancelled;
        if (m_upload.Serial)
        {
            (void) m_manager->CancelCookedMeshUpload(m_upload);
            m_state = State::Draining;
        }
        else
        {
            m_state = State::Cancelled;
        }
    }

    CookedSceneMeshBatch::State CookedSceneMeshBatch::Step(uint8_t frame_index)
    {
        if (m_state == State::Draining)
        {
            const auto result = m_manager->PollCookedMeshUpload(m_upload);
            if (!result.CleanupPending && m_manager->ReleaseCookedMeshUpload(m_upload))
            {
                m_upload = {};
                m_state  = m_after_drain;
            }
            return m_state;
        }
        if (m_state != State::Pending)
            return m_state;
        Managers::CookedCpuClosureCache::Result               assets = {};
        RenderResourceManager::SceneResourcePreparationResult scene  = {};
        if (!m_lease.Poll(&assets) || assets.StateValue != Managers::CookedCpuClosureCache::State::Ready || !assets.Assets || !m_manager->PollSceneResourcePreparation(m_scene, &scene) || !scene.Cooked || scene.State != RenderResourceManager::SceneResourcePreparationState::Preparing)
        {
            Fail();
            return m_state;
        }
        if (m_upload.Serial)
        {
            const auto result = m_manager->PollCookedMeshUpload(m_upload);
            if (result.State == RenderResourceManager::CookedMeshUploadState::Pending)
                return m_state;
            if (result.State != RenderResourceManager::CookedMeshUploadState::Ready)
            {
                Fail();
                return m_state;
            }
            RenderResourceManager::CookedMeshVersion version = {};
            if (!m_manager->TakeCookedMeshUpload(m_upload, &version))
            {
                Fail();
                return m_state;
            }
            m_upload = {};
            if (!m_manager->AppendCookedSceneMesh(m_scene, *assets.Assets, version))
            {
                Fail();
                return m_state;
            }
            if (++m_cursor == m_count)
                m_state = State::Ready;
            return m_state;
        }
        switch (m_staging.State())
        {
            case CookedMeshStaging::Phase::Empty:
            {
                Managers::CookedCpuClosureCache::Lease retained = {};
                const Cook::CookAssetIdentity          identity = {Cook::CookAssetKind::Mesh, m_meshes[m_cursor]};
                if (!m_lease.Retain(&retained) || !m_staging.Capture(std::move(retained), identity) || !m_staging.Prepare(*m_manager))
                    Fail();
                break;
            }
            case CookedMeshStaging::Phase::Staging:
                if (!m_staging.StageOne())
                    Fail();
                break;
            case CookedMeshStaging::Phase::Staged:
            {
                const auto admission = m_manager->RecordCookedMeshUpload(frame_index, m_staging, *m_metadata, &m_upload);
                if (admission == RenderResourceManager::CookedMeshAdmission::Failed)
                    Fail();
                break;
            }
            default:
                Fail();
                break;
        }
        return m_state;
    }

    bool CookedSceneMeshBatch::AbandonAfterDeviceIdle() noexcept
    {
        if (m_upload.Serial && !m_manager->AbandonCookedMeshUploadAfterDeviceIdle(m_upload))
        {
            return false;
        }
        m_upload = {};
        if (m_state == State::Draining)
        {
            m_state = m_after_drain;
        }
        return true;
    }

    bool CookedSceneMeshBatch::Reset() noexcept
    {
        if (m_upload.Serial)
            return false;
        m_staging.Reset();
        if (m_meshes)
        {
            std::destroy_n(m_meshes, m_count);
            m_route->Free(m_meshes);
        }
        m_lease.Reset();
        m_route    = nullptr;
        m_manager  = nullptr;
        m_metadata = nullptr;
        m_scene    = {};
        m_meshes   = nullptr;
        m_count = m_cursor = 0;
        m_state            = State::Empty;
        return true;
    }
} // namespace ZEngine::Rendering::Meshes
