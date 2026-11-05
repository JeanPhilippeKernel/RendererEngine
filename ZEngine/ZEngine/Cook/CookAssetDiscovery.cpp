#include <ZEngine/Cook/CookAdapterSupport.h>
#include <ZEngine/Cook/CookAssetDiscovery.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <algorithm>
#include <new>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    CookAssetDiscovery::~CookAssetDiscovery()
    {
        Reset();
    }

    VFS::VFSResult<void> CookAssetDiscovery::Initialize(ICookAllocator& allocator, const CookLimits& limits)
    {
        if (m_state != State::Empty || ValidateCookLimits(limits).Failed())
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        m_allocator     = &allocator;
        m_limits        = limits;
        m_state         = State::Ready;
        m_cleanup_error = VFS::VFSError::OK;
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookAssetDiscovery::Append()
    {
        if (m_count == m_limits.MaxAssets)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        const uint32_t block = m_count / AssetsPerBlock, offset = m_count % AssetsPerBlock;
        if (!m_blocks[block])
        {
            const size_t capacity  = std::min(AssetsPerBlock, m_limits.MaxAssets - m_count);
            const auto   allocated = m_allocator->Allocate(capacity * sizeof(CapturedAsset), alignof(CapturedAsset), CookMemoryCategory::Captures);
            if (allocated.Failed())
            {
                return VFS::VFSResult<void>::Fail(allocated.Error());
            }
            if (!allocated.Value())
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::OutOfMemory);
            }
            if (reinterpret_cast<uintptr_t>(allocated.Value()) % alignof(CapturedAsset))
            {
                m_allocator->Free(allocated.Value());
                return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
            }
            m_blocks[block] = static_cast<CapturedAsset*>(allocated.Value());
        }
        new (m_blocks[block] + offset) CapturedAsset{};
        ++m_count;
        return VFS::VFSResult<void>::Ok();
    }

    const CookAssetDiscovery::CapturedAsset& CookAssetDiscovery::Asset(uint32_t index) const noexcept
    {
        return m_blocks[index / AssetsPerBlock][index % AssetsPerBlock];
    }

    VFS::VFSResult<void> CookAssetDiscovery::PrepareInput(uint32_t index, ICookAllocator& scratch, VFS::StrictMetaFileData* metadata, CookAssetInput* output) const
    {
        if (index >= m_count || !metadata || !output)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        const auto& captured = Asset(index);
        if (!captured.Artifact.IsValid() || !captured.Metadata.IsValid() || !captured.Metadata.Size())
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
        const uint64_t size = captured.Metadata.Size();
        if (size > m_limits.MaxSidecarBytes || size > SIZE_MAX - sizeof(VFS::StrictMetaFileData))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        CookWorkspace workspace   = {};
        const auto    initialized = workspace.Initialize(scratch, sizeof(VFS::StrictMetaFileData) + static_cast<size_t>(size), CookMemoryCategory::Codecs);
        if (initialized.Failed())
        {
            return initialized;
        }
        auto*      parsed = new (workspace.Arena()->AllocateNoZero(sizeof(VFS::StrictMetaFileData), alignof(VFS::StrictMetaFileData))) VFS::StrictMetaFileData{};
        auto*      bytes  = static_cast<uint8_t*>(workspace.Arena()->AllocateNoZero(static_cast<size_t>(size), 1));
        const auto read   = ReadCookCapturedRange(captured.Metadata, 0, {bytes, static_cast<size_t>(size)});
        if (read.Failed())
        {
            return read;
        }
        const auto result = VFS::MetaFileIO::ParseStrict({bytes, static_cast<size_t>(size)}, parsed);
        if (result.Failed())
        {
            return result;
        }
        if (parsed->Data.AssetUUID != captured.Identity.UUID)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
        const CookAssetInput input = {.Identity = captured.Identity, .Artifact = &captured.Artifact, .EnvironmentSource = captured.Identity.Kind == CookAssetKind::EnvironmentMap ? &captured.Environment : nullptr, .Metadata = metadata, .Limits = &m_limits, .Source = captured.Source};
        *metadata                  = *parsed;
        *output                    = input;
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookAssetDiscovery::ReadInput(uint32_t index, ICookAllocator& scratch, VFS::StrictMetaFileData* metadata, CookAssetInput* output) const
    {
        return m_state == State::Complete ? PrepareInput(index, scratch, metadata, output) : VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
    }

    VFS::VFSResult<void> CookAssetDiscovery::Fail(const CookDependencyGraph& graph, uint32_t node, VFS::VFSError error, CookDiagnostics* diagnostics, cstring message, const CookAssetIdentity* dependency)
    {
        m_state         = State::Failed;
        const auto code = error == VFS::VFSError::NotFound ? CookDiagnosticCode::MissingDependency : Detail::AdapterErrorCode(error);
        if (auto* retained = diagnostics ? diagnostics->Emplace(CookStage::Discover, code, error) : nullptr)
        {
            auto& entry = *retained;
            (void) entry.SetMessage(message);
            if (graph.Describe(node, code, message, &entry).Succeeded())
            {
                entry.Underlying = error;
            }
            if (dependency)
            {
                entry.Asset = *dependency;
                if (entry.ChainCount < entry.Chain.size())
                {
                    entry.Chain[entry.ChainCount++] = *dependency;
                }
                else
                {
                    entry.Chain[entry.Chain.size() - 1] = *dependency;
                    entry.ChainTruncated                = true;
                }
            }
        }
        return VFS::VFSResult<void>::Fail(error);
    }

    VFS::VFSResult<void> CookAssetDiscovery::Discover(CookDependencyGraph& graph, CookInputLease& lease, const CookAdapterRegistry& adapters, ICookAllocator& scratch, CookDiagnostics* diagnostics)
    {
        if (m_state != State::Ready || lease.State() != CookInputState::Active || !adapters.IsInitialized())
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        m_state          = State::Running;
        const auto begun = graph.BeginDiscovery();
        if (begun.Failed())
        {
            return Fail(graph, CookGraphInvalidIndex, begun.Error(), diagnostics, "Asset discovery requires a graph with all scene roots.");
        }
        // Insertion-index traversal is an iterative work queue. SetInspection
        // interns newly found identities at the tail; every node is visited once.
        for (uint32_t index = 0; index < graph.NodeCount(); ++index)
        {
            if (graph.NodeCount() > m_limits.MaxAssets)
            {
                return Fail(graph, index, VFS::VFSError::SizeLimitExceeded, diagnostics, "Asset discovery exceeded its unique-identity limit.");
            }
            const auto appended = Append();
            if (appended.Failed())
            {
                return Fail(graph, index, appended.Error(), diagnostics, "Cannot allocate bounded capture-owner storage.");
            }
            auto& captured              = m_blocks[index / AssetsPerBlock][index % AssetsPerBlock];
            captured.Identity           = graph.Node(index)->Identity;
            CookAssetCatalogEntry entry = {};
            const auto            found = lease.FindAsset(captured.Identity, &entry, diagnostics);
            if (found.Failed())
            {
                return Fail(graph, index, found.Error(), diagnostics, "A reachable typed asset is absent from the frozen catalog.");
            }
            captured.Source     = entry.Artifact;
            const auto artifact = lease.Capture(entry.Artifact, &captured.Artifact, diagnostics);
            if (artifact.Failed())
            {
                return Fail(graph, index, artifact.Error(), diagnostics, "Cannot capture a reachable imported artifact.");
            }
            const auto sidecar = lease.Capture(entry.Metadata, &captured.Metadata, diagnostics);
            if (sidecar.Failed())
            {
                return Fail(graph, index, sidecar.Error(), diagnostics, "Cannot capture a reachable asset sidecar.");
            }
            if (captured.Identity.Kind == CookAssetKind::EnvironmentMap)
            {
                const auto environment = lease.Capture(entry.EnvironmentSource, &captured.Environment, diagnostics);
                if (environment.Failed())
                {
                    return Fail(graph, index, environment.Error(), diagnostics, "Cannot capture an environment's imported source.");
                }
            }
            VFS::StrictMetaFileData metadata = {};
            CookAssetInput          input    = {};
            const auto              prepared = PrepareInput(index, scratch, &metadata, &input);
            if (prepared.Failed())
            {
                return Fail(graph, index, prepared.Error(), diagnostics, "Cannot read captured strict asset metadata.");
            }
            const auto*         adapter    = adapters.Find(captured.Identity.Kind);
            CookAssetInspection inspection = {};
            const auto          inspected  = adapter->Inspect(adapter->Context, input, scratch, &inspection, diagnostics);
            if (inspected.Failed())
            {
                return Fail(graph, index, inspected.Error(), diagnostics, "Shared asset validation failed during dependency discovery.");
            }
            for (const auto& dependency : inspection.Dependencies())
            {
                CookAssetCatalogEntry resolved = {};
                const auto            matched  = ResolveCookDependency(lease, dependency, &resolved, diagnostics);
                if (matched.Failed())
                {
                    return Fail(graph, index, matched.Error(), diagnostics, "A dependency's typed UUID/original path does not match the frozen catalog.", &dependency.Identity);
                }
            }
            const auto stored = graph.SetInspection(index, adapter->Revision, inspection);
            if (stored.Failed())
            {
                return Fail(graph, index, stored.Error(), diagnostics, "Cannot commit inspected dependencies to the bounded graph.");
            }
        }
        const auto finalized = graph.Finalize(diagnostics);
        if (finalized.Failed())
        {
            m_state = State::Failed;
            return finalized;
        }
        m_state = State::Complete;
        return VFS::VFSResult<void>::Ok();
    }

    uint32_t CookAssetDiscovery::Count() const noexcept
    {
        return m_state == State::Complete ? m_count : 0;
    }

    void CookAssetDiscovery::Reset() noexcept
    {
        const auto release = [&](CookCapturedFile& file) {
            const auto result = file.Reset();
            if (result.Failed() && m_cleanup_error == VFS::VFSError::OK)
            {
                m_cleanup_error = result.Error();
            }
        };
        for (uint32_t i = 0; i < m_count; ++i)
        {
            auto& asset = m_blocks[i / AssetsPerBlock][i % AssetsPerBlock];
            release(asset.Artifact);
            release(asset.Metadata);
            release(asset.Environment);
            asset.~CapturedAsset();
        }
        for (auto*& block : m_blocks)
        {
            if (block)
            {
                m_allocator->Free(block);
                block = nullptr;
            }
        }
        m_allocator = nullptr;
        m_limits    = {};
        m_count     = 0;
        m_state     = State::Empty;
    }
} // namespace ZEngine::Cook
