#include <ZEngine/Rendering/Meshes/CookedMeshStaging.h>
#include <utility>

namespace ZEngine::Rendering::Meshes
{
    bool CookedMeshStaging::Capture(Managers::CookedCpuClosureCache::Lease&& lease, const Cook::CookAssetIdentity& identity)
    {
        Managers::CookedCpuClosureCache::Result result = {};
        if (m_phase != Phase::Empty || identity.Kind != Cook::CookAssetKind::Mesh || !identity.IsValid() || !lease.Poll(&result) || result.StateValue != Managers::CookedCpuClosureCache::State::Ready || !result.Assets)
        {
            return false;
        }
        const auto*      version           = result.Assets->FindVersion(identity);
        const auto*      mesh              = result.Assets->FindMesh(identity);
        constexpr size_t floats_per_vertex = 8;
        if (!version || !mesh || mesh->MeshUUID != identity.UUID || mesh->Vertices.empty() || mesh->Indices.empty() || mesh->Vertices.size() % floats_per_vertex != 0 || mesh->Vertices.size() / floats_per_vertex > UINT32_MAX || mesh->Indices.size() > UINT32_MAX || mesh->Vertices.size() > SIZE_MAX / sizeof(float) || mesh->Indices.size() > SIZE_MAX / sizeof(uint32_t))
        {
            return false;
        }
        const size_t vertex_bytes = mesh->Vertices.size() * sizeof(float);
        const size_t index_bytes  = mesh->Indices.size() * sizeof(uint32_t);
        if (vertex_bytes > SIZE_MAX - index_bytes)
        {
            return false;
        }
        m_identity     = identity;
        m_digest       = version->ObjectDigest;
        m_mesh         = mesh;
        m_vertex_bytes = vertex_bytes;
        m_index_bytes  = index_bytes;
        m_lease        = std::move(lease);
        m_phase        = Phase::SourceReady;
        return true;
    }

    bool CookedMeshStaging::Prepare(RenderResourceManager& manager, VkResult* status)
    {
        if (m_phase != Phase::SourceReady)
        {
            if (status)
            {
                *status = VK_ERROR_INITIALIZATION_FAILED;
            }
            return false;
        }
        if (!manager.PrepareUploadStaging(m_vertex_bytes + m_index_bytes, &m_staging, status))
        {
            return false;
        }
        m_phase = Phase::Staging;
        return true;
    }

    bool CookedMeshStaging::StageOne(VkResult* status)
    {
        if (m_phase != Phase::Staging || !m_mesh)
        {
            if (status)
            {
                *status = VK_ERROR_INITIALIZATION_FAILED;
            }
            return false;
        }
        const size_t offset        = static_cast<size_t>(m_staging.Written());
        const bool   vertices      = offset < m_vertex_bytes;
        const size_t source_offset = vertices ? offset : offset - m_vertex_bytes;
        const size_t source_size   = vertices ? m_vertex_bytes : m_index_bytes;
        if (source_offset >= source_size)
        {
            if (status)
            {
                *status = VK_ERROR_INITIALIZATION_FAILED;
            }
            return false;
        }
        const auto* data     = vertices ? reinterpret_cast<const uint8_t*>(m_mesh->Vertices.data()) : reinterpret_cast<const uint8_t*>(m_mesh->Indices.data());
        size_t      consumed = 0;
        if (!m_staging.Append({data + source_offset, source_size - source_offset}, &consumed, status))
        {
            return false;
        }
        if (m_staging.Ready())
        {
            m_phase = Phase::Staged;
        }
        return true;
    }

    void CookedMeshStaging::Reset() noexcept
    {
        m_staging.Reset();
        m_lease.Reset();
        m_mesh         = nullptr;
        m_identity     = {};
        m_digest       = {};
        m_vertex_bytes = 0;
        m_index_bytes  = 0;
        m_phase        = Phase::Empty;
    }
} // namespace ZEngine::Rendering::Meshes
