#include <ZEngine/Cook/CookFrozenFileSet.h>
#include <ZEngine/Cook/CookSort.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <algorithm>
#include <new>
#include <type_traits>
#include <utility>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        VFS::VFSResult<VFS::VFSRelativePath> LogicalEntry(const CookFrozenLibraryReadLease& read, uint32_t root, const VFS::VFSRelativePath& path)
        {
            const auto key = VFS::VFSRelativePath::Parse(read.RootKey(root));
            return key.Succeeded() ? key.Value().Append(path) : VFS::VFSResult<VFS::VFSRelativePath>::Fail(key.Error());
        }

        bool SameFile(const CookFrozenFileRecord& expected, const VFS::VFSFileIdentity& identity, const VFS::VFSFileStat& stat)
        {
            return expected.Identity == identity && expected.Stat.SizeBytes == stat.SizeBytes && expected.Stat.IsDirectory == stat.IsDirectory && expected.Stat.MTimeNs == stat.MTimeNs;
        }

        // Keep cleanup independent of the success path and check close before
        // publishing a capture/verification result. Never retain a source FD per
        // receipt; the owner root has a bounded reusable wrapper pool.
        struct FrozenSourceFile
        {
            const CookFrozenLibraryReadLease* Lease              = nullptr;
            VFS::IVFSFile*                    File               = nullptr;
            uint32_t                          Root               = 0;
            FrozenSourceFile()                                   = default;
            FrozenSourceFile(const FrozenSourceFile&)            = delete;
            FrozenSourceFile& operator=(const FrozenSourceFile&) = delete;
            ~FrozenSourceFile()
            {
                (void) Close();
            }
            VFS::VFSResult<void> Close()
            {
                return File ? Lease->CloseChecked(Root, std::exchange(File, nullptr)) : VFS::VFSResult<void>::Ok();
            }
        };

        VFS::VFSResult<void> OpenFrozenSource(const CookFrozenLibraryReadLease& read, const CookFrozenFileRecord& record, FrozenSourceFile& source)
        {
            const auto opened = read.Open(record.Path, &source.Root);
            if (opened.Failed())
                return VFS::VFSResult<void>::Fail(opened.Error() == VFS::VFSError::NotFound || opened.Error() == VFS::VFSError::NotADirectory ? VFS::VFSError::Corrupted : opened.Error());
            source.File         = opened.Value();
            source.Lease        = &read;
            const auto identity = source.File->Identity();
            if (identity.Failed())
                return VFS::VFSResult<void>::Fail(identity.Error());
            const auto stat = source.File->Stat();
            if (stat.Failed())
                return VFS::VFSResult<void>::Fail(stat.Error());
            return source.Root == record.RootIndex && SameFile(record, identity.Value(), stat.Value()) ? VFS::VFSResult<void>::Ok() : VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }

        template <typename Visitor>
        VFS::VFSResult<void> VisitRecordDirectory(const CookFrozenLibraryReadLease& read, const CookFrozenFileRecord& record, uint64_t maximum_entries, Core::Containers::ArrayView<uint8_t> buffer, Visitor&& visitor)
        {
            VFS::VFSRelativePath relative = {};
            const bool           root     = record.Path.ComponentCount() == 1;
            if (!root)
            {
                const auto resolved = read.Resolve(record.Path, &relative);
                if (resolved.Failed())
                    return VFS::VFSResult<void>::Fail(resolved.Error());
                if (resolved.Value() != record.RootIndex)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            }
            using Callback = std::remove_reference_t<Visitor>;
            return read.Visit(record.RootIndex, root ? nullptr : &relative, maximum_entries, buffer, [](void* context, const VFS::VFSRootEntry& entry) { return (*static_cast<Callback*>(context))(entry); }, &visitor);
        }
    } // namespace

    CookFrozenFileSet::~CookFrozenFileSet()
    {
        Reset();
    }

    void CookFrozenFileSet::Reset() noexcept
    {
        if (m_records)
            m_allocator->Free(m_records);
        m_allocator = nullptr;
        m_records   = nullptr;
        m_count = m_capacity = m_limit = 0;
        m_root_count                   = 0;
        m_generation                   = 0;
    }

    VFS::VFSResult<void> CookFrozenFileSet::Append(const CookFrozenFileRecord& record)
    {
        static_assert(std::is_trivially_copyable_v<CookFrozenFileRecord>);
        if (m_count == m_limit)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        if (m_count == m_capacity)
        {
            const size_t capacity  = std::min(m_limit, m_capacity ? m_capacity + std::min(m_capacity, m_limit - m_capacity) : size_t(16));
            const auto   allocated = m_allocator->Allocate(capacity * sizeof(CookFrozenFileRecord), alignof(CookFrozenFileRecord), CookMemoryCategory::CatalogGraph);
            if (allocated.Failed())
                return VFS::VFSResult<void>::Fail(allocated.Error());
            if (!allocated.Value())
                return VFS::VFSResult<void>::Fail(VFS::VFSError::OutOfMemory);
            if (reinterpret_cast<uintptr_t>(allocated.Value()) % alignof(CookFrozenFileRecord))
            {
                m_allocator->Free(allocated.Value());
                return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
            }
            auto* records = static_cast<CookFrozenFileRecord*>(allocated.Value());
            if (m_count)
                Helpers::secure_memcpy(records, capacity * sizeof(CookFrozenFileRecord), m_records, m_count * sizeof(CookFrozenFileRecord));
            if (m_records)
                m_allocator->Free(m_records);
            m_records  = records;
            m_capacity = capacity;
        }
        new (&m_records[m_count++]) CookFrozenFileRecord(record);
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookFrozenFileSet::Build(ICookAllocator& allocator, const CookFrozenLibraryReadLease& read, size_t maximum_records, Core::Containers::ArrayView<uint8_t> buffer)
    {
        if (m_allocator || !read.IsValid() || !buffer.data() || buffer.size() < ZKilo(4) || buffer.size() > ZKilo(64))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        if (maximum_records < read.RootCount() || maximum_records > SIZE_MAX / sizeof(CookFrozenFileRecord))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        CookMemoryCharge fixed   = {};
        const auto       charged = allocator.TryCharge(sizeof(Detail::CookSortState<CookFrozenFileRecord>) + sizeof(fixed), CookMemoryCategory::CatalogGraph, &fixed);
        if (charged.Failed())
            return charged;
        Detail::CookSortState<CookFrozenFileRecord> sort    = {};
        CookFrozenFileSet                           pending = {};
        pending.m_allocator                                 = &allocator;
        pending.m_limit                                     = maximum_records;
        pending.m_generation                                = read.Generation();
        pending.m_root_count                                = read.RootCount();
        for (uint32_t i = 0; i < read.RootCount(); ++i)
        {
            const auto path     = VFS::VFSRelativePath::Parse(read.RootKey(i));
            const auto identity = read.RootIdentity(i);
            if (path.Failed() || identity.Failed())
                return VFS::VFSResult<void>::Fail(path.Failed() ? path.Error() : identity.Error());
            const auto appended = pending.Append({.Path = path.Value(), .Identity = identity.Value(), .Stat = {.IsDirectory = true}, .RootIndex = i});
            if (appended.Failed())
                return appended;
        }
        // Copy the current record before callbacks can grow/move the array.
        // Already-discovered directory records are the iterative work queue.
        for (size_t i = 0; i < pending.m_count; ++i)
        {
            const auto directory = pending.m_records[i];
            if (!directory.Stat.IsDirectory)
                continue;
            const auto visited = VisitRecordDirectory(read, directory, maximum_records - pending.m_count, buffer, [&](const VFS::VFSRootEntry& entry) {
                const auto logical = LogicalEntry(read, directory.RootIndex, entry.Path);
                if (logical.Failed())
                    return VFS::VFSResult<void>::Fail(logical.Error());
                if (!entry.Identity.IsValid())
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
                return pending.Append({.Path = logical.Value(), .Identity = entry.Identity, .Stat = entry.Stat, .RootIndex = directory.RootIndex});
            });
            if (visited.Failed())
                return visited;
        }
        // Reject multiple logical names for one physical file (e.g. hardlinks),
        // including aliases across declared roots. Reuse the discovery storage
        // before publishing stationary path-sorted records; no identity table.
        Detail::HeapSort<CookFrozenFileRecord>({pending.m_records, pending.m_count}, sort, [](const CookFrozenFileRecord& a, const CookFrozenFileRecord& b) {
            if (a.Identity.Volume != b.Identity.Volume)
                return a.Identity.Volume < b.Identity.Volume;
            if (a.Identity.ObjectHigh != b.Identity.ObjectHigh)
                return a.Identity.ObjectHigh < b.Identity.ObjectHigh;
            return a.Identity.ObjectLow < b.Identity.ObjectLow;
        });
        for (size_t i = 1; i < pending.m_count; ++i)
            if (pending.m_records[i - 1].Identity == pending.m_records[i].Identity)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::AlreadyExists);
        Detail::HeapSort<CookFrozenFileRecord>({pending.m_records, pending.m_count}, sort, [](const CookFrozenFileRecord& a, const CookFrozenFileRecord& b) { return a.Path < b.Path; });
        for (size_t i = 1; i < pending.m_count; ++i)
            if (pending.m_records[i - 1].Path == pending.m_records[i].Path)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        m_allocator  = std::exchange(pending.m_allocator, nullptr);
        m_records    = std::exchange(pending.m_records, nullptr);
        m_count      = std::exchange(pending.m_count, 0);
        m_capacity   = std::exchange(pending.m_capacity, 0);
        m_limit      = pending.m_limit;
        m_generation = pending.m_generation;
        m_root_count = pending.m_root_count;
        return VFS::VFSResult<void>::Ok();
    }

    const CookFrozenFileRecord* CookFrozenFileSet::Find(const VFS::VFSRelativePath& path) const noexcept
    {
        if (!path.IsValid())
            return nullptr;
        size_t begin = 0, end = m_count;
        while (begin < end)
        {
            const auto middle = begin + (end - begin) / 2;
            if (m_records[middle].Path < path)
                begin = middle + 1;
            else
                end = middle;
        }
        return begin < m_count && m_records[begin].Path == path ? &m_records[begin] : nullptr;
    }

    VFS::VFSResult<void> CookFrozenFileSet::Capture(const CookFrozenLibraryReadLease& read, CookCaptureStore& store, const VFS::VFSRelativePath& path, uint64_t maximum_bytes, CookCapturedFile* output)
    {
        if (!output || !m_allocator || !read.IsValid() || read.Generation() != m_generation || read.RootCount() != m_root_count || !IsCookLogicalPath(path))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        const auto* found = Find(path);
        if (!found)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::NotFound);
        if (found->Stat.IsDirectory)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::NotAFile);
        auto&            record = m_records[static_cast<size_t>(found - m_records)];
        FrozenSourceFile source = {};
        const auto       opened = OpenFrozenSource(read, record, source);
        if (opened.Failed())
            return opened;
        CookCapturedFile pending  = {};
        const auto       captured = store.CaptureDisk(*source.File, maximum_bytes, &pending);
        const auto       closed   = source.Close();
        if (captured.Failed() || closed.Failed())
            return captured.Failed() ? captured : closed;
        const auto current = read.Identity(path);
        if (current.Failed())
            return VFS::VFSResult<void>::Fail(current.Error() == VFS::VFSError::NotFound ? VFS::VFSError::Corrupted : current.Error());
        if (current.Value() != record.Identity || pending.Size() != record.Stat.SizeBytes || (record.Captured && pending.Digest() != record.CaptureDigest))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        record.CaptureDigest = pending.Digest();
        record.Captured      = true;
        *output              = std::move(pending);
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookFrozenFileSet::Verify(const CookFrozenLibraryReadLease& read, CookCaptureStore& store, Core::Containers::ArrayView<uint8_t> buffer, VFS::VFSRelativePath* failure_source) const
    {
        VFS::VFSRelativePath source_path = {};
        const auto           verify      = [&]() -> VFS::VFSResult<void> {
            if (!m_allocator || !read.IsValid() || !buffer.data() || buffer.size() < ZKilo(4) || buffer.size() > ZKilo(64))
                return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
            if (read.Generation() != m_generation || read.RootCount() != m_root_count)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            size_t seen = m_root_count;
            for (size_t i = 0; i < m_count; ++i)
            {
                const auto& record = m_records[i];
                if (!record.Stat.IsDirectory)
                    continue;
                source_path = record.Path;
                if (record.Path.ComponentCount() == 1)
                {
                    const auto identity = read.RootIdentity(record.RootIndex);
                    if (identity.Failed())
                        return VFS::VFSResult<void>::Fail(identity.Error());
                    if (record.Path.View() != read.RootKey(record.RootIndex) || identity.Value() != record.Identity)
                        return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                }
                const auto visited = VisitRecordDirectory(read, record, m_count - m_root_count, buffer, [&](const VFS::VFSRootEntry& entry) {
                    const auto logical = LogicalEntry(read, record.RootIndex, entry.Path);
                    if (logical.Failed())
                        return VFS::VFSResult<void>::Fail(logical.Error());
                    source_path          = logical.Value();
                    const auto* expected = Find(logical.Value());
                    if (!expected || !SameFile(*expected, entry.Identity, entry.Stat) || seen == m_count)
                        return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                    ++seen;
                    return VFS::VFSResult<void>::Ok();
                });
                if (visited.Failed())
                    return VFS::VFSResult<void>::Fail(visited.Error() == VFS::VFSError::NotFound || visited.Error() == VFS::VFSError::NotADirectory || visited.Error() == VFS::VFSError::SizeLimitExceeded ? VFS::VFSError::Corrupted : visited.Error());
                source_path = record.Path;
            }
            if (seen != m_count)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            for (const auto& record : Records())
            {
                if (!record.Captured)
                    continue;
                source_path             = record.Path;
                FrozenSourceFile source = {};
                const auto       opened = OpenFrozenSource(read, record, source);
                if (opened.Failed())
                    return opened;
                const auto verified = store.VerifySource(*source.File, record.Stat.SizeBytes, record.CaptureDigest);
                const auto closed   = source.Close();
                if (verified.Failed() || closed.Failed())
                    return verified.Failed() ? verified : closed;
                const auto current = read.Identity(record.Path);
                if (current.Failed())
                    return VFS::VFSResult<void>::Fail(current.Error() == VFS::VFSError::NotFound ? VFS::VFSError::Corrupted : current.Error());
                if (current.Value() != record.Identity)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            }
            return VFS::VFSResult<void>::Ok();
        };
        const auto result = verify();
        if (result.Failed() && failure_source)
            *failure_source = source_path;
        return result;
    }
} // namespace ZEngine::Cook
