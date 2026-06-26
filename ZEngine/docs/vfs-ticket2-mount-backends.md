# ZEngine VFS System — Ticket 2 Implementation Specification

**Priority:** P2 — Implement after VFS Ticket 1 is live  
**Status:** Ready for implementation  
**Depends on:** `vfs-design.md` (Ticket 1)  
**Blocks:** `vfs-ticket3`, `import-pipeline.md`

## Mount Table, VFSContext, VFSDiskBackend, VFSZipBackend

---

## 1. Directory Layout (New Files Only)

```
ZEngine/ZEngine/Core/VFS/
├── VFSMountTable.h
├── VFSMountTable.cpp
├── VFSContext.h
├── VFSContext.cpp
├── VFSDiskBackend.h
├── VFSDiskBackend.cpp
├── VFSZipBackend.h
└── VFSZipBackend.cpp

ZEngine/tests/VFS/
├── vfs_mount_test.cpp
├── vfs_disk_test.cpp
└── vfs_zip_test.cpp

ZEngine/__externals/miniz/
└── miniz.h          (copy from __externals/assimp/contrib/zip/src/miniz.h — already present)
```

**CMakeLists.txt changes required:**

In `ZEngine/ZEngine/CMakeLists.txt`, add the new VFS include directory and the miniz external path:

```cmake
# In target_include_directories, add:
./Core/VFS

# And for the miniz header location, add to target_include_directories:
${PROJECT_SOURCE_DIR}/../__externals/miniz
```

`GLOB_RECURSE` already picks up all `.cpp` files under `ZEngine/ZEngine/`, so `VFSMountTable.cpp`, `VFSContext.cpp`, `VFSDiskBackend.cpp`, and `VFSZipBackend.cpp` are compiled automatically.

In `ZEngine/tests/CMakeLists.txt`, add `VFS/*.cpp` to the `GLOB` pattern:

```cmake
file(GLOB TEST_SOURCES
    Memory/*.cpp
    Containers/*.cpp
    Maths/*.cpp
    Misc/*.cpp
    VFS/*.cpp)   # ADD THIS LINE
```

---

## 2. `VFSMountTable` — Full Declaration

**File:** `ZEngine/ZEngine/Core/VFS/VFSMountTable.h`

```cpp
#pragma once
#include <Core/Containers/Array.h>
#include <Core/Memory/Allocator.h>
#include <ZEngineDef.h>
// Forward declarations from Ticket 1 headers:
#include <Core/VFS/VFSTypes.h>    // IVFSBackend, VFSPath, VFSResult, VFSError

#include <shared_mutex>

namespace ZEngine::Core::VFS
{
    // -------------------------------------------------------------------------
    // MountPoint
    // A single entry in the mount table.
    // Id is an arena-allocated C-string copy of the logical_root buffer.
    // -------------------------------------------------------------------------
    struct MountPoint
    {
        char         Id[MAX_FILE_PATH_COUNT]; // logical root string, NUL-terminated
        VFSPath      LogicalRoot;             // normalized, hashed
        IVFSBackend* Backend    = nullptr;
        int          Priority   = 0;
    };

    // -------------------------------------------------------------------------
    // ResolveResult
    // Output of Resolve(): the winning backend + the relative path to pass it.
    // -------------------------------------------------------------------------
    struct ResolveResult
    {
        IVFSBackend* Backend      = nullptr;
        VFSPath      RelativePath; // path with the logical_root prefix stripped
    };

    // -------------------------------------------------------------------------
    // VFSMountTable
    //
    // Owns an Array<MountPoint> sorted descending by Priority (highest first).
    // Readers take a shared_lock; Mount/Unmount take a unique_lock.
    // All memory for MountPoint data (Id copies, etc.) comes from m_arena.
    // -------------------------------------------------------------------------
    struct VFSMountTable
    {
        static constexpr uint32_t kMaxMountPoints = 256;

        // In Mount(): add before inserting
        // ZENGINE_VALIDATE_ASSERT(m_mounts.Size() < kMaxMountPoints,
        //     "VFSMountTable: maximum mount point count (256) exceeded");

        // Initialize the table. arena must outlive this object.
        void Initialize(Memory::ArenaAllocator* arena, size_t initial_capacity = 16);

        // Mount a backend at logical_root with the given priority.
        // Returns AlreadyExists if a mount with the same logical_root already exists.
        // Inserts the new entry in sorted position (descending priority).
        [[nodiscard]] VFSResult<void> Mount(IVFSBackend* backend, const VFSPath& logical_root, int priority);

        // Unmount by logical_root string. Returns NotFound if not present.
        [[nodiscard]] VFSResult<void> Unmount(const VFSPath& logical_root);

        // Returns the highest-priority backend whose logical_root is a prefix
        // of `path`, and strips that prefix to produce RelativePath.
        // Returns NotFound if no mount matches.
        [[nodiscard]] VFSResult<ResolveResult> Resolve(const VFSPath& path) const;

        // Returns ALL backends whose logical_root is a prefix of `dir_path`,
        // ordered descending by priority. Used for overlay directory listing.
        // Caller provides the output array (arena-allocated by caller).
        [[nodiscard]] VFSResult<void> ResolveAll(
            const VFSPath&                              dir_path,
            Containers::Array<ResolveResult>&           out_results) const;

        // Number of active mount points.
        size_t Count() const;

    private:
        // Returns true if `prefix` is a path prefix of `path`.
        // Prefix "/assets" matches "/assets/textures/foo.png" but not "/assetstore".
        static bool IsPrefixOf(const VFSPath& prefix, const VFSPath& path);

        // Strips the prefix from path, returning the relative sub-path.
        // e.g. prefix="/assets", path="/assets/tex/foo.png" -> "tex/foo.png"
        // If prefix == path, returns VFSPath representing "./" (empty relative).
        [[nodiscard]] static VFSResult<VFSPath> StripPrefix(const VFSPath& prefix, const VFSPath& path);

        Memory::ArenaAllocator*       m_arena    = nullptr;
        Containers::Array<MountPoint> m_mounts;
        mutable std::shared_mutex     m_mutex;
    };

} // namespace ZEngine::Core::VFS
```

### Implementation Notes for `VFSMountTable`

**`Mount(backend, logical_root, priority)`**

```
unique_lock(m_mutex)
for each entry in m_mounts:
    if entry.LogicalRoot == logical_root:
        return Fail(VFSError::AlreadyExists)

allocate MountPoint mp on m_arena:
    secure_strcpy(mp.Id, MAX_FILE_PATH_COUNT, logical_root.Buffer())
    mp.LogicalRoot = logical_root
    mp.Backend     = backend
    mp.Priority    = priority

find insertion index i such that m_mounts[i].Priority < priority  (first lower-priority slot)
// shift entries right to make room — Array supports push() but not insert(); use push + rotate
m_mounts.push(mp)
// rotate from i to end so mp sits at index i:
for j = m_mounts.size()-1 downto i+1:
    swap(m_mounts[j], m_mounts[j-1])

return Ok(void)
```

Note: `Array<T>` does not have `insert()`. The engineer must implement in-place rotation using swaps after `push()`. This is O(n) but the mount table is never large (< 64 entries in practice).

**`Unmount(logical_root)`**

```
unique_lock(m_mutex)
for i in [0, m_mounts.size()):
    if m_mounts[i].LogicalRoot == logical_root:
        // Shift remaining entries left (no erase on Array — shift manually)
        for j = i; j < m_mounts.size()-1; j++:
            m_mounts[j] = m_mounts[j+1]
        m_mounts.pop()    // shrinks logical size by 1
        return Ok(void)
return Fail(VFSError::NotFound)
```

**`Resolve(path)`**

```
shared_lock(m_mutex)
for each mp in m_mounts (already descending priority order):
    if IsPrefixOf(mp.LogicalRoot, path):
        rel = StripPrefix(mp.LogicalRoot, path)
        if rel.Failed(): return Fail(rel.Error())
        return Ok(ResolveResult{ mp.Backend, rel.Value() })
return Fail(VFSError::NotFound)
```

**`ResolveAll(dir_path, out_results)`**

```
shared_lock(m_mutex)
for each mp in m_mounts:
    if IsPrefixOf(mp.LogicalRoot, dir_path):
        rel = StripPrefix(mp.LogicalRoot, dir_path)
        if rel.Succeeded():
            out_results.push(ResolveResult{ mp.Backend, rel.Value() })
// out_results is already in descending priority order because m_mounts is sorted
return Ok(void)
```

**`IsPrefixOf(prefix, path)`**

```
prefix_len = strlen(prefix.Buffer())
path_buf   = path.Buffer()

if prefix_len == 0: return true   // root "/" matches everything
if prefix_len == 1 && prefix.Buffer()[0] == '/': return true

// Require either exact match or path separator after prefix
if strncmp(prefix.Buffer(), path_buf, prefix_len) != 0: return false
// Check that the character at prefix_len is '/' or '\0'
return path_buf[prefix_len] == '/' || path_buf[prefix_len] == '\0'
```

**`StripPrefix(prefix, path)`**

```
prefix_len = strlen(prefix.Buffer())
path_buf   = path.Buffer()
path_len   = strlen(path_buf)

// Skip the separator after the prefix
skip = prefix_len
if path_buf[skip] == '/': skip++

// Remaining is the relative string
relative_str = &path_buf[skip]
if *relative_str == '\0': relative_str = ""

return VFSPath::Parse(relative_str)   // re-normalizes the relative path
```

---

## 3. `VFSContext` — Full Declaration

**File:** `ZEngine/ZEngine/Core/VFS/VFSContext.h`

```cpp
#pragma once
#include <Core/VFS/VFSMountTable.h>
#include <Core/VFS/VFSTypes.h>   // IVFSContext, VFSPath, VFSResult, VFSDirEntry, VFSFileStat

namespace ZEngine::Core::VFS
{
    // -------------------------------------------------------------------------
    // VFSContext
    //
    // The real IVFSContext implementation. Owns a VFSMountTable.
    // Replaces the stub VFSDiskContext from Ticket 1.
    //
    // Thread safety: all routing goes through VFSMountTable which is already
    // reader/writer locked. VFSContext itself adds no additional locking.
    // -------------------------------------------------------------------------
    struct VFSContext : IVFSContext
    {
        // Initialize. arena must outlive this context.
        void Initialize(Memory::ArenaAllocator* arena, size_t mount_table_capacity = 16);

        // ---- IVFSContext overrides ------------------------------------------

        // Resolves path to a backend, strips prefix, calls backend->Open.
        [[nodiscard]] VFSResult<IVFSFile*>  Open(const VFSPath& absolute_path, VFSOpenFlags flags) override;

        // Calls ResolveAll, merges DirEntry lists from all backends.
        // Higher-priority backend entries win on filename collision.
        [[nodiscard]] VFSResult<Containers::Array<VFSDirEntry>> List(
            const VFSPath&          absolute_dir,
            Memory::ArenaAllocator* out_arena) override;

        // Resolves to backend, delegates Stat.
        [[nodiscard]] VFSResult<VFSFileStat>    Stat(const VFSPath& absolute_path) override;

        // Stat followed by error-check.
        [[nodiscard]] VFSResult<bool>           Exists(const VFSPath& absolute_path) override;

        // Mount a backend at the logical root with given priority.
        [[nodiscard]] VFSResult<void>           Mount(IVFSBackend* backend,
                                        const VFSPath& logical_root,
                                        int priority) override;

        // Unmount by logical root.
        [[nodiscard]] VFSResult<void>           Unmount(const VFSPath& logical_root) override;

        // Routes to the highest-priority WRITABLE backend only.
        [[nodiscard]] VFSResult<void>           CreateDir(const VFSPath& absolute_path) override;

        // Routes to the highest-priority WRITABLE backend only.
        [[nodiscard]] VFSResult<void>           Remove(const VFSPath& absolute_path) override;

        // Routes to the highest-priority WRITABLE backend only.
        // src and dst must resolve to the same backend.
        [[nodiscard]] VFSResult<void>           Rename(const VFSPath& src, const VFSPath& dst) override;

    private:
        // Find the first backend that has VFSBackendCaps::Write set.
        // Used by CreateDir, Remove, Rename.
        [[nodiscard]] VFSResult<ResolveResult>  ResolveWritable(const VFSPath& path) const;

        VFSMountTable             m_mount_table;
        Memory::ArenaAllocator*   m_arena    = nullptr;
    };

} // namespace ZEngine::Core::VFS
```

### Implementation Notes for `VFSContext`

**`Open(absolute_path, flags)`**

```
auto r = m_mount_table.Resolve(absolute_path)
if r.Failed(): return Fail(r.Error())
auto [backend, rel] = r.Value()
return backend->Open(rel, flags)
```

**`List(absolute_dir, out_arena)`**

```
// CORRECT ordering — release the lock before calling backends:

// Step 1: Resolve all matching mount points under the shared lock.
Array<ResolveResult> resolves;
{
    std::shared_lock lock(m_mount_table.m_mutex);  // hold lock only for the lookup
    m_mount_table.ResolveAll(dir_path, resolves);
}
// Lock released here. Mount/Unmount can now proceed if needed.

// Step 2: Call backends WITHOUT holding any lock.
// Backend List() calls may stall (ZIP decompression, network I/O).
// Holding the mount table lock during this would starve Mount/Unmount callers.
for (auto& rr : resolves) {
    auto sub_result = rr.Backend->List(scratch_arena, rr.RelativePath);
    if (sub_result.Failed()) continue;
    // ... dedup and merge results ...
}
```

**Key deduplication rule:** Because backends are visited in descending priority order, the first backend to contribute an entry for a given filename wins. Subsequent lower-priority backends that have the same filename are skipped.

**`Stat(absolute_path)`**

```
r = m_mount_table.Resolve(absolute_path)
if r.Failed(): return Fail(r.Error())
auto [backend, rel] = r.Value()
return backend->Stat(rel)
```

**`ResolveWritable(path)`**

```
scratch = ZGetScratch(m_arena)
Array<ResolveResult> resolves;
resolves.init(scratch.Arena, 8)
m_mount_table.ResolveAll(path, resolves)
for each rr in resolves:
    caps = rr.Backend->Capabilities()
    if caps & VFSBackendCaps::Write:
        ZReleaseScratch(scratch)
        return Ok(rr)
ZReleaseScratch(scratch)
return Fail(VFSError::PermissionDenied)
```

**`Rename(src, dst)`**

```
r_src = m_mount_table.Resolve(src)
r_dst = m_mount_table.Resolve(dst)
if r_src.Failed(): return Fail(r_src.Error())
if r_dst.Failed(): return Fail(r_dst.Error())
if r_src.Value().Backend != r_dst.Value().Backend:
    ZENGINE_CORE_ERROR("VFSContext::Rename: src and dst resolve to different backends")
    return Fail(VFSError::Unsupported)
auto [backend, rel_src] = r_src.Value()
auto [_, rel_dst]       = r_dst.Value()
return backend->Rename(rel_src, rel_dst)
```

---

## 4. `VFSDiskBackend` + `VFSDiskFile` — Full Declarations

**File:** `ZEngine/ZEngine/Core/VFS/VFSDiskBackend.h`

```cpp
#pragma once
#include <Core/VFS/VFSTypes.h>
#include <Core/Memory/Allocator.h>
#include <ZEngineDef.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <atomic>

namespace ZEngine::Core::VFS
{
    // -------------------------------------------------------------------------
    // VFSDiskFile
    //
    // Wraps a platform file handle. No seek cursor: all reads/writes take an
    // explicit byte offset (stateless I/O). Multiple VFSDiskFile instances for
    // the same underlying path are safe to use concurrently.
    // -------------------------------------------------------------------------
    struct VFSDiskFile : IVFSFile
    {
#if defined(_WIN32)
        HANDLE          m_handle  = INVALID_HANDLE_VALUE;
#else
        int             m_fd      = -1;
#endif
        uint64_t        m_size    = 0;   // cached at open time
        bool            m_writable = false;

        // IVFSFile overrides
        VFSResult<size_t>   Read(std::span<uint8_t> buf, uint64_t offset) override;
        VFSResult<size_t>   Write(std::span<const uint8_t> buf, uint64_t offset) override;
        VFSResult<uint64_t> Size() override;
        VFSResult<void>     Close() override;

        // Memory-map the entire file (read-only).
        // Returns pointer to mapped region and size via out parameters.
        // Caller is responsible for unmapping via Unmap().
        VFSResult<void*>    MemoryMap(uint64_t& out_size);

        // Unmap a region previously returned by MemoryMap().
        static void         Unmap(void* ptr, uint64_t size);

        ~VFSDiskFile();

    private:
        void* m_mapped_ptr  = nullptr;
        uint64_t m_mapped_size = 0;
    };

    // -------------------------------------------------------------------------
    // VFSDiskBackend
    //
    // IVFSBackend backed by a directory on the real filesystem.
    // Constructor probes case sensitivity and records m_native_root.
    //
    // Every method validates that the resolved native path stays under
    // m_native_root (sandbox check).
    // -------------------------------------------------------------------------
    struct VFSDiskBackend : IVFSBackend
    {
        // native_root: absolute native OS path to the root directory.
        // caps:        VFSBackendCaps flags (Read + Write, or Read only).
        // arena:       allocator for temporary path scratch work.
        void Initialize(cstring native_root,
                        VFSBackendCaps caps,
                        Memory::ArenaAllocator* arena);

        // IVFSBackend overrides
        [[nodiscard]] VFSResult<IVFSFile*>  Open(const VFSPath& relative_path, VFSOpenFlags flags) override;
        [[nodiscard]] VFSResult<void>       Close(IVFSFile* file) override;
        [[nodiscard]] VFSResult<VFSFileStat> Stat(const VFSPath& relative_path) override;
        [[nodiscard]] VFSResult<Containers::Array<VFSDirEntry>> List(const VFSPath& relative_dir,
                                                       Memory::ArenaAllocator* out_arena) override;
        [[nodiscard]] VFSResult<void>       CreateDir(const VFSPath& relative_path) override;
        [[nodiscard]] VFSResult<void>       Remove(const VFSPath& relative_path) override;
        [[nodiscard]] VFSResult<void>       Rename(const VFSPath& rel_src, const VFSPath& rel_dst) override;
        VFSBackendCaps        Capabilities() const override;

    private:
        // Resolves a relative VFSPath to an absolute native path.
        // Writes result into out_buf (MAX_FILE_PATH_COUNT chars).
        // Returns false if the resulting path escapes m_native_root (sandbox violation).
        bool ResolveNativePath(const VFSPath& relative, char* out_buf) const;

        // Returns true if the filesystem at m_native_root is case-sensitive.
        // Probed at Initialize() time by attempting to stat a path with mismatched case.
        static bool ProbeCaseSensitivity(cstring native_root);

        char                    m_native_root[MAX_FILE_PATH_COUNT];
        size_t                  m_native_root_len   = 0;
        VFSBackendCaps          m_caps              = VFSBackendCaps::Read;
        bool                    m_case_sensitive    = true;
        Memory::ArenaAllocator* m_arena             = nullptr;
    };

} // namespace ZEngine::Core::VFS
```

### Implementation Notes for `VFSDiskBackend`

**`Initialize(native_root, caps, arena)`**

```
secure_strcpy(m_native_root, MAX_FILE_PATH_COUNT, native_root)
m_native_root_len = secure_strlen(m_native_root)
// Strip trailing slash from m_native_root except for root "/"
if m_native_root_len > 1 && m_native_root[m_native_root_len-1] == '/':
    m_native_root[m_native_root_len-1] = '\0'
    m_native_root_len--
m_caps           = caps
m_arena          = arena
m_case_sensitive = ProbeCaseSensitivity(m_native_root)
```

**`ResolveNativePath(relative, out_buf)` — Sandbox Check Algorithm**

```
// Step 1: Build the candidate native path
//   native = m_native_root + "/" + relative.Buffer()
// Use snprintf to compose into a stack buffer (MAX_FILE_PATH_COUNT)

char candidate[MAX_FILE_PATH_COUNT]
int written = snprintf(candidate, MAX_FILE_PATH_COUNT, "%s/%s",
                       m_native_root, relative.Buffer())
if written < 0 || written >= MAX_FILE_PATH_COUNT:
    return false  // path too long

// Step 2: Canonicalize (resolve ".." and symlinks)
#if defined(_WIN32)
    char canonical[MAX_PATH]
    DWORD result = GetFullPathNameA(candidate, MAX_PATH, canonical, nullptr)
    if result == 0 || result >= MAX_PATH: return false
#else
    char canonical[PATH_MAX]
    // Use realpath() if the path exists, else manually normalize
    // For paths that don't exist yet (Create, Write), use manual normalization:
    if realpath(candidate, canonical) == nullptr:
        // Manual normalization: resolve ".." segments without hitting filesystem
        NormalizePath(candidate, canonical, PATH_MAX)  // see below
#endif

// Step 3: Sandbox containment check
// canonical must start with m_native_root
// AND the next character is '/' or '\0'
size_t canon_len = secure_strlen(canonical)
if canon_len < m_native_root_len:
    ZENGINE_CORE_WARN("VFSDiskBackend: sandbox violation detected: {}", candidate)
    return false
if strncmp(canonical, m_native_root, m_native_root_len) != 0:
    ZENGINE_CORE_WARN("VFSDiskBackend: sandbox violation detected: {}", candidate)
    return false
char sep = canonical[m_native_root_len]
if sep != '\0' && sep != '/':
    ZENGINE_CORE_WARN("VFSDiskBackend: sandbox violation detected: {}", candidate)
    return false

secure_strcpy(out_buf, MAX_FILE_PATH_COUNT, canonical)
return true
```

**Manual path normalization** (for paths that do not yet exist on disk, used by `Create`/`Write`):

```
NormalizePath(input, output, output_size):
    // Walk input, push each segment onto a stack; on ".." pop the stack
    // Reconstruct from the stack
    // This avoids filesystem calls for non-existent paths while still
    // resolving ".." escape attempts
```

**`Open(relative_path, flags)`**

```
char native[MAX_FILE_PATH_COUNT]
if not ResolveNativePath(relative_path, native):
    return Fail(VFSError::PermissionDenied)

if (flags & VFSOpenFlags::Write) && !(m_caps & VFSBackendCaps::Write):
    return Fail(VFSError::PermissionDenied)

// Allocate VFSDiskFile from arena
VFSDiskFile* f = ZPushStructCtor(m_arena, VFSDiskFile)

#if defined(_WIN32)
    DWORD access = GENERIC_READ
    if flags & VFSOpenFlags::Write: access |= GENERIC_WRITE
    DWORD creation = OPEN_EXISTING
    if flags & VFSOpenFlags::Create: creation = OPEN_ALWAYS
    if flags & VFSOpenFlags::Truncate: creation = CREATE_ALWAYS
    f->m_handle = CreateFileA(native, access, FILE_SHARE_READ, nullptr,
                              creation, FILE_ATTRIBUTE_NORMAL, nullptr)
    if f->m_handle == INVALID_HANDLE_VALUE:
        return Fail(VFSError::NotFound)
    LARGE_INTEGER sz; GetFileSizeEx(f->m_handle, &sz)
    f->m_size = (uint64_t)sz.QuadPart
#else
    int oflags = O_RDONLY
    if flags & (VFSOpenFlags::Write | VFSOpenFlags::Append): oflags = O_RDWR
    if flags & VFSOpenFlags::Create:  oflags |= O_CREAT
    if flags & VFSOpenFlags::Truncate: oflags |= O_TRUNC
    if flags & VFSOpenFlags::Append:  oflags |= O_APPEND
    f->m_fd = open(native, oflags, 0644)
    if f->m_fd < 0:
        return Fail(VFSError::NotFound)
    struct stat st; fstat(f->m_fd, &st)
    f->m_size = (uint64_t)st.st_size
#endif

f->m_writable = !!(flags & (VFSOpenFlags::Write | VFSOpenFlags::Append))
return Ok(static_cast<IVFSFile*>(f))
```

**`VFSDiskFile::Read(buf, offset)`** (POSIX):

```
ssize_t n = pread(m_fd, buf.data(), buf.size(), (off_t)offset)
if n < 0: return Fail(VFSError::IOError)
return Ok((size_t)n)
```

**`VFSDiskFile::Read(buf, offset)`** (Windows):

```
OVERLAPPED ov = {}
ov.Offset     = (DWORD)(offset & 0xFFFFFFFF)
ov.OffsetHigh = (DWORD)(offset >> 32)
DWORD read_bytes = 0
BOOL ok = ReadFile(m_handle, buf.data(), (DWORD)buf.size(), &read_bytes, &ov)
if !ok: return Fail(VFSError::IOError)
return Ok((size_t)read_bytes)
```

**`VFSDiskFile::MemoryMap(out_size)`** (POSIX):

```
if m_mapped_ptr != nullptr: return Ok(m_mapped_ptr)  // already mapped
out_size = m_size
m_mapped_ptr  = mmap(nullptr, m_size, PROT_READ, MAP_PRIVATE, m_fd, 0)
if m_mapped_ptr == MAP_FAILED:
    m_mapped_ptr = nullptr
    return Fail(VFSError::IOError)
m_mapped_size = m_size
return Ok(m_mapped_ptr)
```

**`VFSDiskFile::MemoryMap(out_size)`** (Windows):

```
HANDLE fm = CreateFileMappingA(m_handle, nullptr, PAGE_READONLY, 0, 0, nullptr)
if fm == nullptr: return Fail(VFSError::IOError)
m_mapped_ptr = MapViewOfFile(fm, FILE_MAP_READ, 0, 0, 0)
CloseHandle(fm)   // mapping handle can be closed immediately
if m_mapped_ptr == nullptr: return Fail(VFSError::IOError)
m_mapped_size = m_size
out_size      = m_size
return Ok(m_mapped_ptr)
```

**`ProbeCaseSensitivity(native_root)`**

```
// Create a temp probe path by upper-casing the first component of native_root
// Then attempt to stat it. If stat succeeds, the FS is case-insensitive.
// On Linux, return true unconditionally.
// On macOS/Windows, probe:
#if defined(__APPLE__) || defined(_WIN32)
    char probe[MAX_FILE_PATH_COUNT]
    secure_strcpy(probe, MAX_FILE_PATH_COUNT, native_root)
    // Mutate a character in probe to a different case
    for i in [0, strlen(probe)):
        if probe[i] >= 'a' && probe[i] <= 'z':
            probe[i] = probe[i] - 'a' + 'A'
            break
        elif probe[i] >= 'A' && probe[i] <= 'Z':
            probe[i] = probe[i] - 'A' + 'a'
            break
    struct stat st
    // If the mutated path also resolves, the FS is case-insensitive
    return (stat(probe, &st) != 0)  // true = case-sensitive (probe failed)
#else
    return true
#endif
```

---

## 5. `VFSZipBackend`, `ZipEntry`, and `VFSZipFile` — Full Declarations

**File:** `ZEngine/ZEngine/Core/VFS/VFSZipBackend.h`

```cpp
#pragma once
#include <Core/VFS/VFSTypes.h>
#include <Core/Containers/UnorderedHashMap.h>
#include <Core/Containers/Array.h>
#include <Core/Memory/Allocator.h>
#include <ZEngineDef.h>

// miniz is a single-header C library.
// Include only the declaration macros; the implementation is compiled once
// in VFSZipBackend.cpp via #define MINIZ_IMPLEMENTATION.
#include <miniz/miniz.h>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

#include <atomic>
#include <mutex>

namespace ZEngine::Core::VFS
{
    // -------------------------------------------------------------------------
    // ZipEntry
    // Metadata for one file/directory in the ZIP central directory.
    // Stored as the value in the central-directory hash map.
    // -------------------------------------------------------------------------
    struct ZipEntry
    {
        uint32_t FileIndex     = 0;       // miniz file index
        uint64_t CompSize      = 0;       // compressed byte size
        uint64_t UncompSize    = 0;       // uncompressed byte size
        uint64_t LocalHdrOfs   = 0;       // offset of local file header in archive
        uint32_t Crc32         = 0;
        bool     IsDirectory   = false;
        bool     IsCompressed  = false;   // false = stored (method 0)
    };

    // -------------------------------------------------------------------------
    // VFSZipFile
    //
    // Represents an opened (possibly not-yet-decompressed) entry.
    // On first Read(), decompresses the entire entry into m_data using the
    // parent backend's arena. Subsequent reads are memory copies.
    // Concurrent reads on the same VFSZipFile are serialized by m_decomp_mutex.
    // -------------------------------------------------------------------------
    struct VFSZipFile : IVFSFile
    {
        // Set by VFSZipBackend::Open
        const ZipEntry*         Entry        = nullptr;  // pointer into backend's map
        uint8_t*                m_data       = nullptr;  // decompressed bytes, or nullptr
        std::atomic_bool        m_decompressed{false};
        std::mutex              m_decomp_mutex;

        // Back-pointer to parent backend (needed for pread into archive fd).
        // Raw pointer: VFSZipFile lifetime is bounded by VFSZipBackend lifetime.
        struct VFSZipBackend*   m_backend    = nullptr;
        Memory::ArenaAllocator* m_arena      = nullptr;

        // IVFSFile overrides
        VFSResult<size_t>   Read(std::span<uint8_t> buf, uint64_t offset) override;
        VFSResult<size_t>   Write(std::span<const uint8_t> buf, uint64_t offset) override;
        VFSResult<uint64_t> Size() override;
        VFSResult<void>     Close() override;

        // Returns a pointer to the fully decompressed data.
        // On first call, performs decompression. Thread-safe.
        VFSResult<const uint8_t*> EnsureDecompressed();
    };

    // -------------------------------------------------------------------------
    // VFSZipBackend
    //
    // IVFSBackend backed by a single ZIP/PAK file (read-only).
    // Central directory is loaded eagerly at Initialize() time.
    // O(1) lookup by normalized VFSPath.
    //
    // For concurrent decompression, this backend keeps the archive file open
    // and uses pread() (POSIX) / ReadFile+OVERLAPPED (Windows) so multiple
    // VFSZipFile instances can decompress in parallel without a seek mutex.
    // -------------------------------------------------------------------------
    struct VFSZipBackend : IVFSBackend
    {
        // archive_path: native absolute path to the .zip / .pak file.
        // case_sensitive: if true, path lookup is case-sensitive (default false per ZIP spec).
        // arena: allocator that owns the central directory data.
        void Initialize(cstring archive_path,
                        Memory::ArenaAllocator* arena,
                        bool case_sensitive = false);

        void Shutdown();

        // IVFSBackend overrides
        [[nodiscard]] VFSResult<IVFSFile*>  Open(const VFSPath& relative_path, VFSOpenFlags flags) override;
        [[nodiscard]] VFSResult<void>       Close(IVFSFile* file) override;
        [[nodiscard]] VFSResult<VFSFileStat> Stat(const VFSPath& relative_path) override;
        [[nodiscard]] VFSResult<Containers::Array<VFSDirEntry>> List(const VFSPath& relative_dir,
                                                       Memory::ArenaAllocator* out_arena) override;
        [[nodiscard]] VFSResult<void>       CreateDir(const VFSPath&) override;   // always Fail(Unsupported)
        [[nodiscard]] VFSResult<void>       Remove(const VFSPath&) override;       // always Fail(Unsupported)
        [[nodiscard]] VFSResult<void>       Rename(const VFSPath&, const VFSPath&) override; // Fail(Unsupported)
        VFSBackendCaps        Capabilities() const override;         // Read | List | MemoryMap

        // Used by VFSZipFile::EnsureDecompressed to read raw compressed bytes.
        // offset is absolute byte offset into the archive file.
        [[nodiscard]] VFSResult<size_t> ReadArchiveBytes(void* buf, size_t count, uint64_t offset) const;

    private:
        // Normalize a ZIP entry filename to the same format used by VFSPath.
        // Strips leading "./" and ensures no trailing slash for files.
        static void NormalizeZipEntryName(const char* raw, char* out, size_t out_size);

        // If !m_case_sensitive, lower-cases the VFSPath buffer before lookup.
        void MaybeLower(const VFSPath& path, char* out_buf) const;

        // Central directory: maps normalized VFSPath buffer -> ZipEntry
        Containers::UnorderedHashMap<uint64_t /*path hash*/, ZipEntry> m_central_dir;

        // Directory tree cache: maps normalized dir VFSPath hash -> Array<VFSDirEntry>
        Containers::UnorderedHashMap<uint64_t, Containers::Array<VFSDirEntry>> m_dir_cache;

        // We store a second map from hash to the normalized path string for List()
        // so we can reconstruct VFSPath objects from hashes.
        // Each entry is an arena-allocated C-string.
        Containers::UnorderedHashMap<uint64_t, const char*> m_hash_to_path;

        char                    m_archive_path[MAX_FILE_PATH_COUNT];
        Memory::ArenaAllocator* m_arena         = nullptr;
        bool                    m_case_sensitive = false;

        // Archive fd for concurrent pread — never seeked, only pread/ReadFile+OVERLAPPED
#if defined(_WIN32)
        HANDLE                  m_archive_handle = INVALID_HANDLE_VALUE;
#else
        int                     m_archive_fd     = -1;
#endif

        // miniz archive state — used only at Initialize() to read the central directory
        // and is closed/freed immediately after. Subsequent I/O uses the raw fd above.
        mz_zip_archive          m_mz_archive;
    };

} // namespace ZEngine::Core::VFS
```

### Implementation Notes for `VFSZipBackend`

**`Initialize(archive_path, arena, case_sensitive)`**

```
secure_strcpy(m_archive_path, MAX_FILE_PATH_COUNT, archive_path)
m_arena          = arena
m_case_sensitive = case_sensitive

// Open raw fd for concurrent reads (kept open for lifetime of backend)
#POSIX:  m_archive_fd = open(archive_path, O_RDONLY)
#Windows: m_archive_handle = CreateFileA(archive_path, GENERIC_READ, FILE_SHARE_READ, ...)

// THREAD SAFETY NOTE:
// mz_zip_archive maintains internal seek state and is NOT thread-safe for concurrent reads.
//
// In VFSZipBackend::Initialize():
//   1. Use m_mz_archive to read the central directory and build the m_entries table.
//   2. Call mz_zip_reader_end(&m_mz_archive) IMMEDIATELY after building m_entries.
//      This closes the archive handle — do not use it again after this point.
//   3. Keep m_archive_fd open (platform file descriptor) for subsequent concurrent reads.
//
// In VFSZipFile::EnsureDecompressed():
//   - Use ReadArchiveBytes(offset, size) which uses pread() / ReadFile() at an explicit
//     offset — this is thread-safe and does not require a seek.
//   - Decompress using tinfl_decompress_mem_to_mem() — pure CPU computation, thread-safe.
//   - Do NOT call mz_zip_reader_extract_*() after Initialize() completes.

// Use miniz to read the central directory
memset(&m_mz_archive, 0, sizeof(m_mz_archive))
if not mz_zip_reader_init_file(&m_mz_archive, archive_path, 0):
    ZENGINE_CORE_ERROR("VFSZipBackend: failed to open archive {}", archive_path)
    return  // backend is non-functional; Capabilities() will return 0

uint32_t num_files = mz_zip_reader_get_num_files(&m_mz_archive)

// Pre-size the hash maps
m_central_dir.init(arena, num_files * 2)
m_dir_cache.init(arena, 64)
m_hash_to_path.init(arena, num_files * 2)

for i in [0, num_files):
    mz_zip_archive_file_stat stat
    if not mz_zip_reader_file_stat(&m_mz_archive, i, &stat): continue

    // Normalize the filename
    char normalized[MAX_FILE_PATH_COUNT]
    NormalizeZipEntryName(stat.m_filename, normalized, MAX_FILE_PATH_COUNT)

    if !m_case_sensitive:
        tolower_inplace(normalized)

    // Compute hash of normalized path
    uint64_t h = rapidhash(normalized, strlen(normalized))

    ZipEntry entry
    entry.FileIndex   = i
    entry.CompSize    = stat.m_comp_size
    entry.UncompSize  = stat.m_uncomp_size
    entry.LocalHdrOfs = stat.m_local_header_ofs
    entry.Crc32       = stat.m_crc32   // if exposed by miniz v1.15; else 0
    entry.IsDirectory = mz_zip_reader_is_file_a_directory(&m_mz_archive, i)
    entry.IsCompressed = (stat.m_comp_size != stat.m_uncomp_size) || (stat.m_comp_size == 0 && !entry.IsDirectory)

    m_central_dir.insert(h, entry)

    // Store path string for reconstruction
    char* path_copy = ZPushString(arena, strlen(normalized) + 1)
    secure_strcpy(path_copy, strlen(normalized) + 1, normalized)
    m_hash_to_path.insert(h, path_copy)

// Build directory tree cache
// For each non-directory entry, walk up its parent path chain and ensure
// each parent directory is represented in m_dir_cache
for each (h, entry) in m_central_dir:
    if entry.IsDirectory: continue
    path_str = m_hash_to_path.find(h)   // points to normalized path string
    // Walk parent directories
    char parent[MAX_FILE_PATH_COUNT]
    GetParentPath(*path_str, parent)
    while strlen(parent) > 0:
        uint64_t parent_h = rapidhash(parent, strlen(parent))
        if not m_dir_cache.contains(parent_h):
            Array<VFSDirEntry> arr
            arr.init(arena, 8)
            m_dir_cache.insert(parent_h, arr)
            // Also ensure parent is in central_dir as a synthetic dir entry
            if not m_central_dir.contains(parent_h):
                ZipEntry dir_entry; dir_entry.IsDirectory = true
                m_central_dir.insert(parent_h, dir_entry)
        // Add this file's VFSDirEntry to its immediate parent's Array
        // (only if parent == immediate_parent of this file)
        char immediate_parent[MAX_FILE_PATH_COUNT]
        GetParentPath(*path_str, immediate_parent)
        if strcmp(immediate_parent, parent) == 0:
            VFSDirEntry de; // ... build from entry
            m_dir_cache[parent_h].push(de)
            break
        GetParentPath(parent, parent)

// Close the miniz reader; keep m_archive_fd open
mz_zip_reader_end(&m_mz_archive)
```

**`NormalizeZipEntryName(raw, out, out_size)`**

```
// 1. Strip leading "./"
src = raw
if src[0] == '.' && src[1] == '/': src += 2

// 2. Replace backslashes with forward slashes
secure_strcpy(out, out_size, src)
for each char c in out: if c == '\\': c = '/'

// 3. Strip trailing slash (for directories recorded as "dir/")
len = strlen(out)
if len > 0 && out[len-1] == '/': out[len-1] = '\0'
```

**`Open(relative_path, flags)`**

```
if flags & (VFSOpenFlags::Write | VFSOpenFlags::Append | VFSOpenFlags::Create):
    return Fail(VFSError::PermissionDenied)

char lookup[MAX_FILE_PATH_COUNT]
MaybeLower(relative_path, lookup)
uint64_t h = rapidhash(lookup, strlen(lookup))

ZipEntry* entry = m_central_dir.find(h)
if entry == nullptr: return Fail(VFSError::NotFound)
if entry->IsDirectory: return Fail(VFSError::NotAFile)

VFSZipFile* f = ZPushStructCtor(m_arena, VFSZipFile)
f->Entry    = entry
f->m_backend = this
f->m_arena  = m_arena
return Ok(static_cast<IVFSFile*>(f))
```

**`VFSZipFile::EnsureDecompressed()`**

```
if m_decompressed.load(memory_order_acquire):
    return Ok(m_data)

unique_lock lock(m_decomp_mutex)
// Double-check under lock
if m_decompressed.load(memory_order_relaxed):
    return Ok(m_data)

// Allocate output buffer
m_data = ZPushArray(m_arena, uint8_t, Entry->UncompSize)
if m_data == nullptr: return Fail(VFSError::OutOfMemory)

if not Entry->IsCompressed:
    // Method 0 (stored): raw bytes at (local header offset + local header size)
    // Local header size = 30 + filename_len + extra_len (must be read from local header)
    // Read the local header to determine skip offset
    uint8_t local_hdr[30]
    m_backend->ReadArchiveBytes(local_hdr, 30, Entry->LocalHdrOfs)
    uint16_t fname_len  = LE16(&local_hdr[26])
    uint16_t extra_len  = LE16(&local_hdr[28])
    uint64_t data_offset = Entry->LocalHdrOfs + 30 + fname_len + extra_len
    auto r = m_backend->ReadArchiveBytes(m_data, Entry->UncompSize, data_offset)
    if r.Failed(): return Fail(VFSError::IOError)
else:
    // Compressed: read compressed data then inflate with tinfl (miniz)
    // Allocate compressed scratch from arena (temporary)
    auto scratch = ZGetScratch(m_arena)
    uint8_t* comp = ZPushArray(scratch.Arena, uint8_t, Entry->CompSize)

    uint8_t local_hdr[30]
    m_backend->ReadArchiveBytes(local_hdr, 30, Entry->LocalHdrOfs)
    uint16_t fname_len  = LE16(&local_hdr[26])
    uint16_t extra_len  = LE16(&local_hdr[28])
    uint64_t data_offset = Entry->LocalHdrOfs + 30 + fname_len + extra_len
    auto r = m_backend->ReadArchiveBytes(comp, Entry->CompSize, data_offset)
    if r.Failed(): ZReleaseScratch(scratch); return Fail(VFSError::IOError)

    // Inflate
    size_t out_size = Entry->UncompSize
    int status = tinfl_decompress_mem_to_mem(m_data, out_size,
                                              comp, Entry->CompSize,
                                              TINFL_FLAG_PARSE_ZLIB_HEADER)
    ZReleaseScratch(scratch)
    if status == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED:
        return Fail(VFSError::Corrupted)

m_decompressed.store(true, memory_order_release)
return Ok(static_cast<const uint8_t*>(m_data))
```

Note: `tinfl_decompress_mem_to_mem` is available in miniz v1.15. Check the exact function signature in the vendored header. Raw DEFLATE streams stored in ZIP use `TINFL_FLAG_PARSE_ZLIB_HEADER = 0` (no zlib wrapper); set the flag to `0` not `TINFL_FLAG_PARSE_ZLIB_HEADER`.

**`VFSZipFile::Read(buf, offset)`**

```
r = EnsureDecompressed()
if r.Failed(): return Fail(r.Error())
const uint8_t* data = r.Value()

if offset >= Entry->UncompSize:
    return Ok(0UZ)    // past EOF

size_t available = Entry->UncompSize - offset
size_t to_copy   = min(available, buf.size())
secure_memcpy(buf.data(), buf.size(), data + offset, to_copy)
return Ok(to_copy)
```

**`ReadArchiveBytes(buf, count, offset)`** (POSIX):

```
ssize_t n = pread(m_archive_fd, buf, count, (off_t)offset)
if n < 0: return Fail(VFSError::IOError)
return Ok((size_t)n)
```

**`List(relative_dir, out_arena)`**

```
char lookup[MAX_FILE_PATH_COUNT]
MaybeLower(relative_dir, lookup)
uint64_t h = rapidhash(lookup, strlen(lookup))

auto* entries = m_dir_cache.find(h)
if entries == nullptr: return Fail(VFSError::NotFound)

// Copy into out_arena (m_dir_cache entries live in the backend's own arena)
Array<VFSDirEntry> result
result.init(out_arena, entries->size())
for each entry in *entries:
    result.push(entry)
return Ok(result)
```

**`MaybeLower(path, out_buf)`**

```
secure_strcpy(out_buf, MAX_FILE_PATH_COUNT, path.Buffer())
if not m_case_sensitive:
    for each char c in out_buf: c = tolower(c)
```

---

## 6. Overlay List Merge Algorithm

This is the core of `VFSContext::List()` and is critical for the editor's overlay asset system (e.g., a mod ZIP overlaid on top of the base disk directory).

**Full prose description:**

1. `VFSMountTable::ResolveAll(dir_path)` returns a list of `(backend, relative_path)` pairs in **descending priority order** (highest first). This ordering is guaranteed because `m_mounts` is kept sorted by `Mount()`.

2. `VFSContext::List()` allocates a scratch arena and a `UnorderedHashMap<uint64_t, bool> seen` keyed on the FNV-1a hash of each directory entry's filename component (not full path). The FNV-1a hash comes directly from `VFSPath::Hash()`.

3. For each `(backend, relative_path)` pair, in order:
   a. Call `backend->List(relative_path, scratch_arena)`.
   b. If the backend returns `NotFound` or `Unsupported`, skip it without error.
   c. For each `VFSDirEntry` returned:
      - Extract the filename component hash from `entry.Path.Hash()`.
      - If `seen.contains(hash)`: discard this entry (lower-priority backend loses).
      - Otherwise: `seen.insert(hash, true)` and `result.push(entry)`.

4. Release scratch arena. Return `result` (allocated on caller's `out_arena`).

**Collision resolution rule:** The entry from the highest-priority backend always wins. If both a disk backend and a ZIP backend provide `textures/grass.png`, and the disk backend has priority 10 vs ZIP priority 5, then the disk file is used.

**Edge case — directory vs file collision:** If one backend provides a file and another provides a directory with the same name, the higher-priority backend's type wins entirely. This is intentional: an overlay that replaces a directory with a file (or vice versa) fully hides the lower-priority entry.

**Deduplication granularity:** Deduplication is by the **filename within the queried directory**, not by full path. This is correct: `List("/assets/textures")` deduplicates by the filenames of direct children, not their full absolute paths.

---

## 7. Implementation Notes for Non-Trivial Methods (Summary Table)

| Method | Key Algorithm | Complexity |
|---|---|---|
| `VFSMountTable::Mount` | Linear scan + insertion sort via push+rotate | O(n) |
| `VFSMountTable::Resolve` | Linear scan with prefix check | O(n) |
| `VFSMountTable::ResolveAll` | Full scan | O(n) |
| `VFSDiskBackend::ResolveNativePath` | snprintf + realpath/GetFullPathNameA + prefix check | O(path_len) |
| `VFSZipBackend::Initialize` | Full central directory scan + hash map build + dir tree construction | O(N log N) at startup |
| `VFSZipBackend::Open` | Hash lookup | O(1) amortized |
| `VFSZipFile::EnsureDecompressed` | Double-checked locking + tinfl inflate | O(compressed_size) once |
| `VFSContext::List` | ResolveAll + per-backend List + hash-dedup | O(backends * entries_per_dir) |

---

## 8. Sandbox Check Algorithm — Detailed Specification

The sandbox check in `VFSDiskBackend::ResolveNativePath` prevents path traversal attacks (e.g., `../../etc/passwd`).

**Step 1 — Compose candidate path.**
Concatenate `m_native_root`, `/`, and `relative.Buffer()` using `snprintf`. If the resulting string length equals or exceeds `MAX_FILE_PATH_COUNT`, return `false` (path too long, sandbox violation by exhaustion).

**Step 2 — Canonicalize.**

- **POSIX, path exists on disk:** Use `realpath(candidate, canonical)`. This resolves symlinks and `..` segments. If `realpath` fails (e.g., path does not exist yet), fall back to the manual normalizer.
- **POSIX, path does not exist:** Manually resolve `..` segments without filesystem calls. Walk each path component. On `..`, pop the last component from an output buffer. On `.`, skip. For all other components, append. This is safe against `..` chains even without filesystem access.
- **Windows:** `GetFullPathNameA` resolves `..` without filesystem access. It does not resolve symlinks but that is acceptable on Windows for the game editor use case. For symlink protection on Windows, a `CreateFile` + `GetFinalPathNameByHandle` check can be added as a later hardening step.

**Step 3 — Containment check.** The canonical path must satisfy both:
1. `strncmp(canonical, m_native_root, m_native_root_len) == 0`
2. `canonical[m_native_root_len] == '\0' || canonical[m_native_root_len] == '/'`

Condition 2 prevents a false positive where `m_native_root = "/assets"` passes for a path like `/assetstore/secret.png` (which would satisfy only condition 1).

**On failure:** Log via `ZENGINE_CORE_WARN(...)` and return `false`. The calling method maps `false` to `VFSError::PermissionDenied`.

---

## 9. Unit Tests

NOTE: All test bodies marked with TODO must be implemented before Phase 2 (ECS+VFS)
ships. Tests with empty bodies will not be accepted in code review.
The following pattern must be used for each test:
  1. Set up a concrete VFSDiskContext or VFSMemoryBackend
  2. Register the system under test
  3. Exercise the specific code path
  4. Assert all invariants via EXPECT_* macros
  5. Verify no ASAN/UBSAN errors under sanitizer builds

**File:** `ZEngine/tests/VFS/vfs_mount_test.cpp`

```cpp
// Test framework: Google Test (gtest), same pattern as existing tests

class VFSMountTableTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        allocator.Initialize(ZKilo(64));
        table.Initialize(&allocator, 8);
    }
    void TearDown() override { allocator.Shutdown(); }
    ArenaAllocator allocator;
    VFSMountTable  table;
};

TEST_F(VFSMountTableTest, MountAndResolve_SingleMount)
{
    // Mount a mock backend at "/assets" priority 0
    // Parse VFSPath "/assets/textures/foo.png"
    // Resolve must return backend + relative "textures/foo.png"
}

TEST_F(VFSMountTableTest, Mount_DuplicateRootFails)
{
    // Mount same root twice -> second Mount returns AlreadyExists
}

TEST_F(VFSMountTableTest, Unmount_ThenResolve_ReturnsNotFound)
{
    // Mount, Unmount, then Resolve returns NotFound
}

TEST_F(VFSMountTableTest, PriorityOrdering_HighestWins)
{
    // Mount backend A at "/data" priority 5
    // Mount backend B at "/data" -- wait, same root is forbidden
    // Instead: mount A at "/" priority 5 and B at "/" priority 10
    // Resolve should return B
    // (Use two backends with different prefix-root mounts to test priority)
    //
    // Correct test: mount "/" priority 0 with backend A,
    //               mount "/" priority 10 with backend B
    // Resolve("/foo.txt") must return backend B
    // (Cannot mount same root twice -- use prefix hierarchy instead)
    //
    // Actually: test with "/game" prio 5 and "/game/dlc" prio 10.
    // Resolve("/game/dlc/sound.wav") must return backend at "/game/dlc" (longer + higher prio).
}

TEST_F(VFSMountTableTest, ResolveAll_ReturnsBothMatching)
{
    // Mount "/" priority 0 (backend A) and "/content" priority 5 (backend B)
    // ResolveAll("/content") returns both A and B, B first (higher priority)
}

TEST_F(VFSMountTableTest, StripPrefix_RootMount)
{
    // Mount "/" priority 0
    // Resolve("/foo/bar.txt") -> relative path is "foo/bar.txt"
}

TEST_F(VFSMountTableTest, PrefixCheck_DoesNotMatchPartialComponent)
{
    // Mount "/assets" priority 0
    // Resolve("/assetstore/file.txt") must return NotFound (no mount matches)
}
```

**File:** `ZEngine/tests/VFS/vfs_disk_test.cpp`

```cpp
class VFSDiskBackendTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        allocator.Initialize(ZMega(1));
        // Create a temp directory with known files for testing
        // Use std::filesystem::temp_directory_path() to find a writable location
        // Create: <tmpdir>/vfs_test/hello.txt containing "hello world"
        //         <tmpdir>/vfs_test/subdir/nested.txt containing "nested"
    }
    void TearDown() override
    {
        // Remove temp files
        allocator.Shutdown();
    }
    ArenaAllocator  allocator;
    VFSDiskBackend  backend;
};

TEST_F(VFSDiskBackendTest, Open_ExistingFile_Succeeds)
{
    // backend.Initialize(tmpdir, VFSBackendCaps::Read, &allocator)
    // Open(VFSPath::Parse("hello.txt"), VFSOpenFlags::Read)
    // Expect Ok, read returns "hello world"
}

TEST_F(VFSDiskBackendTest, Open_NonExistentFile_ReturnsNotFound)
{
    // Open("ghost.txt") -> Fail(VFSError::NotFound)
}

TEST_F(VFSDiskBackendTest, SandboxCheck_PathTraversal_Blocked)
{
    // Open(VFSPath from "../../../etc/passwd") -> Fail(VFSError::PermissionDenied)
    // Note: VFSPath::Parse rejects ".." so this tests the backend's own check
    // Use a crafted buffer to bypass VFSPath normalization if needed,
    // or rely on the fact that Parse already blocks it (document that behavior).
}

TEST_F(VFSDiskBackendTest, ReadOnly_WriteAttempt_Blocked)
{
    // Initialize with Read-only caps
    // Open("hello.txt", VFSOpenFlags::Write) -> Fail(VFSError::PermissionDenied)
}

TEST_F(VFSDiskBackendTest, List_ReturnsDirectChildren)
{
    // List(".") or List("") returns hello.txt and subdir/
    // Does not recursively list subdir contents
}

TEST_F(VFSDiskBackendTest, StatFile_CorrectSize)
{
    // Stat("hello.txt").SizeBytes == strlen("hello world")
}

TEST_F(VFSDiskBackendTest, MemoryMap_ReadsCorrectBytes)
{
    // Open, MemoryMap, verify first N bytes match expected content
}
```

**File:** `ZEngine/tests/VFS/vfs_zip_test.cpp`

```cpp
// Requires a small test ZIP created at build time or embedded as a
// compile-time byte array using xxd / bin2c.
// Recommended: create test_archive.zip containing:
//   hello.txt        -> "hello from zip"
//   subdir/world.txt -> "world"
//   emptydir/        -> directory entry

class VFSZipBackendTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        allocator.Initialize(ZMega(2));
        // Write the test ZIP to a temp path, or use a pre-committed test asset
        backend.Initialize("<path_to_test_archive.zip>", &allocator, /*case_sensitive=*/false);
    }
    void TearDown() override
    {
        backend.Shutdown();
        allocator.Shutdown();
    }
    ArenaAllocator  allocator;
    VFSZipBackend   backend;
};

TEST_F(VFSZipBackendTest, Open_KnownFile_Succeeds)
{
    // Open(VFSPath("hello.txt"), VFSOpenFlags::Read)
    // Read full content, compare to "hello from zip"
}

TEST_F(VFSZipBackendTest, Open_CaseInsensitive_Lookup)
{
    // Open("HELLO.TXT") must succeed when case_sensitive=false
}

TEST_F(VFSZipBackendTest, Open_NonExistentFile_ReturnsNotFound)
{
    // Open("ghost.txt") -> Fail(VFSError::NotFound)
}

TEST_F(VFSZipBackendTest, Open_WriteAttempt_ReturnsPermissionDenied)
{
    // Open("hello.txt", VFSOpenFlags::Write) -> Fail(VFSError::PermissionDenied)
}

TEST_F(VFSZipBackendTest, List_RootDirectory)
{
    // List("") or List(".") returns hello.txt and subdir/ and emptydir/
}

TEST_F(VFSZipBackendTest, List_Subdir)
{
    // List("subdir") returns world.txt
}

TEST_F(VFSZipBackendTest, ConcurrentReads_SameFile)
{
    // Open the same file twice, launch two threads, each reads from different offsets
    // Both reads must return correct data (tests that decompressed cache is correct)
}

TEST_F(VFSZipBackendTest, Stat_ExistingFile_CorrectUncompSize)
{
    // Stat("hello.txt").SizeBytes == strlen("hello from zip")
}

// Integration: VFSContext with mixed ZIP + Disk overlay
TEST(VFSContextOverlayTest, ZipOverridesLowerPriorityDisk)
{
    // Mount disk backend at "/" priority 0 (has base/hello.txt = "disk version")
    // Mount ZIP backend at "/" priority 10 (has hello.txt = "zip version")
    // Open("/hello.txt") -> reads "zip version" from ZIP backend
    // Confirm disk backend's version is hidden
}
```

---

## 10. CMakeLists.txt Additions — Explicit Changes

**In `ZEngine/ZEngine/CMakeLists.txt`** — add to `target_include_directories`:

```cmake
./Core/VFS
${PROJECT_SOURCE_DIR}/../__externals/miniz
```

Note: miniz is already present at `__externals/assimp/contrib/zip/src/miniz.h`. The cleanest approach is to add that existing path rather than copying the file, to avoid duplication:

```cmake
${PROJECT_SOURCE_DIR}/../__externals/assimp/contrib/zip/src
```

However, since the spec requires a stable, non-assimp-internal include path, the engineer should copy the file to `__externals/miniz/miniz.h` and use the path above. This is a one-time file copy.

**In `VFSZipBackend.cpp`**, define the implementation macro before including miniz:

```cpp
#define MINIZ_IMPLEMENTATION
#include <miniz/miniz.h>
```

No other `.cpp` file should include miniz without guarding against double-defining MINIZ_IMPLEMENTATION.

---

## 11. Deliverables Checklist

- [ ] `ZEngine/ZEngine/Core/VFS/VFSMountTable.h` — struct declarations with Doxygen comments
- [ ] `ZEngine/ZEngine/Core/VFS/VFSMountTable.cpp` — Mount, Unmount, Resolve, ResolveAll, IsPrefixOf, StripPrefix
- [ ] `ZEngine/ZEngine/Core/VFS/VFSContext.h` — struct declaration
- [ ] `ZEngine/ZEngine/Core/VFS/VFSContext.cpp` — Open, List, Stat, Exists, Mount, Unmount, CreateDir, Remove, Rename, ResolveWritable
- [ ] `ZEngine/ZEngine/Core/VFS/VFSDiskBackend.h` — VFSDiskFile + VFSDiskBackend declarations with platform ifdefs
- [ ] `ZEngine/ZEngine/Core/VFS/VFSDiskBackend.cpp` — Initialize, Open, Read/Write (pread/OVERLAPPED), Stat, List (std::filesystem::directory_iterator), CreateDir, Remove, Rename, MemoryMap/Unmap, ResolveNativePath (sandbox check), ProbeCaseSensitivity
- [ ] `ZEngine/ZEngine/Core/VFS/VFSZipBackend.h` — ZipEntry + VFSZipFile + VFSZipBackend declarations
- [ ] `ZEngine/ZEngine/Core/VFS/VFSZipBackend.cpp` — Initialize (central dir scan), Open, Stat, List (from m_dir_cache), VFSZipFile::EnsureDecompressed (double-checked locking + tinfl), ReadArchiveBytes (pread/OVERLAPPED), Shutdown, NormalizeZipEntryName, MaybeLower
- [ ] `ZEngine/__externals/miniz/miniz.h` — copied from assimp contrib; do not modify
- [ ] `ZEngine/ZEngine/CMakeLists.txt` — updated `target_include_directories` with `./Core/VFS` and the miniz path
- [ ] `ZEngine/tests/VFS/vfs_mount_test.cpp` — 6 test cases covering mount, unmount, priority, resolve-all, strip-prefix, and no-partial-component-match
- [ ] `ZEngine/tests/VFS/vfs_disk_test.cpp` — 6 test cases covering open, not-found, sandbox traversal block, read-only caps, list, stat, mmap
- [ ] `ZEngine/tests/VFS/vfs_zip_test.cpp` — 7 test cases covering open, case-insensitive, not-found, write-blocked, list-root, list-subdir, concurrent-reads, and one integration overlay test
- [ ] `ZEngine/tests/CMakeLists.txt` — add `VFS/*.cpp` to the GLOB pattern
- [ ] All new `.h` files included in the pch if needed (confirm against `pch.h`; currently `<shared_mutex>` is already included)
- [ ] Verify: no `std::string` in any path-facing type; no `new`/`delete` outside placement-new via `ZPushStructCtor`; all arena allocation via `ZPush*` macros; all asserts via `ZENGINE_VALIDATE_ASSERT`; all logging via `ZENGINE_CORE_WARN` / `ZENGINE_CORE_ERROR`

---

## Important Cross-Cutting Notes for the Implementer

**Hash key type for `UnorderedHashMap` in `VFSZipBackend`:**
The key is `uint64_t` (the rapidhash of the normalized path string), not `VFSPath`. This is because `VFSPath` is a value type with a 256-byte buffer, which would make the hashmap entries very large. Using the hash directly requires that hash collisions be treated as non-collisions (i.e., two different paths that happen to hash the same would incorrectly match). In practice this is safe for a game asset system — add a comment documenting this trade-off. If the engineer wants collision safety, the key can be `VFSPath` directly (the `UnorderedHashMap` already handles struct keys via `rapidhash(&key, sizeof(K))`).

**`VFSPath` equality operator:**
The Ticket 1 spec says VFSPath uses FNV-1a hash. The `UnorderedHashMap` hashes keys via `rapidhash(&key, sizeof(K))`, which for a `VFSPath` value will hash the entire 256-byte buffer. This is valid only if `VFSPath::operator==` compares the buffer content. Confirm with the Ticket 1 implementation. If needed, the engineer can specialize the hash or use `const char*` keys pointing into arena-allocated strings.

**`VFSDiskFile` lifetime:**
`VFSDiskFile` is allocated on the `VFSDiskBackend`'s arena. The arena never reclaims individual allocations. This means closing a file (`Close()`) closes the OS handle but does not free the struct memory until the arena itself is cleared. This is by design: the arena is cleared at engine shutdown. The engineer must call `Close()` from `VFSDiskFile::~VFSDiskFile()` to guarantee the OS handle is always released even if the caller forgets.

**`VFSZipFile` decompression buffer:**
The decompressed bytes are allocated in `m_arena` (the backend's arena) and live for the backend's lifetime. If the same file is opened multiple times via `VFSZipBackend::Open()`, multiple `VFSZipFile` instances share the `ZipEntry*` pointer but each has its own `m_data` allocation. A future optimization (beyond Ticket 2) would be a per-entry decompressed cache so the second Open can share the already-decompressed buffer.

**`tinfl` vs `mz_zip_reader_extract`:**
In miniz v1.15, `mz_zip_reader_extract_to_mem` requires the miniz archive to remain open and performs internal seeks. To keep the archive fd as a no-seek pread handle, use `tinfl_decompress_mem_to_mem` directly after reading the compressed bytes via `ReadArchiveBytes`. This is the correct approach and avoids the seek mutex.

**`mz_zip_archive_file_stat.m_crc32`:**
In the miniz v1.15 header present at `__externals/assimp/contrib/zip/src/miniz.h`, confirm that `mz_zip_archive_file_stat` includes `m_crc32`. If it is absent (the struct in the found header is 6870 lines, suggesting a modified version), omit the `Crc32` field from `ZipEntry` and skip CRC validation in `EnsureDecompressed`.

---

### Critical Files for Implementation

- `ZEngine/Core/Containers/UnorderedHashMap.h`
- `ZEngine/Core/Memory/Allocator.h`
- `ZEngine/ZEngineDef.h`
- `ZEngine/CMakeLists.txt`
- `__externals/assimp/contrib/zip/src/miniz.h`