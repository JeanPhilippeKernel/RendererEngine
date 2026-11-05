#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/NativePages.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>
#include <new>

namespace ZEngine::Core::VFS
{
    struct VFSMemoryRoot;

    // One synchronization/storage domain shared by the facade and all rooted
    // operations. Borrowed mode preserves the existing caller-lifetime contract;
    // only owned mode can survive that facade and expose rooted capabilities.
    struct VFSMemoryStorage final : IVFSBackend, IVFSRootProvider, Memory::NativePageObject<VFSMemoryStorage>
    {
        ~VFSMemoryStorage() override;
        VFSResult<void>                           Initialize(Memory::ArenaAllocator*);
        VFSResult<void>                           InitializeOwned(size_t bytes, uint32_t max_nodes);
        VFSResult<IVFSFile*>                      Open(const VFSPath&, VFSOpenFlags) override;
        void                                      Close(IVFSFile*) override;
        VFSResult<VFSFileStat>                    Stat(const VFSPath&) const override;
        bool                                      Exists(const VFSPath&) const override;
        VFSResult<Containers::Array<VFSDirEntry>> List(Memory::ArenaAllocator*, const VFSPath&) const override;
        VFSResult<void>                           CreateDir(const VFSPath&) override;
        VFSResult<void>                           CreateDirExclusive(const VFSPath&) override;
        VFSResult<void>                           Remove(const VFSPath&) override;
        VFSResult<void>                           Rename(const VFSPath&, const VFSPath&) override;
        VFSResult<void>                           RenameNoReplace(const VFSPath&, const VFSPath&) override;
        VFSResult<void>                           WriteFile(const VFSPath&, Containers::ArrayView<const uint8_t>);
        cstring                                   BackendType() const override
        {
            return "memory";
        }
        VFSBackendCaps Capabilities() const override
        {
            return VFSBackendCaps::Read | VFSBackendCaps::Write | VFSBackendCaps::List | VFSBackendCaps::MemoryMap;
        }
        bool OwnsStorage() const
        {
            return m_owned;
        }
        VFSResult<VFSRootMemoryRequirements> MemoryRequirements() const override;

    private:
        friend struct VFSMemoryRoot;
        VFSResult<Helpers::Scope<IVFSRootOperations>>   AcquireRoot(const VFSPath&, VFSRootAccess) override;
        MemNode*                                        FindNode(const VFSPath&);
        const MemNode*                                  FindNode(const VFSPath&) const;
        MemNode*                                        CreateNode(const VFSPath&, MemNode::Kind);
        void                                            DestroyNode(MemNode*);
        VFSResult<void>                                 EnsureParentExists(const VFSPath&);
        VFSResult<void>                                 ValidateParent(const VFSPath&) const;
        bool                                            HasChildren(const VFSPath&) const;
        bool                                            ContainsPinnedRoot(const VFSPath&) const;
        void                                            CompactIndexIfNeeded();
        void                                            AdvanceNamespace();
        VFSFileIdentity                                 NodeIdentity(const MemNode*) const;
        VFSResult<void>                                 RenameImpl(const VFSPath&, const VFSPath&, bool replace);
        VFSMemoryFile*                                  AllocFile();
        void                                            FreeFile(IVFSFile*);
        void                                            DiscardFile(IVFSFile*);

        Memory::ArenaAllocator                          m_owned_arena = {};
        Memory::TLSFSlab                                m_buffers     = {};
        Containers::UnorderedHashMap<cstring, MemNode*> m_nodes       = {};
        mutable std::shared_mutex                       m_mutex       = {};
        Memory::ArenaAllocator*                         m_arena       = nullptr;
        Memory::PoolAllocator                           m_file_pool = {}, m_node_pool = {};
        std::mutex                                      m_file_pool_mutex    = {};
        MemNode**                                       m_rename_scratch     = nullptr;
        Containers::FixedArray<MemNode*, 64>            m_pinned_roots       = {};
        size_t                                          m_erased_names       = 0;
        size_t                                          m_max_nodes          = SIZE_MAX;
        bool                                            m_owned              = false;
        uint64_t                                        m_next_identity      = 1;
        uint64_t                                        m_namespace_revision = 0;
    };

    struct VFSMemoryRoot final : IVFSRootOperations, Memory::NativePageObject<VFSMemoryRoot>
    {
        Helpers::Ref<VFSMemoryStorage>        Storage   = {};
        VFSPath                               Root      = {};
        MemNode*                              Anchor    = nullptr;
        Containers::FixedArray<IVFSFile*, 32> Files     = {};
        std::mutex                            FileMutex = {};
        VFSRootAccess                         Access    = VFSRootAccess::ReadOnly;

        ~VFSMemoryRoot() override;
        VFSPublicationCapabilities Capabilities() const override
        {
            return {.BackendIdentity = Storage->BackendIdentity(), .Guarantees = Access == VFSRootAccess::ReadOnly ? VFS_READ_GUARANTEES : VFS_PUBLICATION_GUARANTEES | VFS_READ_GUARANTEES};
        }
        VFSResult<VFSRootMemoryRequirements> MemoryRequirements() const override
        {
            return Storage->MemoryRequirements();
        }
        VFSResult<IVFSFile*>                          Open(const VFSRelativePath&, VFSOpenFlags) override;
        VFSResult<VFSFileStat>                        Stat(const VFSRelativePath&) const override;
        VFSResult<void>                               CreateDir(const VFSRelativePath&, bool exclusive) override;
        VFSResult<void>                               Remove(const VFSRelativePath&) override;
        VFSResult<void>                               Rename(const VFSRelativePath&, const VFSRelativePath&, bool replace) override;
        VFSResult<void>                               CloseChecked(IVFSFile*) override;
        VFSResult<VFSPath>                            Resolve(const VFSRelativePath&) const;
        VFSResult<VFSFileIdentity>                    RootIdentity() const override;
        VFSResult<VFSFileIdentity>                    Identity(const VFSRelativePath&) const override;
        VFSResult<void>                               Visit(const VFSRelativePath*, uint64_t, Containers::ArrayView<uint8_t>, VFSRootVisitorFn, void*) const override;
        VFSResult<bool>                               Overlaps(const IVFSRootOperations&) const override;
        VFSResult<Helpers::Scope<IVFSRootOperations>> AcquireChild(const VFSRelativePath&) override;
    };

    namespace
    {
        size_t ByteBufferCapacity(size_t current, size_t required)
        {
            return std::max(required, current <= std::numeric_limits<size_t>::max() / 2 ? current * 2 : required);
        }
    } // namespace

    bool VFSMemoryFile::IsWriteMode() const
    {
        return HasFlag(m_flags, VFSOpenFlags::Write) || HasFlag(m_flags, VFSOpenFlags::Append);
    }

    VFSResult<size_t> VFSMemoryFile::Read(Core::Containers::ArrayView<uint8_t> buffer, uint64_t offset)
    {
        if ((!m_node && !m_snapshot) || m_closed || (buffer.size() && !buffer.data()))
        {
            return VFSResult<size_t>::Fail(VFSError::IOError);
        }

        const auto& data = m_snapshot || IsWriteMode() ? m_write_buf : m_node->Data;
        if (offset >= data.size())
        {
            return VFSResult<size_t>::Ok(0);
        }

        const size_t available = data.size() - static_cast<size_t>(offset);
        const size_t n         = buffer.size() < available ? buffer.size() : available;
        if (n > 0)
        {
            Helpers::secure_memcpy(buffer.data(), buffer.size(), data.data() + offset, n);
        }
        return VFSResult<size_t>::Ok(n);
    }

    VFSResult<size_t> VFSMemoryFile::Write(Core::Containers::ArrayView<const uint8_t> buffer, uint64_t offset)
    {
        if (!IsWriteMode() || m_closed)
        {
            return VFSResult<size_t>::Fail(VFSError::Unsupported);
        }
        if (!buffer.size())
            return VFSResult<size_t>::Ok(0);
        if (!buffer.data())
            return VFSResult<size_t>::Fail(VFSError::IOError);

        // Validate before narrowing or pointer arithmetic; a wrapped end would
        // bypass reserve and allow a copy outside the write buffer.
        if (offset > std::numeric_limits<size_t>::max() - buffer.size())
            return VFSResult<size_t>::Fail(VFSError::SizeLimitExceeded);
        const size_t end = static_cast<size_t>(offset) + buffer.size();
        if (m_write_buf.capacity() < end)
        {
            // Geometric growth avoids quadratic copies and abandoned arena
            // blocks when other allocations prevent extending this one in place.
            if (!m_write_buf.try_reserve(ByteBufferCapacity(m_write_buf.capacity(), end)))
                return VFSResult<size_t>::Fail(VFSError::OutOfMemory);
        }
        const size_t begin = static_cast<size_t>(offset);
        // Only a sparse gap needs zeroing; the payload is overwritten below.
        if (begin > m_write_buf.size())
            Helpers::secure_memset(m_write_buf.data() + m_write_buf.size(), 0, begin - m_write_buf.size(), m_write_buf.capacity() - m_write_buf.size());
        Helpers::secure_memcpy(m_write_buf.data() + begin, m_write_buf.capacity() - begin, buffer.data(), buffer.size());
        m_write_buf.m_size = std::max(m_write_buf.size(), end);
        m_dirty            = true;
        return VFSResult<size_t>::Ok(buffer.size());
    }

    VFSResult<uint64_t> VFSMemoryFile::Size() const
    {
        if (m_closed || (!m_node && !m_snapshot))
        {
            return VFSResult<uint64_t>::Fail(VFSError::IOError);
        }
        const uint64_t sz = m_snapshot || IsWriteMode() ? m_write_buf.size() : m_node->Data.size();
        return VFSResult<uint64_t>::Ok(sz);
    }

    VFSResult<VFSFileStat> VFSMemoryFile::Stat() const
    {
        if (m_closed || (!m_node && !m_snapshot))
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::IOError);
        }
        VFSFileStat stat = {};
        stat.IsDirectory = false;
        stat.SizeBytes   = m_snapshot || IsWriteMode() ? m_write_buf.size() : m_node->Data.size();
        stat.IsReadOnly  = false;
        return VFSResult<VFSFileStat>::Ok(stat);
    }

    VFSResult<void> VFSMemoryFile::Commit()
    {
        if (!IsWriteMode() || !m_node || !m_backend_mutex || !m_dirty)
        {
            return VFSResult<void>::Ok();
        }

        std::unique_lock<std::shared_mutex> lock(*m_backend_mutex);
        const size_t                        n = m_write_buf.size();
        // Retain the committed capacity. Reinitializing this arena-backed array
        // on every Flush (and again on Close) permanently abandoned each copy.
        if (m_node->Data.capacity() < n && !m_node->Data.try_reserve(ByteBufferCapacity(m_node->Data.capacity(), n)))
            return VFSResult<void>::Fail(VFSError::OutOfMemory);
        if (n > 0)
        {
            Helpers::secure_memcpy(m_node->Data.data(), n, m_write_buf.data(), n);
        }
        m_node->Data.m_size = n;
        m_dirty             = false;
        return VFSResult<void>::Ok();
    }

    VFSResult<VFSFileIdentity> VFSMemoryFile::Identity() const
    {
        return !m_closed && m_identity.IsValid() ? VFSResult<VFSFileIdentity>::Ok(m_identity) : VFSResult<VFSFileIdentity>::Fail(VFSError::IOError);
    }

    VFSResult<void> VFSMemoryFile::Flush()
    {
        if (m_closed)
        {
            return VFSResult<void>::Ok();
        }
        return Commit();
    }

    const VFSPath& VFSMemoryFile::Path() const
    {
        return m_path;
    }

    VFSResult<void> VFSMemoryFile::Close()
    {
        if (m_closed)
        {
            return VFSResult<void>::Ok();
        }

        VFSResult<void> result = VFSResult<void>::Ok();
        if (IsWriteMode())
        {
            result = Commit();
            std::unique_lock lock(*m_backend_mutex);
            --m_node->WriterCount;
        }
        m_closed = true;
        if (m_read_lock.owns_lock())
        {
            m_read_lock.unlock();
        }
        return result;
    }

    VFSResult<void*> VFSMemoryFile::MemoryMap(uint64_t& out_size)
    {
        if (IsWriteMode())
        {
            return VFSResult<void*>::Fail(VFSError::Unsupported);
        }
        if (m_closed || (!m_node && !m_snapshot))
        {
            return VFSResult<void*>::Fail(VFSError::IOError);
        }
        auto& data = m_snapshot ? m_write_buf : m_node->Data;
        out_size   = data.size();
        return VFSResult<void*>::Ok(static_cast<void*>(data.data()));
    }

    VFSMemoryFile::~VFSMemoryFile()
    {
        (void) Close();
    }

    VFSMemoryStorage::~VFSMemoryStorage()
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        for (const auto& kv : m_nodes)
        {
            kv.second->~MemNode();
        }
        m_nodes.clear();
        m_buffers.Shutdown();
    }

    VFSResult<void> VFSMemoryStorage::Initialize(Memory::ArenaAllocator* arena)
    {
        ZENGINE_VALIDATE_ASSERT(arena != nullptr, "VFSMemoryBackend requires a valid arena")
        m_arena = arena;
        if (!m_nodes.try_init(arena, 64))
            return VFSResult<void>::Fail(VFSError::OutOfMemory);
        const size_t file_stride = Helpers::memory_align_size_t(sizeof(VFSMemoryFile), ZAlignof<VFSMemoryFile>());
        if (!m_file_pool.TryInitialize(arena, file_stride * kVFSMaxOpenMemoryFiles, file_stride, ZAlignof<VFSMemoryFile>()))
            return VFSResult<void>::Fail(VFSError::OutOfMemory);
        return VFSResult<void>::Ok();
    }

    VFSResult<void> VFSMemoryStorage::InitializeOwned(size_t bytes, uint32_t max_nodes)
    {
        const size_t node_count = max_nodes;
        if (!bytes || !max_nodes || node_count > SIZE_MAX / (2 * sizeof(Containers::HashEntry<cstring, MemNode*>)))
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        m_owned     = true;
        m_max_nodes = max_nodes;
        m_owned_arena.Initialize(bytes, 0, "OwnedVFSMemoryBackend");
        if (!m_owned_arena.m_memory)
            return VFSResult<void>::Fail(VFSError::OutOfMemory);
        m_arena = &m_owned_arena;
        if (!m_nodes.try_init(m_arena, static_cast<size_t>(max_nodes) * 2))
            return VFSResult<void>::Fail(VFSError::OutOfMemory);
        const size_t file_stride = Helpers::memory_align_size_t(sizeof(VFSMemoryFile), alignof(VFSMemoryFile));
        const size_t node_stride = Helpers::memory_align_size_t(sizeof(MemNode), alignof(MemNode));
        if (max_nodes > SIZE_MAX / node_stride || !m_file_pool.TryInitialize(m_arena, file_stride * kVFSMaxOpenMemoryFiles, file_stride, alignof(VFSMemoryFile)) || !m_node_pool.TryInitialize(m_arena, node_stride * max_nodes, node_stride, alignof(MemNode)))
            return VFSResult<void>::Fail(VFSError::OutOfMemory);
        m_rename_scratch = static_cast<MemNode**>(m_arena->Allocate(static_cast<size_t>(max_nodes) * sizeof(MemNode*), alignof(MemNode*)));
        if (!m_rename_scratch)
            return VFSResult<void>::Fail(VFSError::OutOfMemory);
        // Account for alignment before assigning the remaining bounded storage.
        const size_t cursor = Helpers::memory_align_size_t(m_arena->m_current_offset, DEFAULT_ALIGNMENT);
        if (cursor > bytes || !m_buffers.TryInit(m_arena, bytes - cursor))
            return VFSResult<void>::Fail(VFSError::OutOfMemory);
        return VFSResult<void>::Ok();
    }

    MemNode* VFSMemoryStorage::FindNode(const VFSPath& path)
    {
        MemNode** slot = m_nodes.find(path.CStr());
        return slot ? *slot : nullptr;
    }

    const MemNode* VFSMemoryStorage::FindNode(const VFSPath& path) const
    {
        const MemNode* const* slot = m_nodes.find(path.CStr());
        return slot ? *slot : nullptr;
    }

    MemNode* VFSMemoryStorage::CreateNode(const VFSPath& path, MemNode::Kind kind)
    {
        if (m_nodes.size() == m_max_nodes || m_next_identity == UINT64_MAX)
            return nullptr;
        AdvanceNamespace();
        if (m_nodes.size() + 1 > m_nodes.capacity() - m_nodes.capacity() / 4 && !m_nodes.try_reserve(m_nodes.capacity() * 2))
            return nullptr;
        void* storage = m_owned ? (m_node_pool.head ? m_node_pool.Allocate() : nullptr) : m_arena->Allocate(sizeof(MemNode), alignof(MemNode));
        if (!storage)
            return nullptr;
        auto* node             = new (storage) MemNode{};
        node->NodeKind         = kind;
        node->Path             = path;
        node->Identity         = m_next_identity++;
        node->Data.m_allocator = m_owned ? nullptr : m_arena;
        node->Data.m_slab      = m_owned ? &m_buffers : nullptr;
        m_nodes.insert(node->Path.CStr(), node);
        return node;
    }

    void VFSMemoryStorage::DestroyNode(MemNode* node)
    {
        AdvanceNamespace();
        node->~MemNode();
        if (m_owned)
            m_node_pool.Free(node);
    }

    void VFSMemoryStorage::AdvanceNamespace()
    {
        // Saturation makes future visits fail closed instead of allowing an ABA
        // after a generation wrap. All namespace changes hold m_mutex exclusively.
        if (m_namespace_revision != UINT64_MAX)
            ++m_namespace_revision;
    }

    VFSFileIdentity VFSMemoryStorage::NodeIdentity(const MemNode* node) const
    {
        // Root is implicit (object zero). The high word separates synthetic
        // identities from native device/inode identities; pool reuse never
        // reuses a live or previously issued node identity.
        return {.Volume = BackendIdentity(), .ObjectLow = node ? node->Identity : 0, .ObjectHigh = UINT64_MAX};
    }

    VFSMemoryFile* VFSMemoryStorage::AllocFile()
    {
        std::lock_guard<std::mutex> pool_lock(m_file_pool_mutex);
        if (!m_file_pool.head)
            return nullptr;
        void* mem = m_file_pool.Allocate();
        if (!mem)
        {
            return nullptr;
        }
        return ZConstruct<VFSMemoryFile>(mem);
    }

    void VFSMemoryStorage::FreeFile(IVFSFile* file)
    {
        std::lock_guard<std::mutex> pool_lock(m_file_pool_mutex);
        m_file_pool.Free(file);
    }

    VFSResult<void> VFSMemoryStorage::EnsureParentExists(const VFSPath& path)
    {
        // Validate existing ancestors and node capacity before publishing parents.
        size_t missing = 0;
        for (auto parent = path.Parent(); parent.IsValid() && !parent.IsRoot(); parent = parent.Parent())
        {
            const auto* node = FindNode(parent);
            if (node && node->NodeKind != MemNode::Kind::Directory)
                return VFSResult<void>::Fail(VFSError::NotADirectory);
            if (!node)
                ++missing;
        }
        if (missing >= m_max_nodes - m_nodes.size())
            return VFSResult<void>::Fail(VFSError::OutOfMemory);
        VFSPath parent = path.Parent();
        while (parent.IsValid() && !parent.IsRoot())
        {
            if (FindNode(parent))
            {
                break;
            }
            if (!CreateNode(parent, MemNode::Kind::Directory))
                return VFSResult<void>::Fail(VFSError::OutOfMemory);
            parent = parent.Parent();
        }
        return VFSResult<void>::Ok();
    }

    VFSResult<void> VFSMemoryStorage::ValidateParent(const VFSPath& path) const
    {
        const auto parent = path.Parent();
        if (parent.IsRoot())
            return VFSResult<void>::Ok();
        const auto* node = FindNode(parent);
        if (!node)
            return VFSResult<void>::Fail(VFSError::NotFound);
        return node->NodeKind == MemNode::Kind::Directory ? VFSResult<void>::Ok() : VFSResult<void>::Fail(VFSError::NotADirectory);
    }

    bool VFSMemoryStorage::ContainsPinnedRoot(const VFSPath& path) const
    {
        for (const auto* node : m_pinned_roots)
            if (node && path.IsPrefixOf(node->Path))
                return true;
        return false;
    }

    void VFSMemoryStorage::CompactIndexIfNeeded()
    {
        if (!m_owned || m_erased_names < m_nodes.capacity() / 4)
            return;
        // Publication creates a fresh temporary name each time. Rebuild in the
        // existing index before tombstones turn missing-name lookups into full
        // table scans. Reuse directory-move scratch only after a mutation ends.
        size_t count = 0;
        for (const auto& entry : m_nodes)
            m_rename_scratch[count++] = entry.second;
        m_nodes.clear();
        for (size_t i = 0; i < count; ++i)
        {
            auto* node = m_rename_scratch[i];
            m_nodes.insert(node->Path.CStr(), node);
        }
        m_erased_names = 0;
    }

    bool VFSMemoryStorage::HasChildren(const VFSPath& dir) const
    {
        for (const auto& kv : m_nodes)
        {
            if (kv.second->Path.Parent() == dir)
            {
                return true;
            }
        }
        return false;
    }

    VFSResult<IVFSFile*> VFSMemoryStorage::Open(const VFSPath& relative_path, VFSOpenFlags flags)
    {
        const bool snapshot = HasFlag(flags, VFSOpenFlags::AllowReplace);
        if (snapshot && (!m_owned || flags != (VFSOpenFlags::Read | VFSOpenFlags::AllowReplace)))
            return VFSResult<IVFSFile*>::Fail(VFSError::Unsupported);
        constexpr auto known       = VFSOpenFlags::Read | VFSOpenFlags::Write | VFSOpenFlags::Create | VFSOpenFlags::Truncate | VFSOpenFlags::Append | VFSOpenFlags::Exclusive | VFSOpenFlags::AllowReplace;
        const bool     wants_write = HasFlag(flags, VFSOpenFlags::Write) || HasFlag(flags, VFSOpenFlags::Append);
        if ((static_cast<uint32_t>(flags) & ~static_cast<uint32_t>(known)) || (!wants_write && !HasFlag(flags, VFSOpenFlags::Read)) || (!wants_write && (HasFlag(flags, VFSOpenFlags::Create) || HasFlag(flags, VFSOpenFlags::Truncate))))
            return VFSResult<IVFSFile*>::Fail(VFSError::Unsupported);
        const bool exclusive = HasFlag(flags, VFSOpenFlags::Exclusive);
        if (exclusive && (!HasFlag(flags, VFSOpenFlags::Write) || !HasFlag(flags, VFSOpenFlags::Create)))
            return VFSResult<IVFSFile*>::Fail(VFSError::Unsupported);
        if (!relative_path.IsValid())
            return VFSResult<IVFSFile*>::Fail(VFSError::InvalidPath);
        if (relative_path.IsRoot())
            return VFSResult<IVFSFile*>::Fail(exclusive ? VFSError::AlreadyExists : VFSError::NotAFile);
        if (wants_write)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);

            MemNode*                            node = FindNode(relative_path);
            if (exclusive && node)
                return VFSResult<IVFSFile*>::Fail(VFSError::AlreadyExists);
            if (!node && !(m_owned ? HasFlag(flags, VFSOpenFlags::Create) : HasFlag(flags, VFSOpenFlags::Write)))
                return VFSResult<IVFSFile*>::Fail(VFSError::NotFound);
            if (node && node->NodeKind == MemNode::Kind::Directory)
            {
                return VFSResult<IVFSFile*>::Fail(VFSError::NotAFile);
            }
            if (m_owned)
            {
                const auto parent = ValidateParent(relative_path);
                if (parent.Failed())
                    return VFSResult<IVFSFile*>::Fail(parent.Error());
            }

            VFSMemoryFile* file = AllocFile();
            if (!file)
            {
                return VFSResult<IVFSFile*>::Fail(VFSError::OutOfMemory);
            }
            auto discard = [&] {
                file->m_closed = true;
                file->~VFSMemoryFile();
                FreeFile(file);
            };
            file->m_write_buf.m_allocator = m_owned ? nullptr : m_arena;
            file->m_write_buf.m_slab      = m_owned ? &m_buffers : nullptr;
            const bool   truncate         = HasFlag(flags, VFSOpenFlags::Truncate) || (!m_owned && !HasFlag(flags, VFSOpenFlags::Append));
            const size_t initial          = node && !truncate ? node->Data.size() : 0;
            if (!file->m_write_buf.try_reserve(std::max<size_t>(16, initial)))
            {
                discard();
                return VFSResult<IVFSFile*>::Fail(VFSError::OutOfMemory);
            }
            if (initial)
                Helpers::secure_memcpy(file->m_write_buf.data(), file->m_write_buf.capacity(), node->Data.data(), initial);
            file->m_write_buf.m_size = initial;
            // Reserve the handle/scratch before publishing an exclusive name.
            if (!node)
            {
                const auto parent = m_owned ? VFSResult<void>::Ok() : EnsureParentExists(relative_path);
                if (parent.Failed())
                {
                    discard();
                    return VFSResult<IVFSFile*>::Fail(parent.Error());
                }
                node = CreateNode(relative_path, MemNode::Kind::File);
                if (!node)
                {
                    discard();
                    return VFSResult<IVFSFile*>::Fail(VFSError::OutOfMemory);
                }
            }
            file->Owner           = this;
            file->m_node          = node;
            file->m_flags         = flags;
            file->m_arena         = m_arena;
            file->m_backend_mutex = &m_mutex;
            file->m_dirty         = truncate;
            file->m_path          = relative_path;
            file->m_identity      = NodeIdentity(node);
            ++node->WriterCount;
            return VFSResult<IVFSFile*>::Ok(file);
        }

        std::shared_lock<std::shared_mutex> lock(m_mutex);
        MemNode*                            node = FindNode(relative_path);
        if (!node)
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::NotFound);
        }
        if (node->NodeKind == MemNode::Kind::Directory)
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::NotAFile);
        }

        VFSMemoryFile* file = AllocFile();
        if (!file)
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::OutOfMemory);
        }
        file->Owner           = this;
        file->m_node          = node;
        file->m_flags         = flags;
        file->m_arena         = m_arena;
        file->m_backend_mutex = &m_mutex;
        file->m_path          = relative_path;
        file->m_identity      = NodeIdentity(node);
        if (snapshot)
        {
            // Read-only immutable capture, independent of the namespace and tree
            // lock. Its bytes are reclaimed on close from the same bounded slab.
            file->m_write_buf.m_slab = &m_buffers;
            if (!file->m_write_buf.try_reserve(node->Data.size()))
            {
                file->m_closed = true;
                file->~VFSMemoryFile();
                FreeFile(file);
                return VFSResult<IVFSFile*>::Fail(VFSError::OutOfMemory);
            }
            if (node->Data.size())
                Helpers::secure_memcpy(file->m_write_buf.data(), file->m_write_buf.capacity(), node->Data.data(), node->Data.size());
            file->m_write_buf.m_size = node->Data.size();
            file->m_snapshot         = true;
            file->m_node             = nullptr;
        }
        else
            file->m_read_lock = std::move(lock);
        return VFSResult<IVFSFile*>::Ok(file);
    }

    void VFSMemoryStorage::Close(IVFSFile* file)
    {
        if (!file)
        {
            return;
        }
        file->Close();
        file->~IVFSFile();
        FreeFile(file);
    }

    void VFSMemoryStorage::DiscardFile(IVFSFile* file)
    {
        auto* memory_file    = static_cast<VFSMemoryFile*>(file);
        memory_file->m_dirty = false;
        Close(file);
    }

    VFSResult<VFSFileStat> VFSMemoryStorage::Stat(const VFSPath& path) const
    {
        if (!path.IsValid())
            return VFSResult<VFSFileStat>::Fail(VFSError::InvalidPath);
        // The root is implicit, just as it is in List and EnsureParentExists.
        if (path.IsRoot())
        {
            VFSFileStat stat = {};
            stat.IsDirectory = true;
            return VFSResult<VFSFileStat>::Ok(stat);
        }
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        const MemNode*                      node = FindNode(path);
        if (!node)
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::NotFound);
        }
        VFSFileStat stat = {};
        stat.IsDirectory = node->NodeKind == MemNode::Kind::Directory;
        stat.SizeBytes   = stat.IsDirectory ? 0 : node->Data.size();
        stat.IsReadOnly  = false;
        return VFSResult<VFSFileStat>::Ok(stat);
    }

    bool VFSMemoryStorage::Exists(const VFSPath& path) const
    {
        if (!path.IsValid())
            return false;
        if (path.IsRoot())
            return true;
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return FindNode(path) != nullptr;
    }

    VFSResult<Core::Containers::Array<VFSDirEntry>> VFSMemoryStorage::List(Core::Memory::ArenaAllocator* arena, const VFSPath& dir) const
    {

        std::shared_lock<std::shared_mutex> lock(m_mutex);

        if (!dir.IsRoot())
        {
            const MemNode* dir_node = FindNode(dir);
            if (!dir_node)
            {
                return VFSResult<Core::Containers::Array<VFSDirEntry>>::Fail(VFSError::NotFound);
            }
            if (dir_node->NodeKind != MemNode::Kind::Directory)
            {
                return VFSResult<Core::Containers::Array<VFSDirEntry>>::Fail(VFSError::NotADirectory);
            }
        }

        Core::Containers::Array<VFSDirEntry> entries = {};
        entries.m_allocator                          = arena;

        for (const auto& kv : m_nodes)
        {
            const MemNode* node = kv.second;
            if (node->Path.Parent() == dir)
            {
                VFSDirEntry entry      = {};
                entry.Path             = node->Path;
                entry.IsDirectory      = node->NodeKind == MemNode::Kind::Directory;
                entry.Stat.IsDirectory = entry.IsDirectory;
                entry.Stat.SizeBytes   = entry.IsDirectory ? 0 : node->Data.size();
                if (!entries.try_push(entry))
                    return VFSResult<Containers::Array<VFSDirEntry>>::Fail(VFSError::OutOfMemory);
            }
        }
        return VFSResult<Core::Containers::Array<VFSDirEntry>>::Ok(std::move(entries));
    }

    VFSResult<void> VFSMemoryStorage::CreateDir(const VFSPath& relative_path)
    {
        if (!relative_path.IsValid())
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        if (relative_path.IsRoot())
            return VFSResult<void>::Fail(VFSError::AlreadyExists);
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (FindNode(relative_path))
        {
            return VFSResult<void>::Fail(VFSError::AlreadyExists);
        }
        const auto parent = EnsureParentExists(relative_path);
        if (parent.Failed())
            return parent;
        if (!CreateNode(relative_path, MemNode::Kind::Directory))
            return VFSResult<void>::Fail(VFSError::OutOfMemory);
        return VFSResult<void>::Ok();
    }

    VFSResult<void> VFSMemoryStorage::CreateDirExclusive(const VFSPath& relative_path)
    {
        if (!relative_path.IsValid())
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (relative_path.IsRoot() || FindNode(relative_path))
            return VFSResult<void>::Fail(VFSError::AlreadyExists);
        const auto  parent_path = relative_path.Parent();
        const auto* parent      = FindNode(parent_path);
        if (!parent && !parent_path.IsRoot())
            return VFSResult<void>::Fail(VFSError::NotFound);
        if (parent && parent->NodeKind != MemNode::Kind::Directory)
            return VFSResult<void>::Fail(VFSError::NotADirectory);
        if (!CreateNode(relative_path, MemNode::Kind::Directory))
            return VFSResult<void>::Fail(VFSError::OutOfMemory);
        return VFSResult<void>::Ok();
    }

    VFSResult<void> VFSMemoryStorage::Remove(const VFSPath& relative_path)
    {
        if (!relative_path.IsValid() || relative_path.IsRoot())
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        MemNode*                            node = FindNode(relative_path);
        if (!node)
        {
            return VFSResult<void>::Fail(VFSError::NotFound);
        }
        if (node->WriterCount || ContainsPinnedRoot(relative_path))
            return VFSResult<void>::Fail(VFSError::AlreadyExists);
        if (node->NodeKind == MemNode::Kind::Directory && HasChildren(relative_path))
        {
            return VFSResult<void>::Fail(VFSError::IOError);
        }
        m_nodes.remove(relative_path.CStr());
        ++m_erased_names;
        DestroyNode(node);
        CompactIndexIfNeeded();
        return VFSResult<void>::Ok();
    }

    VFSResult<void> VFSMemoryStorage::Rename(const VFSPath& rel_src, const VFSPath& rel_dst)
    {
        return RenameImpl(rel_src, rel_dst, true);
    }

    VFSResult<void> VFSMemoryStorage::RenameNoReplace(const VFSPath& rel_src, const VFSPath& rel_dst)
    {
        return RenameImpl(rel_src, rel_dst, false);
    }

    VFSResult<void> VFSMemoryStorage::RenameImpl(const VFSPath& rel_src, const VFSPath& rel_dst, bool replace)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (!rel_src.IsValid() || !rel_dst.IsValid() || rel_src.IsRoot() || rel_dst.IsRoot() || (rel_src != rel_dst && rel_src.IsPrefixOf(rel_dst)))
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        auto* source = FindNode(rel_src);
        if (!source)
            return VFSResult<void>::Fail(VFSError::NotFound);
        if (rel_src == rel_dst)
            return replace ? VFSResult<void>::Ok() : VFSResult<void>::Fail(VFSError::AlreadyExists);
        auto* destination = FindNode(rel_dst);
        if (destination && !replace)
            return VFSResult<void>::Fail(VFSError::AlreadyExists);
        if (ContainsPinnedRoot(rel_src) || ContainsPinnedRoot(rel_dst))
            return VFSResult<void>::Fail(VFSError::AlreadyExists);
        if (source->WriterCount || (destination && destination->WriterCount))
            return VFSResult<void>::Fail(VFSError::AlreadyExists);
        if (destination && destination->NodeKind != source->NodeKind)
            return VFSResult<void>::Fail(VFSError::NotAFile);
        if (destination && destination->NodeKind == MemNode::Kind::Directory && HasChildren(rel_dst))
            return VFSResult<void>::Fail(VFSError::AlreadyExists);
        const auto parent = !m_owned && replace ? EnsureParentExists(rel_dst) : ValidateParent(rel_dst);
        if (parent.Failed())
            return parent;
        if (source->NodeKind == MemNode::Kind::File)
        {
            AdvanceNamespace();
            if (destination)
            {
                m_nodes.remove(rel_dst.CStr());
                ++m_erased_names;
                DestroyNode(destination);
            }
            m_nodes.remove(rel_src.CStr());
            ++m_erased_names;
            source->Path = rel_dst;
            m_nodes.insert(source->Path.CStr(), source);
            CompactIndexIfNeeded();
            return VFSResult<void>::Ok();
        }

        // Directory moves must rekey every descendant. Validate the entire move
        // before mutating; an overlong descendant cannot leave a partial rename.
        // Only retain pointers, not a full VFSPath for each descendant. Rebuild
        // the validated paths with one reusable temporary during the commit.
        // Copy prefixes once in case the caller passes a node's Path by reference.
        const auto source_path      = rel_src;
        const auto destination_path = rel_dst;
        const auto target_for       = [&](const MemNode& node) {
            const size_t suffix_length      = node.Path.Length() - source_path.Length();
            char         path[VFS_MAX_PATH] = {};
            if (destination_path.Length() + suffix_length >= sizeof(path))
                return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
            Helpers::secure_memcpy(path, sizeof(path), destination_path.CStr(), destination_path.Length());
            Helpers::secure_memcpy(path + destination_path.Length(), sizeof(path) - destination_path.Length(), node.Path.CStr() + source_path.Length(), suffix_length + 1);
            return VFSPath::Parse(path);
        };
        size_t count = 0;
        for (const auto& entry : m_nodes)
            if (source_path.IsPrefixOf(entry.second->Path))
                ++count;
        Helpers::Scope<MemNode*[]> move_owner = {};
        if (!m_owned)
            move_owner.reset(new (std::nothrow) MemNode* [count] {});
        MemNode** moves = m_owned ? m_rename_scratch : move_owner.get();
        if (!moves)
            return VFSResult<void>::Fail(VFSError::OutOfMemory);
        size_t index = 0;
        for (const auto& entry : m_nodes)
        {
            auto* node = entry.second;
            if (!source_path.IsPrefixOf(node->Path))
                continue;
            if (node->WriterCount)
                return VFSResult<void>::Fail(VFSError::AlreadyExists);
            const auto target = target_for(*node);
            if (target.Failed())
                return VFSResult<void>::Fail(target.Error());
            const auto* conflict = FindNode(target.Value());
            if (conflict && conflict != destination)
                return VFSResult<void>::Fail(VFSError::AlreadyExists);
            moves[index++] = node;
        }
        AdvanceNamespace();
        if (destination)
        {
            m_nodes.remove(destination_path.CStr());
            ++m_erased_names;
            DestroyNode(destination);
        }
        for (size_t i = 0; i < count; ++i)
            m_nodes.remove(moves[i]->Path.CStr());
        m_erased_names += count;
        for (size_t i = 0; i < count; ++i)
        {
            auto* node = moves[i];
            node->Path = target_for(*node).Value(); // Already validated under this lock; no allocation or failure.
            m_nodes.insert(node->Path.CStr(), node);
        }
        CompactIndexIfNeeded();
        return VFSResult<void>::Ok();
    }

    VFSResult<void> VFSMemoryStorage::WriteFile(const VFSPath& path, Core::Containers::ArrayView<const uint8_t> data)
    {
        if (!path.IsValid())
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        if (path.IsRoot())
            return VFSResult<void>::Fail(VFSError::NotAFile);
        if (data.size() && !data.data())
            return VFSResult<void>::Fail(VFSError::IOError);
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        MemNode*                            node = FindNode(path);
        if (!node)
        {
            Containers::Array<uint8_t> pending = {};
            pending.m_allocator                = m_owned ? nullptr : m_arena;
            pending.m_slab                     = m_owned ? &m_buffers : nullptr;
            if (!pending.try_reserve(data.size()))
                return VFSResult<void>::Fail(VFSError::OutOfMemory);
            if (data.size())
                Helpers::secure_memcpy(pending.data(), pending.capacity(), data.data(), data.size());
            pending.m_size    = data.size();
            const auto parent = EnsureParentExists(path);
            if (parent.Failed())
                return parent;
            node = CreateNode(path, MemNode::Kind::File);
            if (!node)
                return VFSResult<void>::Fail(VFSError::OutOfMemory);
            node->Data = std::move(pending);
            return VFSResult<void>::Ok();
        }
        if (node->WriterCount)
            return VFSResult<void>::Fail(VFSError::AlreadyExists);
        else if (node->NodeKind == MemNode::Kind::Directory)
        {
            return VFSResult<void>::Fail(VFSError::NotAFile);
        }

        const size_t n = data.size();
        if (node->Data.capacity() < n && !node->Data.try_reserve(ByteBufferCapacity(node->Data.capacity(), n)))
            return VFSResult<void>::Fail(VFSError::OutOfMemory);
        if (n > 0)
        {
            Helpers::secure_memcpy(node->Data.data(), n, data.data(), n);
        }
        node->Data.m_size = n;
        return VFSResult<void>::Ok();
    }

    VFSResult<VFSRootMemoryRequirements> VFSMemoryStorage::MemoryRequirements() const
    {
        using Result = VFSResult<VFSRootMemoryRequirements>;
        if (!m_owned || !m_owned_arena.m_memory)
            return Result::Fail(VFSError::Unsupported);
        // Owned initialization carves the ENTIRE remaining arena into TLSF.
        // Nodes, file wrappers, snapshots and write/rename buffers stay inside
        // this fixed committed domain; no per-open system heap is hidden here.
        const uint64_t pages         = m_owned_arena.m_committed_size;
        const uint64_t tracking      = m_owned_arena.AllocationTrackingBytes();
        const auto     storage_bytes = NativeStorageBytes();
        const auto     root_bytes    = VFSMemoryRoot::NativeStorageBytes();
        if (!storage_bytes || !root_bytes)
            return Result::Fail(VFSError::Unsupported);
        if (pages > UINT64_MAX - tracking || pages + tracking > UINT64_MAX - storage_bytes)
            return Result::Fail(VFSError::SizeLimitExceeded);
        return Result::Ok({
            .StateBytes = root_bytes, .Storage = {this, pages + tracking + storage_bytes}
        });
    }

    VFSResult<Helpers::Scope<IVFSRootOperations>> VFSMemoryStorage::AcquireRoot(const VFSPath& path, VFSRootAccess access)
    {
        using Result = VFSResult<Helpers::Scope<IVFSRootOperations>>;
        if (!m_owned)
            return Result::Fail(VFSError::Unsupported);
        std::unique_lock lock(m_mutex);
        auto*            node = FindNode(path);
        if (!path.IsRoot() && !node)
            return Result::Fail(VFSError::NotFound);
        if (node && node->NodeKind != MemNode::Kind::Directory)
            return Result::Fail(VFSError::NotADirectory);
        if (node && node->RootPins == UINT32_MAX)
            return Result::Fail(VFSError::SizeLimitExceeded);
        MemNode** pin_slot = nullptr;
        if (node && !node->RootPins)
        {
            for (auto& slot : m_pinned_roots)
                if (!slot)
                {
                    pin_slot = &slot;
                    break;
                }
            if (!pin_slot)
                return Result::Fail(VFSError::OutOfMemory);
        }
        Helpers::Scope<VFSMemoryRoot> root(new (std::nothrow) VFSMemoryRoot());
        if (!root)
            return Result::Fail(VFSError::OutOfMemory);
        root->Storage = this;
        root->Root    = path;
        root->Anchor  = node;
        root->Access  = access;
        if (pin_slot)
            *pin_slot = node;
        if (node)
            ++node->RootPins;
        return Result::Ok(std::move(root));
    }

    VFSMemoryRoot::~VFSMemoryRoot()
    {
        // Correct callers already closed every wrapper. Discard any remaining
        // unflushed write without allocation or an implicit commit during teardown.
        for (auto* file : Files)
            if (file)
                Storage->DiscardFile(file);
        if (Anchor)
        {
            std::unique_lock lock(Storage->m_mutex);
            if (!--Anchor->RootPins)
                for (auto& slot : Storage->m_pinned_roots)
                    if (slot == Anchor)
                    {
                        slot = nullptr;
                        break;
                    }
        }
    }

    VFSResult<VFSPath> VFSMemoryRoot::Resolve(const VFSRelativePath& path) const
    {
        if (!path.IsValid())
            return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
        // Namespace changes to this directory or its ancestors are rejected while
        // pinned. Flat-tree storage therefore has an enforced stable anchor.
        return Root.Append(path.CStr());
    }

    VFSResult<Helpers::Scope<IVFSRootOperations>> VFSMemoryRoot::AcquireChild(const VFSRelativePath& path)
    {
        const auto full = Resolve(path);
        // A live retained anchor remains authority after the provider retires.
        // Do not reacquire through the facade or its global mount namespace.
        return full.Succeeded() ? Storage->AcquireRoot(full.Value(), Access) : VFSResult<Helpers::Scope<IVFSRootOperations>>::Fail(full.Error());
    }

    VFSResult<IVFSFile*> VFSMemoryRoot::Open(const VFSRelativePath& path, VFSOpenFlags flags)
    {
        if (Access == VFSRootAccess::ReadOnly && flags != (VFSOpenFlags::Read | VFSOpenFlags::AllowReplace))
            return VFSResult<IVFSFile*>::Fail(VFSError::PermissionDenied);
        const auto full = Resolve(path);
        if (full.Failed())
            return VFSResult<IVFSFile*>::Fail(full.Error());
        std::lock_guard lock(FileMutex);
        for (auto& slot : Files)
        {
            if (slot)
                continue;
            const auto open = Storage->Open(full.Value(), flags);
            if (open.Failed())
                return open;
            slot = open.Value();
            return open;
        }
        return VFSResult<IVFSFile*>::Fail(VFSError::OutOfMemory);
    }

    VFSResult<VFSFileStat> VFSMemoryRoot::Stat(const VFSRelativePath& path) const
    {
        const auto full = Resolve(path);
        return full.Succeeded() ? Storage->Stat(full.Value()) : VFSResult<VFSFileStat>::Fail(full.Error());
    }

    VFSResult<void> VFSMemoryRoot::CreateDir(const VFSRelativePath& path, bool exclusive)
    {
        if (Access == VFSRootAccess::ReadOnly)
            return VFSResult<void>::Fail(VFSError::PermissionDenied);
        const auto full = Resolve(path);
        if (full.Failed())
            return VFSResult<void>::Fail(full.Error());
        if (exclusive)
            return Storage->CreateDirExclusive(full.Value());
        const auto create = Storage->CreateDir(full.Value());
        if (create.Error() != VFSError::AlreadyExists)
            return create;
        const auto stat = Storage->Stat(full.Value());
        if (stat.Failed())
            return VFSResult<void>::Fail(stat.Error());
        return stat.Value().IsDirectory ? VFSResult<void>::Ok() : VFSResult<void>::Fail(VFSError::NotADirectory);
    }

    VFSResult<void> VFSMemoryRoot::Remove(const VFSRelativePath& path)
    {
        if (Access == VFSRootAccess::ReadOnly)
            return VFSResult<void>::Fail(VFSError::PermissionDenied);
        const auto full = Resolve(path);
        return full.Succeeded() ? Storage->Remove(full.Value()) : VFSResult<void>::Fail(full.Error());
    }

    VFSResult<void> VFSMemoryRoot::Rename(const VFSRelativePath& from, const VFSRelativePath& to, bool replace)
    {
        if (Access == VFSRootAccess::ReadOnly)
            return VFSResult<void>::Fail(VFSError::PermissionDenied);
        const auto source = Resolve(from), destination = Resolve(to);
        if (source.Failed() || destination.Failed())
            return VFSResult<void>::Fail(source.Failed() ? source.Error() : destination.Error());
        return replace ? Storage->Rename(source.Value(), destination.Value()) : Storage->RenameNoReplace(source.Value(), destination.Value());
    }

    VFSResult<VFSFileIdentity> VFSMemoryRoot::RootIdentity() const
    {
        return VFSResult<VFSFileIdentity>::Ok(Storage->NodeIdentity(Anchor));
    }

    VFSResult<VFSFileIdentity> VFSMemoryRoot::Identity(const VFSRelativePath& path) const
    {
        const auto full = Resolve(path);
        if (full.Failed())
            return VFSResult<VFSFileIdentity>::Fail(full.Error());
        std::shared_lock lock(Storage->m_mutex);
        const auto*      node = Storage->FindNode(full.Value());
        return node ? VFSResult<VFSFileIdentity>::Ok(Storage->NodeIdentity(node)) : VFSResult<VFSFileIdentity>::Fail(VFSError::NotFound);
    }

    VFSResult<void> VFSMemoryRoot::Visit(const VFSRelativePath* path, uint64_t maximum_entries, Containers::ArrayView<uint8_t> buffer, VFSRootVisitorFn visitor, void* context) const
    {
        static_assert(std::is_trivially_copyable_v<VFSRootEntry>);
        if (!visitor || !buffer.data() || buffer.size() < ZKilo(4) || buffer.size() > ZKilo(64))
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        const auto directory = path ? Resolve(*path) : VFSResult<VFSPath>::Ok(Root);
        if (directory.Failed())
            return VFSResult<void>::Fail(directory.Error());
        std::shared_lock lock(Storage->m_mutex);
        if (!directory.Value().IsRoot())
        {
            const auto* node = Storage->FindNode(directory.Value());
            if (!node)
                return VFSResult<void>::Fail(VFSError::NotFound);
            if (node->NodeKind != MemNode::Kind::Directory)
                return VFSResult<void>::Fail(VFSError::NotADirectory);
        }
        const auto revision = Storage->m_namespace_revision;
        if (revision == UINT64_MAX)
            return VFSResult<void>::Fail(VFSError::SizeLimitExceeded);
        auto         iterator       = Storage->m_nodes.cbegin();
        uint64_t     seen           = 0;
        const size_t batch_capacity = buffer.size() / sizeof(VFSRootEntry);
        for (;;)
        {
            // Reacquire before touching a retained iterator. Callbacks run without
            // tree/file locks, so reentrant reads and mutation attempts cannot
            // deadlock. A namespace mutation invalidates the visit, not memory.
            if (revision != Storage->m_namespace_revision)
                return VFSResult<void>::Fail(VFSError::Corrupted);
            size_t count    = 0;
            bool   exceeded = false;
            while (iterator != Storage->m_nodes.cend() && count < batch_capacity)
            {
                const auto* node = (*iterator).second;
                ++iterator;
                if (node->Path.Parent() != directory.Value())
                    continue;
                if (seen == maximum_entries)
                {
                    exceeded = true;
                    break;
                }
                const auto relative = VFSRelativePath::Parse(node->Path.CStr() + Root.Length() + (Root.IsRoot() ? 0 : 1));
                if (relative.Failed())
                    return VFSResult<void>::Fail(relative.Error());
                const bool         is_directory = node->NodeKind == MemNode::Kind::Directory;
                const VFSRootEntry entry        = {
                    .Path = relative.Value(), .Stat = {.SizeBytes = is_directory ? 0 : node->Data.size(), .IsDirectory = is_directory},
                         .Identity = Storage->NodeIdentity(node)
                };
                Helpers::secure_memcpy(buffer.data() + count * sizeof(entry), buffer.size() - count * sizeof(entry), &entry, sizeof(entry));
                ++count;
                ++seen;
            }
            const bool complete = iterator == Storage->m_nodes.cend();
            lock.unlock();
            for (size_t i = 0; i < count; ++i)
            {
                VFSRootEntry entry = {};
                Helpers::secure_memcpy(&entry, sizeof(entry), buffer.data() + i * sizeof(entry), sizeof(entry));
                const auto visited = visitor(context, entry);
                if (visited.Failed())
                    return visited;
            }
            lock.lock();
            if (revision != Storage->m_namespace_revision)
                return VFSResult<void>::Fail(VFSError::Corrupted);
            if (exceeded)
                return VFSResult<void>::Fail(VFSError::SizeLimitExceeded);
            if (complete)
                return VFSResult<void>::Ok();
        }
    }

    VFSResult<bool> VFSMemoryRoot::Overlaps(const IVFSRootOperations& operations) const
    {
        const auto* other = dynamic_cast<const VFSMemoryRoot*>(&operations);
        if (!other)
            return VFSResult<bool>::Fail(VFSError::Unsupported);
        return VFSResult<bool>::Ok(Storage == other->Storage && (Root.IsPrefixOf(other->Root) || other->Root.IsPrefixOf(Root)));
    }

    VFSResult<void> VFSMemoryRoot::CloseChecked(IVFSFile* file)
    {
        if (!file)
            return VFSResult<void>::Ok();
        std::lock_guard lock(FileMutex);
        for (auto& slot : Files)
            if (slot == file)
            {
                const auto close = file->Close();
                Storage->Close(file);
                slot = nullptr;
                return close;
            }
        return VFSResult<void>::Fail(VFSError::InvalidPath);
    }

    VFSMemoryBackend::VFSMemoryBackend() = default;
    VFSMemoryBackend::~VFSMemoryBackend()
    {
        if (m_storage)
            m_storage->Retire();
    }

    void VFSMemoryBackend::Initialize(Memory::ArenaAllocator* arena)
    {
        Helpers::Ref<VFSMemoryStorage> storage(new (std::nothrow) VFSMemoryStorage());
        ZENGINE_VALIDATE_ASSERT(storage != nullptr, "VFSMemoryBackend: storage allocation failed");
        if (!storage)
            return;
        const auto initialized = storage->Initialize(arena);
        ZENGINE_VALIDATE_ASSERT(initialized.Succeeded(), "VFSMemoryBackend: initialization failed");
        if (initialized.Failed())
            return;
        if (m_storage)
            m_storage->Retire();
        m_storage = std::move(storage);
    }

    VFSResult<void> VFSMemoryBackend::InitializeOwned(size_t bytes, uint32_t max_nodes)
    {
        Helpers::Ref<VFSMemoryStorage> storage(new (std::nothrow) VFSMemoryStorage());
        if (!storage)
            return VFSResult<void>::Fail(VFSError::OutOfMemory);
        const auto initialized = storage->InitializeOwned(bytes, max_nodes);
        if (initialized.Failed())
            return initialized;
        if (m_storage)
            m_storage->Retire();
        m_storage = std::move(storage);
        return VFSResult<void>::Ok();
    }

    Helpers::Ref<IVFSRootProvider> VFSMemoryBackend::RootProvider() const
    {
        return m_storage && m_storage->OwnsStorage() ? Helpers::Ref<IVFSRootProvider>(m_storage) : Helpers::Ref<IVFSRootProvider>{};
    }

    VFSResult<IVFSFile*> VFSMemoryBackend::Open(const VFSPath& path, VFSOpenFlags flags)
    {
        return m_storage ? m_storage->Open(path, flags) : VFSResult<IVFSFile*>::Fail(VFSError::Unsupported);
    }
    void VFSMemoryBackend::Close(IVFSFile* file)
    {
        if (m_storage)
            m_storage->Close(file);
    }
    VFSResult<VFSFileStat> VFSMemoryBackend::Stat(const VFSPath& path) const
    {
        return m_storage ? m_storage->Stat(path) : VFSResult<VFSFileStat>::Fail(VFSError::Unsupported);
    }
    bool VFSMemoryBackend::Exists(const VFSPath& path) const
    {
        return m_storage && m_storage->Exists(path);
    }
    VFSResult<Containers::Array<VFSDirEntry>> VFSMemoryBackend::List(Memory::ArenaAllocator* arena, const VFSPath& path) const
    {
        return m_storage ? m_storage->List(arena, path) : VFSResult<Containers::Array<VFSDirEntry>>::Fail(VFSError::Unsupported);
    }
    VFSResult<void> VFSMemoryBackend::CreateDir(const VFSPath& path)
    {
        return m_storage ? m_storage->CreateDir(path) : VFSResult<void>::Fail(VFSError::Unsupported);
    }
    VFSResult<void> VFSMemoryBackend::CreateDirExclusive(const VFSPath& path)
    {
        return m_storage ? m_storage->CreateDirExclusive(path) : VFSResult<void>::Fail(VFSError::Unsupported);
    }
    VFSResult<void> VFSMemoryBackend::Remove(const VFSPath& path)
    {
        return m_storage ? m_storage->Remove(path) : VFSResult<void>::Fail(VFSError::Unsupported);
    }
    VFSResult<void> VFSMemoryBackend::Rename(const VFSPath& from, const VFSPath& to)
    {
        return m_storage ? m_storage->Rename(from, to) : VFSResult<void>::Fail(VFSError::Unsupported);
    }
    VFSResult<void> VFSMemoryBackend::RenameNoReplace(const VFSPath& from, const VFSPath& to)
    {
        return m_storage ? m_storage->RenameNoReplace(from, to) : VFSResult<void>::Fail(VFSError::Unsupported);
    }
    VFSResult<void> VFSMemoryBackend::WriteFile(const VFSPath& path, Containers::ArrayView<const uint8_t> bytes)
    {
        return m_storage ? m_storage->WriteFile(path, bytes) : VFSResult<void>::Fail(VFSError::Unsupported);
    }
    cstring VFSMemoryBackend::BackendType() const
    {
        return "memory";
    }
    VFSBackendCaps VFSMemoryBackend::Capabilities() const
    {
        return VFSBackendCaps::Read | VFSBackendCaps::Write | VFSBackendCaps::List | VFSBackendCaps::MemoryMap;
    }
} // namespace ZEngine::Core::VFS
