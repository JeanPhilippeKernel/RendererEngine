#pragma once
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <uuid.h>

namespace ZEngine::Core::VFS
{
    // Material identity extraction uses a JSON parser that may temporarily retain one
    // string token. Keep that bounded without imposing a limit on mesh assets.
    inline constexpr uint64_t            MATERIAL_IDENTITY_MAX_FILE_SIZE = 1024ULL * 1024ULL;

    // Read only the embedded identity; material JSON is streamed without an arena or DOM.
    [[nodiscard]] VFSResult<uuids::uuid> ReadEmbeddedAssetUUID(IVFSContext& ctx, const VFSPath& path);

    // Create one private directory through retained scratch authority. Ownership
    // comes only from exclusive creation; collisions are never removed. The
    // caller removes its known contents and this directory before releasing root.
    // No allocation, recursive cleanup, or live mount lookup; failure preserves out.
    [[nodiscard]] VFSResult<void>        CreatePrivateScratchDirectory(VFSScratchRootLease& root, VFSRelativePath* out);

    enum class AtomicWriteMode : uint8_t
    {
        Replace   = 0,
        NoReplace = 1,
    };

    // Inline streaming state; no own allocation or callable/control block.
    // Context/root outlives this owner and every cleanup attempt. Open acquires
    // ownership ONLY through exclusive sibling creation. Seal checked-closes the
    // writer, so callers may reopen TemporaryPath to verify actual staged bytes
    // before Commit. Commit accepts a different filename in the SAME directory
    // (e.g. an object digest learned while streaming), never another mount/root.
    // Failed writes/flushes/closes poison this stage. Failed renames retain the
    // sealed owned temporary, permitting inspection or explicit Reset cleanup.
    // Reset reports cleanup failures and forgets authority without hidden retries;
    // save TemporaryPath first when reporting them. Destruction is best effort.
    // Once committed, Reset/destruction never removes the published destination.
    // Wrappers/backends may allocate; their storage remains caller-accounted.
    template <typename Context, typename Path>
    class VFSAtomicFileWriter
    {
    public:
        VFSAtomicFileWriter() = default;
        ~VFSAtomicFileWriter();
        VFSAtomicFileWriter(const VFSAtomicFileWriter&)                     = delete;
        VFSAtomicFileWriter&          operator=(const VFSAtomicFileWriter&) = delete;

        [[nodiscard]] VFSResult<void> Open(Context&, const Path& sibling_of);
        [[nodiscard]] VFSResult<void> Write(Containers::ArrayView<const uint8_t>);
        [[nodiscard]] VFSResult<void> Seal();
        [[nodiscard]] VFSResult<void> Commit(const Path& destination, AtomicWriteMode = AtomicWriteMode::Replace);
        [[nodiscard]] VFSResult<void> Reset() noexcept;
        [[nodiscard]] const Path&     TemporaryPath() const noexcept
        {
            return m_temporary;
        }
        [[nodiscard]] uint64_t BytesWritten() const noexcept
        {
            return m_bytes;
        }
        [[nodiscard]] bool OwnsTemporary() const noexcept
        {
            return m_context && m_state != State::Committed;
        }
        [[nodiscard]] bool IsSealed() const noexcept
        {
            return m_state == State::Sealed;
        }
        [[nodiscard]] bool IsCommitted() const noexcept
        {
            return m_state == State::Committed;
        }
        [[nodiscard]] VFSError Error() const noexcept
        {
            return m_error;
        }
        [[nodiscard]] VFSError CleanupError() const noexcept
        {
            return m_cleanup_error;
        }

    private:
        enum class State : uint8_t
        {
            Empty = 0,
            Writing,
            Sealed,
            Committed,
            Failed,
        };
        Context*  m_context       = nullptr;
        IVFSFile* m_file          = nullptr;
        Path      m_temporary     = {};
        uint64_t  m_bytes         = 0;
        State     m_state         = State::Empty;
        VFSError  m_error         = VFSError::OK;
        VFSError  m_cleanup_error = VFSError::OK;
    };

    // Only these two explicit instantiations are provided. Both use the same
    // streaming/flush/close/rename implementation as WriteFileAtomically below.
    using VFSContextAtomicFileWriter = VFSAtomicFileWriter<IVFSContext, VFSPath>;
    using VFSRootAtomicFileWriter    = VFSAtomicFileWriter<VFSPublicationRootLease, VFSRelativePath>;
    extern template class VFSAtomicFileWriter<IVFSContext, VFSPath>;
    extern template class VFSAtomicFileWriter<VFSPublicationRootLease, VFSRelativePath>;

    // Publish using an exclusively owned temporary sibling on the same mount/filesystem.
    // NoReplace returns AlreadyExists without changing an existing destination.
    [[nodiscard]] VFSResult<void> WriteFileAtomically(IVFSContext& ctx, const VFSPath& path, Containers::ArrayView<const uint8_t> data, AtomicWriteMode mode = AtomicWriteMode::Replace);
    [[nodiscard]] VFSResult<void> WriteFileAtomically(VFSPublicationRootLease& root, const VFSRelativePath& path, Containers::ArrayView<const uint8_t> data, AtomicWriteMode mode = AtomicWriteMode::Replace);
} // namespace ZEngine::Core::VFS
