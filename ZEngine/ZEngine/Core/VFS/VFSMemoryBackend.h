#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSBackend.h>
#include <ZEngine/Core/VFS/IVFSFile.h>
#include <ZEngine/ZEngineDef.h>
#include <mutex>
#include <shared_mutex>

namespace ZEngine::Core::VFS
{
    static constexpr size_t kVFSMaxOpenMemoryFiles = 256;

    struct MemNode
    {
        enum class Kind : uint8_t
        {
            File      = 0,
            Directory = 1
        };

        Kind                             NodeKind = Kind::File;
        VFSPath                          Path     = {};
        Core::Containers::Array<uint8_t> Data     = {};
    };

    struct VFSMemoryFile final : public IVFSFile
    {
        VFSMemoryFile() = default;
        ~VFSMemoryFile() override;

        VFSResult<size_t>      Read(Core::Containers::ArrayView<uint8_t> buffer, uint64_t offset) override;
        VFSResult<size_t>      Write(Core::Containers::ArrayView<const uint8_t> buffer, uint64_t offset) override;
        VFSResult<uint64_t>    Size() const override;
        VFSResult<VFSFileStat> Stat() const override;
        VFSResult<void>        Flush() override;
        const VFSPath&         Path() const override;
        VFSResult<void>        Close() override;

        VFSResult<void*>       MemoryMap(uint64_t& out_size);

    private:
        friend struct VFSMemoryBackend;

        MemNode*                            m_node   = nullptr;
        VFSOpenFlags                        m_flags  = VFSOpenFlags::None;
        bool                                m_closed = false;

        std::shared_lock<std::shared_mutex> m_read_lock;

        Core::Containers::Array<uint8_t>    m_write_buf     = {};
        Memory::ArenaAllocator*             m_arena         = nullptr;

        std::shared_mutex*                  m_backend_mutex = nullptr;

        bool                                IsWriteMode() const;
        VFSResult<void>                     Commit();
    };

    struct VFSMemoryBackend final : public IVFSBackend
    {
        VFSMemoryBackend();
        ~VFSMemoryBackend() override;

        void                                                          Initialize(Memory::ArenaAllocator* arena);

        [[nodiscard]] VFSResult<IVFSFile*>                            Open(const VFSPath& relative_path, VFSOpenFlags flags) override;
        void                                                          Close(IVFSFile* file) override;
        [[nodiscard]] VFSResult<VFSFileStat>                          Stat(const VFSPath& path) const override;
        bool                                                          Exists(const VFSPath& path) const override;
        [[nodiscard]] VFSResult<Core::Containers::Array<VFSDirEntry>> List(Core::Memory::ArenaAllocator* arena, const VFSPath& dir) const override;
        [[nodiscard]] VFSResult<void>                                 CreateDir(const VFSPath& relative_path) override;
        [[nodiscard]] VFSResult<void>                                 Remove(const VFSPath& relative_path) override;
        [[nodiscard]] VFSResult<void>                                 Rename(const VFSPath& rel_src, const VFSPath& rel_dst) override;
        cstring                                                       BackendType() const override;
        VFSBackendCaps                                                Capabilities() const override;

        [[nodiscard]] VFSResult<void>                                 WriteFile(const VFSPath& path, Core::Containers::ArrayView<const uint8_t> data);

    private:
        Core::Containers::UnorderedHashMap<cstring, MemNode*> m_nodes;
        mutable std::shared_mutex                             m_mutex;
        Memory::ArenaAllocator*                               m_arena     = nullptr;

        Memory::PoolAllocator                                 m_file_pool = {};
        std::mutex                                            m_file_pool_mutex;

        MemNode*                                              FindNode(const VFSPath& path);
        const MemNode*                                        FindNode(const VFSPath& path) const;
        MemNode*                                              CreateNode(const VFSPath& path, MemNode::Kind kind);
        void                                                  EnsureParentExists(const VFSPath& path);
        bool                                                  HasChildren(const VFSPath& dir) const;

        VFSMemoryFile*                                        AllocFile();
        void                                                  FreeFile(IVFSFile* file);
    };

} // namespace ZEngine::Core::VFS
