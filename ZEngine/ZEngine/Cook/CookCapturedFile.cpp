#include <ZEngine/Cook/CookCapturedFile.h>
#include <ZEngine/Cook/CookLimits.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <algorithm>
#include <limits>
#include <utility>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        template <typename Transfer>
        VFS::VFSResult<void> TransferCaptureRange(size_t count, Transfer&& transfer, VFS::VFSError premature_eof = VFS::VFSError::IOError)
        {
            size_t done = 0;
            while (done < count)
            {
                const auto result = transfer(done, count - done);
                if (result.Failed())
                    return VFS::VFSResult<void>::Fail(result.Error());
                if (!result.Value())
                    return VFS::VFSResult<void>::Fail(premature_eof);
                if (result.Value() > count - done)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::IOError);
                done += result.Value();
            }
            return VFS::VFSResult<void>::Ok();
        }

        VFS::VFSResult<void> ReadCaptureRange(VFS::IVFSFile& file, uint64_t offset, Core::Containers::ArrayView<uint8_t> bytes, VFS::VFSError premature_eof = VFS::VFSError::IOError)
        {
            return TransferCaptureRange(bytes.size(), [&](size_t done, size_t remaining) { return file.Read({bytes.data() + done, remaining}, offset + done); }, premature_eof);
        }

        VFS::VFSResult<void> WriteCaptureRange(VFS::IVFSFile& file, uint64_t offset, Core::Containers::ArrayView<const uint8_t> bytes)
        {
            return TransferCaptureRange(bytes.size(), [&](size_t done, size_t remaining) { return file.Write({bytes.data() + done, remaining}, offset + done); });
        }

        VFS::VFSResult<uint64_t> SourceSize(VFS::IVFSFile& source)
        {
            const auto stat = source.Stat();
            if (stat.Failed())
                return VFS::VFSResult<uint64_t>::Fail(stat.Error());
            if (stat.Value().IsDirectory)
                return VFS::VFSResult<uint64_t>::Fail(VFS::VFSError::NotAFile);
            const auto size = source.Size();
            if (size.Failed())
                return size;
            return size.Value() != stat.Value().SizeBytes ? VFS::VFSResult<uint64_t>::Fail(VFS::VFSError::Corrupted) : size;
        }

        template <typename Buffer, typename Consume>
        VFS::VFSResult<void> HashSource(VFS::IVFSFile& source, uint64_t size, Core::Hash::SHA256& hash, Buffer&& acquire_buffer, Consume&& consume, Digest256* digest, VFS::VFSError premature_eof = VFS::VFSError::IOError)
        {
            for (uint64_t offset = 0; offset < size;)
            {
                const auto storage = acquire_buffer(offset);
                if (!storage.data() || !storage.size())
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                const auto count = static_cast<size_t>(std::min<uint64_t>(size - offset, std::min<size_t>(storage.size(), ZKilo(64))));
                const auto read  = ReadCaptureRange(source, offset, {storage.data(), count}, premature_eof);
                if (read.Failed())
                    return read;
                const auto hashed = hash.Update({storage.data(), count});
                if (hashed.Failed())
                    return hashed;
                const auto consumed = consume(offset, Core::Containers::ArrayView<const uint8_t>{storage.data(), count});
                if (consumed.Failed())
                    return consumed;
                offset += count;
            }
            uint8_t    extra = 0;
            const auto end   = source.Read({&extra, 1}, size);
            if (end.Failed())
                return VFS::VFSResult<void>::Fail(end.Error());
            const auto final_size = source.Size();
            if (final_size.Failed())
                return VFS::VFSResult<void>::Fail(final_size.Error());
            if (end.Value() || final_size.Value() != size)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            return hash.Finalize(digest);
        }
    } // namespace

    VFS::VFSResult<void> ReadCookCapturedRange(const ICookCapturedFile& file, uint64_t offset, Core::Containers::ArrayView<uint8_t> bytes)
    {
        if ((bytes.size() && !bytes.data()) || offset > file.Size() || bytes.size() > file.Size() - offset)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        return TransferCaptureRange(bytes.size(), [&](size_t done, size_t remaining) { return file.Read(offset + done, {bytes.data() + done, remaining}); });
    }

    CookCapturedFile::~CookCapturedFile()
    {
        (void) Reset();
    }

    CookCapturedFile::CookCapturedFile(CookCapturedFile&& other) noexcept : m_store(std::exchange(other.m_store, nullptr)), m_memory(std::exchange(other.m_memory, nullptr)), m_disk_path(std::exchange(other.m_disk_path, {})), m_size(std::exchange(other.m_size, 0)), m_digest(std::exchange(other.m_digest, {})) {}

    CookCapturedFile& CookCapturedFile::operator=(CookCapturedFile&& other) noexcept
    {
        if (this != &other)
        {
            (void) Reset();
            m_store     = std::exchange(other.m_store, nullptr);
            m_memory    = std::exchange(other.m_memory, nullptr);
            m_disk_path = std::exchange(other.m_disk_path, {});
            m_size      = std::exchange(other.m_size, 0);
            m_digest    = std::exchange(other.m_digest, {});
        }
        return *this;
    }

    bool CookCapturedFile::IsValid() const noexcept
    {
        return m_store != nullptr;
    }
    uint64_t CookCapturedFile::Size() const noexcept
    {
        return m_size;
    }
    Digest256 CookCapturedFile::Digest() const noexcept
    {
        return m_digest;
    }

    VFS::VFSResult<size_t> CookCapturedFile::Read(uint64_t offset, Core::Containers::ArrayView<uint8_t> output) const
    {
        if (!m_store || (output.size() && !output.data()))
            return VFS::VFSResult<size_t>::Fail(VFS::VFSError::InvalidPath);
        if (offset >= m_size || !output.size())
            return VFS::VFSResult<size_t>::Ok(0);
        const size_t count = static_cast<size_t>(std::min<uint64_t>(output.size(), m_size - offset));
        if (!m_disk_path.IsValid())
        {
            Helpers::secure_memcpy(output.data(), output.size(), m_memory + static_cast<size_t>(offset), count);
            return VFS::VFSResult<size_t>::Ok(count);
        }
        const auto file = m_store->m_scratch->Open(m_disk_path, VFS::VFSOpenFlags::Read);
        if (file.Failed())
            return VFS::VFSResult<size_t>::Fail(file.Error());
        const auto read  = ReadCaptureRange(*file.Value(), offset, {output.data(), count});
        const auto close = m_store->m_scratch->CloseChecked(file.Value());
        if (read.Failed())
            return VFS::VFSResult<size_t>::Fail(read.Error());
        return close.Succeeded() ? VFS::VFSResult<size_t>::Ok(count) : VFS::VFSResult<size_t>::Fail(close.Error());
    }

    VFS::VFSResult<void> CookCapturedFile::Reset() noexcept
    {
        if (!m_store)
            return VFS::VFSResult<void>::Ok();
        const auto release = m_store->Release(*this);
        m_store            = nullptr;
        m_memory           = nullptr;
        m_disk_path        = {};
        m_size             = 0;
        m_digest           = {};
        return release;
    }

    CookCaptureStore::~CookCaptureStore()
    {
        ZENGINE_VALIDATE_ASSERT(!m_outstanding, "Cook capture store destroyed before captured files.");
    }

    VFS::VFSResult<void> CookCaptureStore::Initialize(const CookCaptureContext& context, Core::Containers::ArrayView<uint8_t> stream_storage)
    {
        if (m_allocator || !context.Allocator || !context.Limits || !context.Scratch || !context.Scratch->IsValid() || !context.ScratchRun.IsValid() || (stream_storage.size() && !stream_storage.data()) || stream_storage.size() > ZKilo(64))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        const auto limits = ValidateCookLimits(*context.Limits);
        if (limits.Failed())
            return limits;
        const auto run = context.Scratch->Stat(context.ScratchRun);
        if (run.Failed())
            return VFS::VFSResult<void>::Fail(run.Error());
        if (!run.Value().IsDirectory)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::NotADirectory);
        m_allocator = context.Allocator;
        m_scratch   = context.Scratch;
        m_run       = context.ScratchRun;
        m_stream    = stream_storage;
        m_limit     = context.Limits->MaxCapturedBytes;
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookCaptureStore::Release(CookCapturedFile& file) noexcept
    {
        VFS::VFSResult<void> cleanup = VFS::VFSResult<void>::Ok();
        if (file.m_memory)
            m_allocator->Free(file.m_memory);
        if (file.m_disk_path.IsValid())
        {
            cleanup = m_scratch->Remove(file.m_disk_path);
            if (cleanup.Error() == VFS::VFSError::NotFound)
                cleanup = VFS::VFSResult<void>::Ok();
        }
        if (cleanup.Succeeded())
            m_live_bytes -= file.m_size;
        else if (m_cleanup_error == VFS::VFSError::OK)
            m_cleanup_error = cleanup.Error();
        // Failed cleanup remains charged: repeated failures cannot bypass the
        // retained-capture budget by abandoning files beneath the owned run.
        --m_outstanding;
        return cleanup;
    }

    VFS::VFSResult<void> CookCaptureStore::CaptureMemory(VFS::IVFSFile& source, uint64_t maximum_bytes, CookCapturedFile* output)
    {
        return Capture(source, maximum_bytes, false, output);
    }

    VFS::VFSResult<void> CookCaptureStore::CaptureDisk(VFS::IVFSFile& source, uint64_t maximum_bytes, CookCapturedFile* output)
    {
        return Capture(source, maximum_bytes, true, output);
    }

    VFS::VFSResult<void> CookCaptureStore::VerifySource(VFS::IVFSFile& source, uint64_t expected_size, const Digest256& expected_digest) const
    {
        if (!m_allocator)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        CookMemoryCharge fixed   = {};
        const auto       charged = m_allocator->TryCharge(Core::Hash::SHA256::FixedWorkingBytes() + sizeof(Digest256) + sizeof(uint8_t) + sizeof(fixed), CookMemoryCategory::Captures, &fixed);
        if (charged.Failed())
            return charged;
        const auto size = SourceSize(source);
        if (size.Failed())
            return VFS::VFSResult<void>::Fail(size.Error());
        if (size.Value() != expected_size)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        Core::Hash::SHA256 hash   = {};
        Digest256          digest = {};
        const auto         hashed = HashSource(source, expected_size, hash, [&](uint64_t) { return m_stream; }, [](uint64_t, Core::Containers::ArrayView<const uint8_t>) { return VFS::VFSResult<void>::Ok(); }, &digest, VFS::VFSError::Corrupted);
        if (hashed.Failed())
            return hashed;
        return digest == expected_digest ? VFS::VFSResult<void>::Ok() : VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
    }

    VFS::VFSResult<void> CookCaptureStore::Capture(VFS::IVFSFile& source, uint64_t maximum_bytes, bool disk, CookCapturedFile* output)
    {
        if (!m_allocator || !output)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        // Reserve principal temporary capture/hash state before touching the
        // source or staging a file. It is outside the retained stream pool.
        CookMemoryCharge fixed   = {};
        const auto       charged = m_allocator->TryCharge(Core::Hash::SHA256::FixedWorkingBytes() + sizeof(CookCapturedFile) + sizeof(uint8_t) + sizeof(fixed), CookMemoryCategory::Captures, &fixed);
        if (charged.Failed())
            return charged;
        const auto size = SourceSize(source);
        if (size.Failed())
            return VFS::VFSResult<void>::Fail(size.Error());
        if (size.Value() > maximum_bytes || size.Value() > m_limit - m_live_bytes || (!disk && size.Value() > SIZE_MAX))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        if (disk && size.Value() && !m_stream.size())
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        Core::Hash::SHA256 hash     = {};
        CookCapturedFile   pending  = {};
        pending.m_store             = this;
        pending.m_size              = size.Value();
        m_live_bytes               += size.Value();
        ++m_outstanding;
        VFS::IVFSFile* writer = nullptr;
        if (disk)
        {
            for (uint32_t attempt = 0; attempt < 16; ++attempt)
            {
                if (m_sequence == std::numeric_limits<uint64_t>::max())
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                char           name[25] = "capture-0000000000000000";
                constexpr char digits[] = "0123456789abcdef";
                const auto     sequence = m_sequence++;
                for (uint32_t i = 0; i < 16; ++i)
                    name[8 + i] = digits[(sequence >> ((15 - i) * 4)) & 0x0f];
                const auto path = m_run.Append(name);
                if (path.Failed())
                    return VFS::VFSResult<void>::Fail(path.Error());
                const auto file = m_scratch->Open(path.Value(), VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Create | VFS::VFSOpenFlags::Exclusive);
                if (file.Error() == VFS::VFSError::AlreadyExists)
                    continue;
                if (file.Failed())
                    return VFS::VFSResult<void>::Fail(file.Error());
                pending.m_disk_path = path.Value();
                writer              = file.Value();
                break;
            }
            if (!writer)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::AlreadyExists);
        }
        else if (size.Value())
        {
            const auto allocation = m_allocator->Allocate(static_cast<size_t>(size.Value()), DEFAULT_ALIGNMENT, CookMemoryCategory::Captures);
            if (allocation.Failed())
                return VFS::VFSResult<void>::Fail(allocation.Error());
            if (!allocation.Value())
                return VFS::VFSResult<void>::Fail(VFS::VFSError::OutOfMemory);
            pending.m_memory = static_cast<uint8_t*>(allocation.Value());
        }

        const auto           copy  = HashSource(source, size.Value(), hash, [&](uint64_t offset) -> Core::Containers::ArrayView<uint8_t> { return disk ? m_stream : Core::Containers::ArrayView<uint8_t>{pending.m_memory + static_cast<size_t>(offset), static_cast<size_t>(size.Value() - offset)}; }, [&](uint64_t offset, Core::Containers::ArrayView<const uint8_t> bytes) { return writer ? WriteCaptureRange(*writer, offset, bytes) : VFS::VFSResult<void>::Ok(); }, &pending.m_digest);
        VFS::VFSResult<void> flush = VFS::VFSResult<void>::Ok();
        VFS::VFSResult<void> close = VFS::VFSResult<void>::Ok();
        if (writer)
        {
            if (copy.Succeeded())
                flush = writer->Flush();
            close = m_scratch->CloseChecked(writer);
        }
        if (copy.Failed())
            return copy;
        if (flush.Failed())
            return flush;
        if (close.Failed())
            return close;
        *output = std::move(pending);
        return VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Cook
