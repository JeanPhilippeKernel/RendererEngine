#include <ZEngine/Cook/CookSceneCaptureSet.h>
#include <ZEngine/Cook/CookSort.h>
#include <ZEngine/Cook/CookStorageLayout.h>
#include <algorithm>
#include <new>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    CookSceneCaptureSet::~CookSceneCaptureSet()
    {
        (void) Reset();
    }

    VFS::VFSResult<void> CookSceneCaptureSet::Fail(VFS::VFSError error, CookDiagnosticCode code, cstring message, CookDiagnostics* diagnostics, const VFS::VFSRelativePath& source, const uuids::uuid& uuid)
    {
        if (auto* entry = diagnostics ? diagnostics->Emplace(CookStage::Capture, code, error) : nullptr)
        {
            entry->Source    = source;
            entry->SceneUUID = uuid;
            (void) entry->SetMessage(message);
        }
        return VFS::VFSResult<void>::Fail(error);
    }

    VFS::VFSResult<void> CookSceneCaptureSet::Reset() noexcept
    {
        for (uint32_t i = 0; i < m_count; ++i)
        {
            const auto released = m_entries[i].Capture.Reset();
            if (released.Failed() && m_cleanup_error == VFS::VFSError::OK)
                m_cleanup_error = released.Error();
            m_entries[i].~Entry();
        }
        if (m_entries)
            m_allocator->Free(m_entries);
        m_allocator = nullptr;
        m_entries   = nullptr;
        m_limits    = {};
        m_count     = 0;
        m_complete  = false;
        return m_cleanup_error == VFS::VFSError::OK ? VFS::VFSResult<void>::Ok() : VFS::VFSResult<void>::Fail(m_cleanup_error);
    }

    VFS::VFSResult<CookSceneInput> CookSceneCaptureSet::Input(uint32_t index) const noexcept
    {
        CookSceneInput input  = {};
        const auto     status = InputInto(index, &input);
        return status.Failed() ? VFS::VFSResult<CookSceneInput>::Fail(status.Error()) : VFS::VFSResult<CookSceneInput>::Ok(input);
    }

    VFS::VFSResult<void> CookSceneCaptureSet::InputInto(uint32_t index, CookSceneInput* output) const noexcept
    {
        if (!output || !m_complete || index >= m_count)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        output->Capture = &m_entries[index].Capture;
        output->Source  = m_entries[index].Source;
        output->Limits  = &m_limits;
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookSceneCaptureSet::Capture(ICookAllocator& allocator, Core::Containers::ArrayView<const VFS::VFSPath> sources, CookInputLease& lease, CookSceneCodec& codec, CookDependencyGraph& graph, const CookLimits& limits, CookDiagnostics* diagnostics)
    {
        if (m_entries)
            return Fail(VFS::VFSError::AlreadyExists, CookDiagnosticCode::InvalidRequest, "A scene capture set belongs to one run; Reset before reinitializing.", diagnostics);
        if (!sources.data() || !sources.size() || lease.State() != CookInputState::Active || !codec.Schemas().IsInitialized() || graph.SceneCount() || graph.NodeCount() || graph.IsFinalized() || ValidateCookLimits(limits).Failed())
            return Fail(VFS::VFSError::InvalidPath, CookDiagnosticCode::InvalidRequest, "Scene capture requires a nonempty request, active inputs, valid limits and an initialized empty graph.", diagnostics);
        if (sources.size() > limits.MaxScenes)
            return Fail(VFS::VFSError::SizeLimitExceeded, CookDiagnosticCode::BudgetExceeded, "The complete requested scene set exceeds its configured count cap.", diagnostics);
        for (const auto& source : sources)
            if (!source.IsValid() || source.IsRoot())
                return Fail(VFS::VFSError::InvalidPath, CookDiagnosticCode::InvalidPath, "Every requested scene must name a file within a declared input root.", diagnostics);

        CookMemoryCharge fixed = {};
        if (sources.size() > 1)
        {
            const auto charged = allocator.TryCharge(sizeof(Detail::CookSortState<uint32_t>) + sizeof(fixed), CookMemoryCategory::Captures, &fixed);
            if (charged.Failed())
            {
                const auto error = charged.Error();
                const auto code  = error == VFS::VFSError::SizeLimitExceeded ? CookDiagnosticCode::BudgetExceeded : error == VFS::VFSError::OutOfMemory ? CookDiagnosticCode::AllocationFailed : error == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : error == VFS::VFSError::InvalidPath ? CookDiagnosticCode::InvalidRequest : CookDiagnosticCode::IOFailure;
                return Fail(error, code, "Scene path sorting requires budgeted fixed state.", diagnostics);
            }
        }

        Detail::CookStorageLayout layout  = {};
        size_t                    entries = 0, indexes = 0;
        if (!layout.Add<Entry>(sources.size(), &entries) || !layout.Add<uint32_t>(sources.size(), &indexes))
            return Fail(VFS::VFSError::SizeLimitExceeded, CookDiagnosticCode::BudgetExceeded, "Scene capture storage size overflowed before allocation.", diagnostics);
        const auto allocated = allocator.Allocate(layout.Size(), std::max(alignof(Entry), alignof(uint32_t)), CookMemoryCategory::Captures);
        if (allocated.Failed() || !allocated.Value())
        {
            const auto error = allocated.Failed() ? allocated.Error() : VFS::VFSError::OutOfMemory;
            return Fail(error, error == VFS::VFSError::SizeLimitExceeded ? CookDiagnosticCode::BudgetExceeded : CookDiagnosticCode::AllocationFailed, "Could not allocate the exact scene-owner/path-index table.", diagnostics);
        }
        if (reinterpret_cast<uintptr_t>(allocated.Value()) % std::max(alignof(Entry), alignof(uint32_t)))
        {
            allocator.Free(allocated.Value());
            return Fail(VFS::VFSError::InvalidPath, CookDiagnosticCode::InvalidRequest, "Scene-owner storage must honor the requested alignment.", diagnostics);
        }
        m_allocator     = &allocator;
        m_entries       = reinterpret_cast<Entry*>(static_cast<uint8_t*>(allocated.Value()) + entries);
        m_count         = static_cast<uint32_t>(sources.size());
        m_limits        = limits;
        m_cleanup_error = VFS::VFSError::OK;
        auto* order     = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(allocated.Value()) + indexes);
        for (uint32_t i = 0; i < m_count; ++i)
        {
            new (m_entries + i) Entry{};
            new (order + i) uint32_t(i);
        }
        // All mappings are known before any scene capture. Sort only indexes,
        // never relocate the capture owners or change request/graph indexes.
        for (uint32_t i = 0; i < m_count; ++i)
        {
            const auto mapped = lease.MapContextPath(sources[i], &m_entries[i].Source, diagnostics);
            if (mapped.Failed())
                return Fail(mapped.Error(), mapped.Error() == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : CookDiagnosticCode::InvalidPath, "Requested scene could not be mapped exactly to a declared logical input root.", diagnostics);
        }
        if (m_count > 1)
        {
            Detail::CookSortState<uint32_t> sort = {};
            Core::Containers::HeapSort<uint32_t>({order, m_count}, sort, [&](uint32_t left, uint32_t right) { return m_entries[left].Source < m_entries[right].Source; });
        }
        fixed.Release(); // Sorting state is gone before per-scene codec work.
        for (uint32_t i = 1; i < m_count; ++i)
            if (m_entries[order[i - 1]].Source == m_entries[order[i]].Source)
                return Fail(VFS::VFSError::AlreadyExists, CookDiagnosticCode::IdentityConflict, "Duplicate logical scene paths are invalid; requests are never silently deduplicated.", diagnostics, m_entries[order[i]].Source);
        for (uint32_t i = 0; i < m_count; ++i)
        {
            auto&      entry    = m_entries[i];
            const auto captured = lease.Capture(entry.Source, &entry.Capture, diagnostics);
            if (captured.Failed())
                return captured;
            ECS::SceneDocument document = {};
            const auto         parsed   = codec.Read({&entry.Capture, entry.Source, &m_limits}, &document, diagnostics);
            if (parsed.Failed())
                return parsed;
            const auto added = graph.AddScene(document, codec.Schemas(), entry.Source, diagnostics);
            if (added.Failed())
                return Fail(added.Error(), added.Error() == VFS::VFSError::AlreadyExists ? CookDiagnosticCode::IdentityConflict : added.Error() == VFS::VFSError::SizeLimitExceeded ? CookDiagnosticCode::BudgetExceeded : added.Error() == VFS::VFSError::OutOfMemory ? CookDiagnosticCode::AllocationFailed : CookDiagnosticCode::InvalidScene, "Captured scene identity/reference validation failed; discard the partial graph.", diagnostics, entry.Source, document.Settings() ? document.Settings()->UUID : uuids::uuid{});
        }
        m_complete = true;
        return VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Cook
