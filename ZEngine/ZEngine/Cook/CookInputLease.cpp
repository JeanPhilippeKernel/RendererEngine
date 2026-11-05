#include <ZEngine/Cook/CookInputLease.h>
#include <utility>

namespace ZEngine::Cook
{
    namespace
    {
        Core::VFS::VFSResult<void> InvalidLeaseRequest(CookDiagnostics* diagnostics)
        {
            if (auto* entry = diagnostics ? diagnostics->Emplace(CookStage::Capture, CookDiagnosticCode::InvalidRequest) : nullptr)
            {
                (void) entry->SetMessage("Input lease is inactive or the requested operation is invalid.");
            }
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }
    } // namespace

    bool IsCookAssetCatalogEntryValid(const CookAssetCatalogEntry& entry) noexcept
    {
        return entry.Identity.IsValid() && IsCookLogicalPath(entry.Artifact) && IsCookLogicalPath(entry.Metadata) && entry.Artifact != entry.Metadata && (entry.Identity.Kind == CookAssetKind::EnvironmentMap ? IsCookLogicalPath(entry.EnvironmentSource) && entry.EnvironmentSource != entry.Artifact && entry.EnvironmentSource != entry.Metadata : !entry.EnvironmentSource.IsValid());
    }

    CookInputLease::~CookInputLease()
    {
        Reset();
    }
    CookInputLease::CookInputLease(CookInputLease&& other) noexcept : m_session(std::exchange(other.m_session, nullptr)), m_state(std::exchange(other.m_state, CookInputState::Invalid)) {}
    CookInputLease& CookInputLease::operator=(CookInputLease&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_session = std::exchange(other.m_session, nullptr);
            m_state   = std::exchange(other.m_state, CookInputState::Invalid);
        }
        return *this;
    }

    void CookInputLease::Reset() noexcept
    {
        auto* session = std::exchange(m_session, nullptr);
        m_state       = CookInputState::Invalid;
        if (session)
        {
            session->Release();
        }
    }

    Core::VFS::VFSResult<void> CookInputLease::EnumerateCatalog(CookCatalogVisitorFn visitor, void* context, CookDiagnostics* diagnostics)
    {
        if (m_state != CookInputState::Active || !m_session || !visitor)
        {
            return InvalidLeaseRequest(diagnostics);
        }
        return m_session->EnumerateCatalog(visitor, context, diagnostics);
    }

    Core::VFS::VFSResult<void> CookInputLease::RequireDisjoint(const Core::VFS::VFSRootLease& root, CookDiagnostics* diagnostics)
    {
        if (m_state != CookInputState::Rooted || !m_session || !root.IsValid())
        {
            return InvalidLeaseRequest(diagnostics);
        }
        return m_session->RequireDisjoint(root, diagnostics);
    }

    Core::VFS::VFSResult<void> CookInputLease::PrepareCapture(const CookCaptureContext& context, CookDiagnostics* diagnostics)
    {
        if (m_state != CookInputState::Rooted || !m_session)
        {
            return InvalidLeaseRequest(diagnostics);
        }
        const auto prepared = m_session->PrepareCapture(context, diagnostics);
        m_state             = prepared.Succeeded() ? CookInputState::Active : CookInputState::Failed;
        return prepared;
    }

    Core::VFS::VFSResult<void> CookInputLease::MapContextPath(const Core::VFS::VFSPath& path, Core::VFS::VFSRelativePath* output, CookDiagnostics* diagnostics)
    {
        if (m_state != CookInputState::Active || !m_session || !output || !path.IsValid())
        {
            return InvalidLeaseRequest(diagnostics);
        }
        Core::VFS::VFSRelativePath pending = {};
        const auto                 mapped  = m_session->MapContextPath(path, &pending, diagnostics);
        if (mapped.Failed())
        {
            return mapped;
        }
        if (!IsCookLogicalPath(pending))
        {
            return InvalidLeaseRequest(diagnostics);
        }
        *output = pending;
        return Core::VFS::VFSResult<void>::Ok();
    }

    Core::VFS::VFSResult<void> CookInputLease::FindAsset(const CookAssetIdentity& identity, CookAssetCatalogEntry* output, CookDiagnostics* diagnostics)
    {
        if (m_state != CookInputState::Active || !m_session || !output || !identity.IsValid())
        {
            return InvalidLeaseRequest(diagnostics);
        }
        CookAssetCatalogEntry pending = {};
        const auto            found   = m_session->FindAsset(identity, &pending, diagnostics);
        if (found.Failed())
        {
            return found;
        }
        if (pending.Identity != identity || !IsCookAssetCatalogEntryValid(pending))
        {
            return InvalidLeaseRequest(diagnostics);
        }
        *output = pending;
        return Core::VFS::VFSResult<void>::Ok();
    }

    Core::VFS::VFSResult<void> CookInputLease::Capture(const Core::VFS::VFSRelativePath& path, CookCapturedFile* output, CookDiagnostics* diagnostics)
    {
        if (m_state != CookInputState::Active || !m_session || !output)
        {
            return InvalidLeaseRequest(diagnostics);
        }
        if (!IsCookLogicalPath(path))
        {
            if (auto* entry = diagnostics ? diagnostics->Emplace(CookStage::Capture, CookDiagnosticCode::InvalidPath) : nullptr)
            {
                entry->Source = path;
                (void) entry->SetMessage("A captured input requires a declared logical root key and a relative file path.");
            }
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        CookCapturedFile pending  = {};
        const auto       captured = m_session->Capture(path, &pending, diagnostics);
        if (captured.Failed())
        {
            return captured;
        }
        if (!pending.IsValid())
        {
            if (auto* entry = diagnostics ? diagnostics->Emplace(CookStage::Capture, CookDiagnosticCode::InvalidAsset) : nullptr)
            {
                entry->Source = path;
                (void) entry->SetMessage("Input provider returned success without an owned immutable capture.");
            }
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Corrupted);
        }
        *output = std::move(pending);
        return Core::VFS::VFSResult<void>::Ok();
    }

    Core::VFS::VFSResult<void> CookInputLease::Seal(CookDiagnostics* diagnostics)
    {
        if (m_state != CookInputState::Active || !m_session)
        {
            return InvalidLeaseRequest(diagnostics);
        }
        const auto sealed = m_session->Seal(diagnostics);
        m_state           = sealed.Succeeded() ? CookInputState::Sealed : CookInputState::Failed;
        return sealed;
    }

    Core::VFS::VFSResult<void> ICookInputSource::BindLease(Detail::ICookInputSession& session, CookInputLease* output, CookInputState initial)
    {
        if (!output || (initial != CookInputState::Rooted && initial != CookInputState::Active))
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        if (output->m_session)
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::AlreadyExists);
        }
        output->m_session = &session;
        output->m_state   = initial;
        return Core::VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Cook
