
#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSBackend.h>
#include <ZEngine/Core/VFS/IVFSFile.h>
#include <ZEngine/ZEngineDef.h>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ZEngine::Core::VFS
{

    static constexpr size_t kVFSMaxOpenFiles = 256;

    struct VFSDiskFile : IVFSFile
    {
        VFSResult<size_t>      Read(Core::Containers::ArrayView<uint8_t> buffer, uint64_t offset) override;
        VFSResult<size_t>      Write(Core::Containers::ArrayView<const uint8_t> buffer, uint64_t offset) override;
        VFSResult<uint64_t>    Size() const override;
        VFSResult<VFSFileStat> Stat() const override;
        VFSResult<void>        Flush() override;
        const VFSPath&         Path() const override;
        VFSResult<void>        Close() override;

        VFSResult<void*>       MemoryMap(uint64_t& out_size);

        static void            Unmap(void* ptr, uint64_t size);

        ~VFSDiskFile();

    private:
        friend struct VFSDiskBackend;

#if defined(_WIN32)
        HANDLE m_handle = INVALID_HANDLE_VALUE;
#else
        int m_fd = -1;
#endif
        bool     m_writable    = false;
        uint64_t m_size        = 0;
        VFSPath  m_path        = {};
        void*    m_mapped_ptr  = nullptr;
        uint64_t m_mapped_size = 0;
    };

    struct VFSDiskBackend : IVFSBackend
    {
        void                                                          Initialize(cstring native_root, VFSBackendCaps caps, Memory::ArenaAllocator* arena, size_t max_open_files = kVFSMaxOpenFiles);

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

    private:
        bool                    ResolveNativePath(const VFSPath& relative, char* out_buf) const;

        static bool             ProbeCaseSensitivity(cstring native_root);

        char                    m_native_root[MAX_FILE_PATH_COUNT] = {0};
        size_t                  m_native_root_len                  = 0;
        VFSBackendCaps          m_caps                             = VFSBackendCaps::Read;
        Memory::ArenaAllocator* m_arena                            = nullptr;
        Memory::PoolAllocator   m_file_pool                        = {};
        bool                    m_case_sensitive                   = true;
    };

} // namespace ZEngine::Core::VFS
