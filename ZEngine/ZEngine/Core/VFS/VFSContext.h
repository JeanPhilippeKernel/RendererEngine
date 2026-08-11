#pragma once
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/VFSDirectoryCache.h>
#include <ZEngine/Core/VFS/VFSFileWatcher.h>
#include <ZEngine/Core/VFS/VFSMountTable.h>
#include <mutex>

namespace ZEngine::Core::VFS
{
    struct VFSScanner;

    struct VFSContext : IVFSContext
    {
        void                                                    Initialize(Memory::ArenaAllocator* arena, size_t mount_table_capacity = 16);

        void                                                    InitWatcher(const char* project_root_native, VFSDirectoryCache* cache, VFSScanner* scanner);
        void                                                    Tick();

        void                                                    ShutdownWatcher();

        [[nodiscard]] VFSResult<IVFSFile*>                      Open(const VFSPath& absolute_path, VFSOpenFlags flags) override;
        void                                                    Close(IVFSFile* file) override;

        [[nodiscard]] VFSResult<Containers::Array<VFSDirEntry>> List(const VFSPath& absolute_dir, Memory::ArenaAllocator* out_arena) override;

        [[nodiscard]] VFSResult<VFSFileStat>                    Stat(const VFSPath& absolute_path) override;

        [[nodiscard]] VFSResult<bool>                           Exists(const VFSPath& absolute_path) override;

        [[nodiscard]] VFSResult<void>                           Mount(IVFSBackend* backend, const VFSPath& logical_root, int priority) override;

        [[nodiscard]] VFSResult<void>                           Unmount(const VFSPath& logical_root) override;

        [[nodiscard]] VFSResult<void>                           CreateDir(const VFSPath& absolute_path) override;

        [[nodiscard]] VFSResult<void>                           Remove(const VFSPath& absolute_path) override;

        [[nodiscard]] VFSResult<void>                           Rename(const VFSPath& src, const VFSPath& dst) override;

        void                                                    Shutdown() override;

    private:
        [[nodiscard]] VFSResult<ResolveResult> ResolveWritable(const VFSPath& path) const;

        VFSMountTable                          m_mount_table = {};
        Memory::ArenaAllocator*                m_arena       = nullptr;
        mutable std::mutex                     m_arena_mutex;

        IVFSPlatformWatcher*                   m_platform_watcher                         = nullptr;
        VFSFileWatcher*                        m_file_watcher                             = nullptr;
        VFSDirectoryCache*                     m_directory_cache                          = nullptr;
        VFSScanner*                            m_scanner                                  = nullptr;
        char                                   m_project_root_native[MAX_FILE_PATH_COUNT] = {};
    };

} // namespace ZEngine::Core::VFS
