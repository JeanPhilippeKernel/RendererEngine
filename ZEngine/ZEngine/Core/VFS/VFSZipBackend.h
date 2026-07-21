#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSBackend.h>
#include <ZEngine/Core/VFS/IVFSFile.h>
#include <ZEngine/ZEngineDef.h>
#include <miniz.h>
#include <mutex>

namespace ZEngine::Core::VFS
{
    struct VFSZipBackend;

    static constexpr size_t kVFSMaxOpenZipFiles = 256;

    struct ZipEntry
    {
        uint64_t CompSize     = 0;
        uint64_t UncompSize   = 0;
        uint64_t LocalHdrOfs  = 0;
        uint32_t FileIndex    = 0;
        uint32_t Crc32        = 0;
        bool     IsDirectory  = false;
        bool     IsCompressed = false;
    };

    struct VFSZipFile : IVFSFile
    {
        VFSResult<size_t>         Read(Core::Containers::ArrayView<uint8_t> buffer, uint64_t offset) override;
        VFSResult<size_t>         Write(Core::Containers::ArrayView<const uint8_t> buffer, uint64_t offset) override;
        VFSResult<uint64_t>       Size() const override;
        VFSResult<VFSFileStat>    Stat() const override;
        VFSResult<void>           Flush() override;
        const VFSPath&            Path() const override;
        VFSResult<void>           Close() override;

        VFSResult<const uint8_t*> EnsureDecompressed();

    private:
        friend struct VFSZipBackend;

        VFSPath                 m_path;
        const ZipEntry*         m_entry        = nullptr;
        uint8_t*                m_data         = nullptr;
        VFSZipBackend*          m_backend      = nullptr;
        Memory::ArenaAllocator* m_arena        = nullptr;
        bool                    m_decompressed = false;
    };

    struct VFSZipBackend : IVFSBackend
    {
        struct ZipFileSpec
        {
            cstring     Name = nullptr;
            const void* Data = nullptr;
            size_t      Size = 0;
        };
        static bool WriteArchive(cstring path, const ZipFileSpec* files, size_t count);

        void        Initialize(cstring archive_path, Memory::ArenaAllocator* arena, bool case_sensitive = false, size_t max_open_files = kVFSMaxOpenZipFiles);
        void        Shutdown();
        ~VFSZipBackend() override;

        [[nodiscard]] VFSResult<IVFSFile*>                            Open(const VFSPath& relative_path, VFSOpenFlags flags) override;
        void                                                          Close(IVFSFile* file) override;
        [[nodiscard]] VFSResult<VFSFileStat>                          Stat(const VFSPath& relative_path) const override;
        bool                                                          Exists(const VFSPath& relative_path) const override;
        [[nodiscard]] VFSResult<Core::Containers::Array<VFSDirEntry>> List(Core::Memory::ArenaAllocator* arena, const VFSPath& dir) const override;
        cstring                                                       BackendType() const override;
        VFSBackendCaps                                                Capabilities() const override;

        bool                                                          ExtractEntry(uint32_t file_index, void* out, size_t out_size);

    private:
        static void                                                            NormalizeZipEntryName(const char* raw, char* out, size_t out_size);
        void                                                                   MaybeLower(const VFSPath& path, char* out_buf) const;
        uint64_t                                                               HashOf(const VFSPath& p) const;
        void                                                                   IndexEntry(const VFSPath& vp, const ZipEntry& entry);
        void                                                                   AddChildToDir(uint64_t parent_hash, const VFSDirEntry& child);

        Containers::UnorderedHashMap<uint64_t, ZipEntry>                       m_central_dir;
        Containers::UnorderedHashMap<uint64_t, Containers::Array<VFSDirEntry>> m_dir_cache;

        bool                                                                   m_ready                             = false;
        bool                                                                   m_case_sensitive                    = false;
        Memory::ArenaAllocator*                                                m_arena                             = nullptr;
        mz_zip_archive                                                         m_zip                               = {};
        char                                                                   m_archive_path[MAX_FILE_PATH_COUNT] = {};

        std::mutex                                                             m_archive_mutex;
        Memory::PoolAllocator                                                  m_file_pool = {};
    };

} // namespace ZEngine::Core::VFS
