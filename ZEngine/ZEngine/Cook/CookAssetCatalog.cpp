#include <ZEngine/Cook/CookAssetCatalog.h>
#include <ZEngine/Cook/CookLimits.h>
#include <ZEngine/Cook/CookSort.h>
#include <limits>
#include <new>
#include <type_traits>

namespace ZEngine::Cook
{
    namespace
    {
        Core::VFS::VFSResult<void> CatalogFailure(CookDiagnostics* diagnostics, CookDiagnosticCode code, const CookAssetCatalogEntry& entry, cstring message)
        {
            if (auto* diagnostic = diagnostics ? diagnostics->Emplace(CookStage::Capture, code) : nullptr)
            {
                diagnostic->Source = entry.Artifact;
                diagnostic->Asset  = entry.Identity;
                (void) diagnostic->SetMessage(message);
            }
            return Core::VFS::VFSResult<void>::Fail(CookDiagnosticError(code));
        }

        template <typename Less>
        uint32_t FindIndex(const uint32_t* indices, uint32_t count, Less&& less)
        {
            uint32_t begin = 0;
            while (begin < count)
            {
                const uint32_t middle = begin + (count - begin) / 2;
                if (less(indices[middle]))
                    begin = middle + 1;
                else
                    count = middle;
            }
            return begin;
        }
    } // namespace

    CookAssetCatalog::~CookAssetCatalog()
    {
        Reset();
    }

    Core::VFS::VFSResult<void> CookAssetCatalog::Initialize(ICookAllocator& allocator, uint32_t capacity, uint32_t maximum_entries)
    {
        constexpr CookLimits caps            = {};
        constexpr size_t     bytes_per_entry = sizeof(CookAssetCatalogEntry) + 4 * sizeof(uint32_t);
        static_assert(std::is_trivially_destructible_v<CookAssetCatalogEntry>);
        static_assert(caps.MaxCatalogEntries <= (std::numeric_limits<size_t>::max() - (alignof(uint32_t) - 1)) / bytes_per_entry);
        if (m_state != State::Empty || maximum_entries > caps.MaxCatalogEntries)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        if (capacity > maximum_entries)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
        if (capacity)
        {
            const size_t records_bytes    = static_cast<size_t>(capacity) * sizeof(CookAssetCatalogEntry);
            const size_t index_offset     = (records_bytes + alignof(uint32_t) - 1) & ~(alignof(uint32_t) - 1);
            const size_t allocation_bytes = index_offset + static_cast<size_t>(capacity) * 4 * sizeof(uint32_t);
            const auto   allocation       = allocator.Allocate(allocation_bytes, DEFAULT_ALIGNMENT, CookMemoryCategory::CatalogGraph);
            if (allocation.Failed())
                return Core::VFS::VFSResult<void>::Fail(allocation.Error());
            if (!allocation.Value())
                return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::OutOfMemory);
            if (reinterpret_cast<uintptr_t>(allocation.Value()) % DEFAULT_ALIGNMENT)
            {
                allocator.Free(allocation.Value());
                return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
            }
            m_records = static_cast<CookAssetCatalogEntry*>(allocation.Value());
            for (size_t i = 0; i < capacity; ++i)
                new (m_records + i) CookAssetCatalogEntry{};
            m_by_identity = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(allocation.Value()) + index_offset);
            m_by_uuid     = m_by_identity + capacity;
            m_by_artifact = m_by_uuid + capacity;
            m_by_metadata = m_by_artifact + capacity;
        }
        m_allocator = &allocator;
        m_capacity  = capacity;
        m_state     = State::Building;
        return Core::VFS::VFSResult<void>::Ok();
    }

    Core::VFS::VFSResult<void> CookAssetCatalog::Append(const CookAssetCatalogEntry& entry, CookDiagnostics* diagnostics)
    {
        if (m_state != State::Building)
            return CatalogFailure(diagnostics, CookDiagnosticCode::InvalidRequest, entry, "Catalog is not accepting records.");
        if (!IsCookAssetCatalogEntryValid(entry))
            return CatalogFailure(diagnostics, CookDiagnosticCode::InvalidMetadata, entry, "Catalog identity or logical artifact/metadata/source path is invalid.");
        if (m_count == m_capacity)
            return CatalogFailure(diagnostics, CookDiagnosticCode::BudgetExceeded, entry, "Catalog enumeration exceeded its bounded retained capacity.");
        m_records[m_count]     = entry;
        m_by_identity[m_count] = m_by_uuid[m_count] = m_by_artifact[m_count] = m_by_metadata[m_count] = m_count;
        ++m_count;
        return Core::VFS::VFSResult<void>::Ok();
    }

    Core::VFS::VFSResult<void> CookAssetCatalog::Finalize(CookDiagnostics* diagnostics)
    {
        if (m_state != State::Building)
            return CatalogFailure(diagnostics, CookDiagnosticCode::InvalidRequest, {}, "Catalog cannot be finalized in its current state.");
        if (m_count > 1)
        {
            CookMemoryCharge fixed   = {};
            const auto       charged = m_allocator->TryCharge(sizeof(Detail::CookSortState<uint32_t>) + sizeof(fixed), CookMemoryCategory::CatalogGraph, &fixed);
            if (charged.Failed())
                return charged; // Still building: no index or record was modified.
            Detail::CookSortState<uint32_t> sort = {};
            Core::Containers::HeapSort<uint32_t>({m_by_identity, m_count}, sort, [&](uint32_t left, uint32_t right) { return m_records[left].Identity < m_records[right].Identity; });
            Core::Containers::HeapSort<uint32_t>({m_by_uuid, m_count}, sort, [&](uint32_t left, uint32_t right) { return m_records[left].Identity.UUID < m_records[right].Identity.UUID; });
            Core::Containers::HeapSort<uint32_t>({m_by_artifact, m_count}, sort, [&](uint32_t left, uint32_t right) { return m_records[left].Artifact < m_records[right].Artifact; });
            Core::Containers::HeapSort<uint32_t>({m_by_metadata, m_count}, sort, [&](uint32_t left, uint32_t right) { return m_records[left].Metadata < m_records[right].Metadata; });
            for (size_t i = 1; i < m_count; ++i)
            {
                const auto& uuid     = m_records[m_by_uuid[i]];
                const auto& artifact = m_records[m_by_artifact[i]];
                const auto& metadata = m_records[m_by_metadata[i]];
                if (uuid.Identity.UUID == m_records[m_by_uuid[i - 1]].Identity.UUID)
                {
                    m_state = State::Failed;
                    return CatalogFailure(diagnostics, CookDiagnosticCode::IdentityConflict, uuid, "Duplicate asset UUID, including identities with different kinds.");
                }
                if (artifact.Artifact == m_records[m_by_artifact[i - 1]].Artifact || metadata.Metadata == m_records[m_by_metadata[i - 1]].Metadata)
                {
                    m_state = State::Failed;
                    return CatalogFailure(diagnostics, CookDiagnosticCode::IdentityConflict, artifact.Artifact == m_records[m_by_artifact[i - 1]].Artifact ? artifact : metadata, "One logical artifact or sidecar has multiple catalog owners.");
                }
            }
            // A file cannot simultaneously be another record's artifact and
            // sidecar. Merge the two sorted indexes without another allocation.
            uint32_t artifact_index = 0, metadata_index = 0;
            while (artifact_index < m_count && metadata_index < m_count)
            {
                const auto& artifact = m_records[m_by_artifact[artifact_index]];
                const auto& metadata = m_records[m_by_metadata[metadata_index]];
                if (artifact.Artifact == metadata.Metadata)
                {
                    m_state = State::Failed;
                    return CatalogFailure(diagnostics, CookDiagnosticCode::IdentityConflict, artifact, "One input file is both an artifact and a sidecar.");
                }
                if (artifact.Artifact < metadata.Metadata)
                    ++artifact_index;
                else
                    ++metadata_index;
            }
            for (uint32_t i = 0; i < m_count; ++i)
            {
                const auto& entry = m_records[i];
                if (!entry.EnvironmentSource.IsValid())
                    continue;
                const auto artifact = FindIndex(m_by_artifact, m_count, [&](uint32_t record) { return m_records[record].Artifact < entry.EnvironmentSource; });
                const auto metadata = FindIndex(m_by_metadata, m_count, [&](uint32_t record) { return m_records[record].Metadata < entry.EnvironmentSource; });
                if ((artifact < m_count && m_records[m_by_artifact[artifact]].Artifact == entry.EnvironmentSource) || (metadata < m_count && m_records[m_by_metadata[metadata]].Metadata == entry.EnvironmentSource))
                {
                    m_state = State::Failed;
                    return CatalogFailure(diagnostics, CookDiagnosticCode::IdentityConflict, entry, "An environment source aliases a catalog artifact or sidecar.");
                }
            }
        }
        m_state = State::Finalized;
        return Core::VFS::VFSResult<void>::Ok();
    }

    void CookAssetCatalog::Reset() noexcept
    {
        if (m_records)
            m_allocator->Free(m_records);
        m_allocator   = nullptr;
        m_records     = nullptr;
        m_by_identity = m_by_uuid = m_by_artifact = m_by_metadata = nullptr;
        m_count = m_capacity = 0;
        m_state              = State::Empty;
    }

    bool CookAssetCatalog::IsFinalized() const noexcept
    {
        return m_state == State::Finalized;
    }

    const CookAssetCatalogEntry* CookAssetCatalog::Find(const CookAssetIdentity& identity) const noexcept
    {
        if (!IsFinalized() || !identity.IsValid())
            return nullptr;
        const auto index = FindIndex(m_by_identity, m_count, [&](uint32_t record) { return m_records[record].Identity < identity; });
        return index < m_count && m_records[m_by_identity[index]].Identity == identity ? m_records + m_by_identity[index] : nullptr;
    }

    const CookAssetCatalogEntry* CookAssetCatalog::FindArtifact(const Core::VFS::VFSRelativePath& path) const noexcept
    {
        if (!IsFinalized() || !IsCookLogicalPath(path))
            return nullptr;
        const auto index = FindIndex(m_by_artifact, m_count, [&](uint32_t record) { return m_records[record].Artifact < path; });
        return index < m_count && m_records[m_by_artifact[index]].Artifact == path ? m_records + m_by_artifact[index] : nullptr;
    }

    Core::VFS::VFSResult<void> CookAssetCatalog::Enumerate(CookCatalogVisitorFn visitor, void* context) const
    {
        if (!IsFinalized() || !visitor)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        for (size_t i = 0; i < m_count; ++i)
        {
            const auto visited = visitor(context, m_records[m_by_identity[i]]);
            if (visited.Failed())
                return visited;
        }
        return Core::VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Cook
