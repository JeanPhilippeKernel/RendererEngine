# VFS Path Abstraction — Architecture Design

**Priority:** P1 — Implement in parallel with ECS core (Phase 6 of migration-plan.md)  
**Status:** Implemented  
**Module:** `ZEngine/Core/VFS/`  
**Standard:** C++20  
**Estimated effort:** 2–3 days (1 engineer)  
**Blocks:** `vfs-ticket2`, `vfs-ticket3`, `vfs-ticket4`, `vfs-ticket5`, `vfs-ticket6`, `import-pipeline.md`

---

## Table of Contents

1. [Motivation](#1-motivation)
2. [Scope of This Ticket](#2-scope-of-this-ticket)
3. [Directory Layout](#3-directory-layout)
4. [System Architecture](#4-system-architecture)
5. [Component Specifications](#5-component-specifications)
   - 5.1 [VFSError.h](#51-vfserrorh)
   - 5.2 [VFSPath.h / VFSPath.cpp](#52-vfspathh--vfspathecpp)
   - 5.3 [IVFSFile.h](#53-ivfsfileh)
   - 5.4 [IVFSBackend.h](#54-ivfsbackendh)
   - 5.5 [IVFSContext.h / VFSDiskContext](#55-ivfscontexth--vfsdiskcontext)
6. [Normalization Rules](#6-normalization-rules)
7. [Call Site Migration](#7-call-site-migration)
8. [Unit Tests](#8-unit-tests)
9. [Deliverables Checklist](#9-deliverables-checklist)
10. [Out of Scope](#10-out-of-scope)
11. [Future Tickets](#11-future-tickets)

---

## 1. Motivation

Every file access in ZEngine currently uses raw `std::filesystem::path` at the call site —
`ProjectViewUIComponent`, `AssetManager`, `ShaderReader`, and others. This creates four concrete problems:

| Problem | Where it hurts today |
|---|---|
| Paths are OS-native (backslash on Windows, slash on POSIX) with no enforcement | `ProjectViewUIComponent::Initialize`, `AssetManager::CurrentWorkingSpacePath` |
| No cross-platform normalization — a path that works on macOS silently breaks on Windows | Anywhere a path string is constructed via `fmt::format` with a separator |
| No abstraction boundary — swapping disk for ZIP/PAK requires touching every call site | Future packaging / mod support |
| `ProjectViewUIComponent` scans `std::filesystem::directory_iterator` on the render thread | `ProjectViewUIComponent.cpp:106`, `ProjectViewUIComponent.cpp:183` |

This ticket delivers the **path value type and file I/O interface layer only**. It introduces zero
behaviour change — the concrete implementation (`VFSDiskContext`) forwards directly to
`std::filesystem`, identically to what exists today. The mount table, backends, and async scanner
plug on top in subsequent tickets without requiring call sites to change again.

---

## 2. Scope of This Ticket

**In scope:**
- `VFSPath` — normalized, immutable path value type
- `VFSResult<T>` — error propagation without exceptions
- `IVFSFile` — file handle interface (read, write, stat, memory-map)
- `IVFSBackend` — backend interface (disk, zip, memory — declared, not implemented here)
- `IVFSContext` — the top-level VFS interface
- `VFSDiskContext` — the only concrete implementation in this ticket; passthrough to `std::filesystem`
- Migration of two existing call sites as proof of concept
- Unit tests for `VFSPath`

**Not in scope:** mount table, ZIP/PAK backend, memory backend, async I/O, file watching, scanner.

---

## 3. Directory Layout

```
ZEngine/ZEngine/Core/VFS/
    VFSError.h              ← VFSError enum + VFSResult<T>
    VFSPath.h               ← VFSPath declaration
    VFSPath.cpp             ← Parse, FromNative, BuildComponents, operators
    IVFSFile.h              ← IVFSFile interface + VFSOpenFlags + VFSFileStat
    IVFSBackend.h           ← IVFSBackend interface + VFSDirEntry + VFSBackendCaps
    IVFSContext.h           ← IVFSContext interface + VFSDiskContext declaration
    VFSDiskContext.cpp      ← VFSDiskContext implementation

ZEngine/tests/
    test_vfspath.cpp        ← VFSPath unit tests
```

---

## 4. System Architecture

```
                    ┌─────────────────────────────────┐
                    │         Engine call sites         │
                    │  AssetManager  ProjectView  ...   │
                    └────────────────┬────────────────┘
                                     │ IVFSContext*
                    ┌────────────────▼────────────────┐
                    │           IVFSContext             │
                    │  Open  Close  Stat  List  Exists  │
                    │  Mount / Unmount (stub this ticket│
                    │  full impl: next ticket)          │
                    └────────────────┬────────────────┘
                                     │
               ┌─────────────────────▼──────────────────────┐
               │                IVFSBackend                   │
               │  Open  Close  Stat  List  Exists             │
               │  CreateDir  Remove  Rename                   │
               └──────┬──────────────┬────────────────┬──────┘
                      │              │                 │
           ┌──────────▼──┐  ┌────────▼──────┐  ┌──────▼────────┐
           │VFSDiskBackend│  │VFSZipBackend  │  │VFSMemoryBackend│
           │(this ticket) │  │(later ticket) │  │(later ticket)  │
           └─────────────┘  └───────────────┘  └───────────────┘

All paths flowing through the system are VFSPath — normalized, forward-slash,
UTF-8, fixed-size (no heap allocation), immutable after construction.
```

### Data flow for a single Open call (this ticket)

```
caller: ctx->Open(VFSPath::Parse("/textures/rock.png").Value(), VFSOpenFlags::Read)
  │
  ▼
VFSDiskContext::Open(path, flags)
  │  Calls ToNativePath(path) → "/project/assets/textures/rock.png" (OS path)
  │  Validates result is still under m_native_root (sandbox check)
  ▼
std::filesystem::path (fopen / CreateFile / open)
  │
  ▼
VFSDiskFile (concrete IVFSFile) returned to caller
```

---

## 5. Component Specifications

### 5.1 `VFSError.h`

Write this first — every other component depends on `VFSResult<T>`.

```cpp
// ZEngine/Core/VFS/VFSError.h
#pragma once
#include <ZEngineDef.h>
#include <cstdint>

namespace ZEngine::Core::VFS
{
    enum class VFSError : uint32_t
    {
        OK               = 0,
        NotFound         = 1,
        PermissionDenied = 2,
        AlreadyExists    = 3,
        NotADirectory    = 4,
        NotAFile         = 5,
        InvalidPath      = 6,
        Unsupported      = 7,
        IOError          = 8,
        OutOfMemory      = 9,
        Corrupted        = 10,
        Cancelled        = 11,
    };

    // Lightweight result type — no exceptions, no heap allocation.
    //
    // Usage:
    //   VFSResult<uint64_t> r = file->Size();
    //   if (r.Succeeded()) { use r.Value(); }
    //   else               { log r.Error(); }
    template <typename T>
    struct VFSResult
    {
        static VFSResult Ok(T value)
        {
            VFSResult r;
            r.m_value = std::move(value);
            r.m_error = VFSError::OK;
            return r;
        }

        static VFSResult Fail(VFSError error)
        {
            ZENGINE_VALIDATE_ASSERT(error != VFSError::OK, "Use Ok() to construct success")
            VFSResult r;
            r.m_error = error;
            return r;
        }

        bool Succeeded() const
        {
            return m_error == VFSError::OK;
        }
        bool Failed() const
        {
            return m_error != VFSError::OK;
        }
        VFSError Error() const
        {
            return m_error;
        }

        T& Value()
        {
            ZENGINE_VALIDATE_ASSERT(Succeeded(), "Accessing value of a failed VFSResult")
            return m_value;
        }
        const T& Value() const
        {
            ZENGINE_VALIDATE_ASSERT(Succeeded(), "Accessing value of a failed VFSResult")
            return m_value;
        }

    private:
        T        m_value = {};
        VFSError m_error = VFSError::OK;
    };

    // Void specialisation for operations that succeed or fail with no return value
    template <>
    struct VFSResult<void>
    {
        static VFSResult Ok()
        {
            VFSResult r;
            r.m_error = VFSError::OK;
            return r;
        }
        static VFSResult Fail(VFSError e)
        {
            VFSResult r;
            r.m_error = e;
            return r;
        }

        bool Succeeded() const
        {
            return m_error == VFSError::OK;
        }
        bool Failed() const
        {
            return m_error != VFSError::OK;
        }
        VFSError Error() const
        {
            return m_error;
        }

    private:
        VFSError m_error = VFSError::OK;
    };

} // namespace ZEngine::Core::VFS
```

---

### 5.2 `VFSPath.h` / `VFSPath.cpp`

The core of this ticket. `VFSPath` is **immutable after construction**, stores a normalized
UTF-8 string with forward slashes only, fits in a fixed-size 256-byte buffer (no heap allocation),
and pre-computes an FNV-1a hash for O(1) use in hash maps.

#### Declaration (`VFSPath.h`)

```cpp
// ZEngine/Core/VFS/VFSPath.h
#pragma once
#include <Core/Memory/Allocator.h>
#include <Core/VFS/VFSError.h>
#include <ZEngineDef.h>
#include <cstdint>

namespace ZEngine::Core::VFS
{
    // Maximum path length including null terminator.
    // Matches MAX_FILE_PATH_COUNT from ZEngineDef.h.
    static constexpr size_t VFS_MAX_PATH       = MAX_FILE_PATH_COUNT; // 256
    static constexpr size_t VFS_MAX_COMPONENTS = 32;

    // A non-owning view of one path segment (e.g. "textures", "rock", ".png").
    // Always points into the owning VFSPath's internal buffer.
    struct VFSPathComponent
    {
        const char* Data   = nullptr;
        size_t      Length = 0;

        bool        Empty() const
        {
            return Length == 0 || Data == nullptr;
        }

        // Compare with a raw string
        bool Equals(cstring other) const;
    };

    // VFSPath
    //
    // Immutable, normalized, absolute VFS path value type.
    //
    //   Always begins with '/'.
    //   Never ends with '/' (except root "/").
    //   Forward-slash separator only.
    //   UTF-8 encoded.
    //   No heap allocation — 256-byte fixed buffer.
    //   Safe to copy, compare, and hash.
    //
    // Construction:
    //   VFSPath::Parse(raw)        — from any string; normalizes and validates
    //   VFSPath::FromNative(path)  — from an OS-native path string
    //   VFSPath::Root()            — the root path "/"
    struct VFSPath
    {
        // Construction

        // Normalize and validate `raw`. Accepts absolute or relative input;
        // always produces an absolute VFS path.
        // Returns InvalidPath if input cannot be normalized (e.g. root escape).
        [[nodiscard]] static VFSResult<VFSPath> Parse(cstring raw);
        [[nodiscard]] static VFSResult<VFSPath> Parse(const char* raw, size_t length);

        // Convert an OS-native path to a VFSPath.
        //   Windows : strips drive letter (e.g. "C:"), converts '\' to '/'.
        //   POSIX   : accepted as-is after normalization.
        [[nodiscard]] static VFSResult<VFSPath> FromNative(cstring native_path);

        // Returns the root path "/".
        static VFSPath            Root();

        // Default-constructed VFSPath is invalid (IsValid() == false).
        // Always use Parse(), FromNative(), or Root() to construct.
        VFSPath() = default;

        // Accessors

        cstring CStr() const
        {
            return m_buffer;
        }
        size_t Length() const
        {
            return m_length;
        }
        bool IsValid() const
        {
            return m_length > 0;
        }
        bool IsRoot() const
        {
            return m_length == 1 && m_buffer[0] == '/';
        }

        // Last path component: "/textures/rock.png" → "rock.png"
        VFSPathComponent Filename() const;

        // Filename without extension: "rock.png" → "rock"
        VFSPathComponent Stem() const;

        // Extension including dot: "rock.png" → ".png"  |  "rock" → ""
        VFSPathComponent Extension() const;

        // Parent path: "/textures/rock.png" → "/textures"
        //              "/textures"          → "/"
        VFSPath          Parent() const;

        // Component access: "/a/b/c" → components are "a", "b", "c"
        uint32_t         ComponentCount() const
        {
            return m_component_count;
        }
        VFSPathComponent   ComponentAt(uint32_t index) const;

        // Composition

        // Append a relative segment. Returns InvalidPath if result exceeds VFS_MAX_PATH.
        [[nodiscard]] VFSResult<VFSPath> Append(cstring segment) const;
        [[nodiscard]] VFSResult<VFSPath> Append(const VFSPath& other) const;

        // Operator form — asserts on failure. Use only when path is known valid
        // (e.g. compiled-in asset paths). Prefer Append() in runtime code.
        VFSPath            operator/(cstring segment) const;

        // Comparison

        // Byte-exact comparison on the normalized string.
        // Case sensitivity is a backend concern — VFSPath is always case-sensitive.
        bool               operator==(const VFSPath& other) const;
        bool               operator!=(const VFSPath& other) const;
        bool               operator<(const VFSPath& other) const; // for sorted containers

        // Returns true if `this` is a path-component-boundary prefix of `other`.
        //   "/textures".IsPrefixOf("/textures/rock.png") == true
        //   "/tex".IsPrefixOf("/textures/rock.png")      == false
        bool               IsPrefixOf(const VFSPath& other) const;

        // Conversion

        // Write the OS-native form into `out_buffer` (platform path separator).
        // Only call from OS-facing code; never in hot render/update loops.
        void               ToNative(char* out_buffer, size_t out_size) const;

        // Pre-computed FNV-1a hash. O(1). Stable for the lifetime of the object.
        uint64_t           Hash() const
        {
            return m_hash;
        }

    private:
        char     m_buffer[VFS_MAX_PATH]                  = {};
        size_t   m_length                                = 0;

        // Component offsets and lengths — views into m_buffer.
        // Built once in BuildComponents(), never modified after.
        uint16_t m_component_offsets[VFS_MAX_COMPONENTS] = {};
        uint16_t m_component_lengths[VFS_MAX_COMPONENTS] = {};
        uint32_t m_component_count                       = 0;

        uint64_t m_hash                                  = 0;

        // Called after m_buffer and m_length are written.
        // Fills m_component_offsets, m_component_lengths, m_component_count, m_hash.
        void     BuildComponents();
        uint64_t ComputeHash() const;
    };

    // Hash adapter for use in ZEngine::Core::Containers::UnorderedHashMap.
    struct VFSPathHasher
    {
        uint64_t operator()(const VFSPath& path) const
        {
            return path.Hash();
        }
    };

} // namespace ZEngine::Core::VFS
```

        ####Implementation notes(`VFSPath.cpp`)

        * *`Parse(raw)` algorithm — in
    - place,
    no allocation : **

``` 1. Copy `raw` into a local char[VFS_MAX_PATH] working buffer.2. Replace all '\\' with '/' .3. Windows : strip drive letter prefix "X:" if present.4. Collapse consecutive slashes("///") into single "/" .5. Resolve "." and ".." via a component stack : Walk components left to right."."  → skip.".." → pop last component from stack.If stack is empty → return Fail(VFSError::InvalidPath).  ← root escape other → push component onto stack.6. Reconstruct the normalized string : Write leading '/'.For each stack entry : write '/' + component.Special case: empty stack → result is "/".
7.  Strip trailing '/' unless result is "/".
8.  Validate result fits in VFS_MAX_PATH - 1 chars.
9.  Copy into m_buffer, set m_length.
10. Call BuildComponents().
```

**`BuildComponents()` algorithm:**

```
Walk m_buffer from index 1 (skip the leading '/').
On each '/' character: record that a new component begins at the next index.
Track start offset and length of each component.
Fill m_component_offsets[i] and m_component_lengths[i].
Increment m_component_count for each component found.
Compute m_hash = FNV-1a over m_buffer[0 .. m_length].
```

**FNV-1a (64-bit):**

```
hash = 14695981039346656037ULL
for each byte b in the string:
    hash ^= b
    hash *= 1099511628211ULL
```

---

### 5.3 `IVFSFile.h`

All reads and writes use explicit byte offsets (`pread`/`pwrite` semantics). There is no internal
seek cursor — this allows multiple async workers to read the same file handle concurrently
without locking.

```cpp
// ZEngine/Core/VFS/IVFSFile.h
#pragma once
#include <Core/VFS/VFSError.h>
#include <Core/VFS/VFSPath.h>
#include <cstdint>
#include <span>

namespace ZEngine::Core::VFS
{
    enum class VFSOpenFlags : uint32_t
    {
        None     = 0,
        Read     = 1 << 0,
        Write    = 1 << 1,
        Append   = 1 << 2,
        Create   = 1 << 3, // create if not exists (requires Write)
        Truncate = 1 << 4, // truncate on open    (requires Write)
    };
    inline VFSOpenFlags operator|(VFSOpenFlags a, VFSOpenFlags b)
    {
        return static_cast<VFSOpenFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline bool HasFlag(VFSOpenFlags flags, VFSOpenFlags test)
    {
        return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
    }

    struct VFSFileStat
    {
        uint64_t SizeBytes   = 0;
        int64_t  MTimeNs     = 0; // last modified, nanoseconds since Unix epoch
        bool     IsDirectory = false;
        bool     IsReadOnly  = false;
    };

    // IVFSFile
    //
    // Handle to an open file. Returned by IVFSContext::Open().
    // Closed via IVFSContext::Close() — do not delete directly.
    //
    // Offset-based I/O: every read/write takes an explicit byte offset.
    // No seek cursor — safe for concurrent reads from multiple threads.
    struct IVFSFile
    {
        virtual ~IVFSFile()                                                                                 = default;

        // Read up to `buffer.size()` bytes starting at `offset`.
        // Returns bytes actually read (may be < buffer.size() at EOF).
        [[nodiscard]] virtual VFSResult<size_t>                   Read(std::span<uint8_t> buffer, uint64_t offset)        = 0;

        // Write `buffer.size()` bytes at `offset`.
        // Returns Unsupported on read-only files.
        [[nodiscard]] virtual VFSResult<size_t>                   Write(std::span<const uint8_t> buffer, uint64_t offset) = 0;

        [[nodiscard]] virtual VFSResult<uint64_t>                 Size() const                                            = 0;
        [[nodiscard]] virtual VFSResult<VFSFileStat>              Stat() const                                            = 0;
        [[nodiscard]] virtual VFSResult<void>                     Flush()                                                 = 0;
        virtual const VFSPath&                      Path() const                                            = 0;

        // Optional zero-copy read. Returns a span directly into the backend's
        // memory (e.g. mmap region, MemoryBackend buffer).
        // Returns Unsupported if not available — caller falls back to Read().
        // The span is valid until Close() is called.
        virtual VFSResult<std::span<const uint8_t>> MemoryMap()
        {
            return VFSResult<std::span<const uint8_t>>::Fail(VFSError::Unsupported);
        }

        // Convenience: read entire file into `out_buffer` in one call.
        // Caller owns and sizes the buffer. Returns total bytes read.
        [[nodiscard]] VFSResult<size_t> ReadAll(std::span<uint8_t> out_buffer);
    };

} // namespace ZEngine::Core::VFS
```

    -- -

    ## #5.4 `IVFSBackend
        .h`

    A backend is associated with one logical mount root.All paths passed to backend methods are** relative to that root** — the `IVFSContext` strips the mount prefix before calling the backend.

```cpp
// ZEngine/Core/VFS/IVFSBackend.h
#pragma once
#include <Core/Containers/Array.h>
#include <Core/Memory/Allocator.h>
#include <Core/VFS/IVFSFile.h>
#include <Core/VFS/VFSPath.h>
#include <cstdint>

    namespace ZEngine::Core::VFS
{
    struct VFSDirEntry
    {
        VFSPath     Path        = {};
        VFSFileStat Stat        = {};
        bool        IsDirectory = false;
    };

    enum class VFSBackendCaps : uint32_t
    {
        None      = 0,
        Read      = 1 << 0,
        Write     = 1 << 1,
        List      = 1 << 2,
        MemoryMap = 1 << 3,
        Watch     = 1 << 4,
    };
    inline VFSBackendCaps operator|(VFSBackendCaps a, VFSBackendCaps b)
    {
        return static_cast<VFSBackendCaps>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline bool HasCap(VFSBackendCaps caps, VFSBackendCaps test)
    {
        return (static_cast<uint32_t>(caps) & static_cast<uint32_t>(test)) != 0;
    }

    // IVFSBackend
    //
    // Abstract storage provider. Concrete implementations:
    //   VFSDiskBackend   — wraps std::filesystem            (next ticket)
    //   VFSZipBackend    — reads from ZIP/PAK archive       (later)
    //   VFSMemoryBackend — in-memory store for tests/scratch (later)
    //
    // `path` in all methods is RELATIVE to this backend's mount root.
    // The IVFSContext is responsible for stripping the mount prefix.
    struct IVFSBackend
    {
        virtual ~IVFSBackend()                                                                                                      = default;

        [[nodiscard]] virtual VFSResult<IVFSFile*>                            Open(const VFSPath& path, VFSOpenFlags flags)                       = 0;
        virtual void                                            Close(IVFSFile* file)                                               = 0;
        [[nodiscard]] virtual VFSResult<VFSFileStat>                          Stat(const VFSPath& path) const                                     = 0;
        virtual bool                                            Exists(const VFSPath& path) const                                   = 0;

        // `arena` used to allocate the returned Array — lifetime tied to arena.
        [[nodiscard]] virtual VFSResult<Core::Containers::Array<VFSDirEntry>> List(Core::Memory::ArenaAllocator* arena, const VFSPath& dir) const = 0;

        // Write operations. Return Unsupported on read-only backends.
        [[nodiscard]] virtual VFSResult<void>                                 CreateDir(const VFSPath& path)
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        [[nodiscard]] virtual VFSResult<void> Remove(const VFSPath& path)
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        [[nodiscard]] virtual VFSResult<void> Rename(const VFSPath& from, const VFSPath& to)
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }

        virtual cstring        BackendType() const  = 0; // "disk", "zip", "memory"
        virtual VFSBackendCaps Capabilities() const = 0;
    };

} // namespace ZEngine::Core::VFS
```

    -- -

    ## #5.5 `IVFSContext.h` / `VFSDiskContext`

`IVFSContext` is the object all engine code holds.One instance per                                  engine,
    owned by engine                                                                                  startup
        .

`VFSDiskContext` is the **only concrete implementation in this ticket **.It is a direct passthrough to
`std::filesystem` — behaviour is identical to today,
    but typed through                          the new interface.
`Mount()` and `Unmount()` are stubs that log a warning and return OK.The full mount table replaces this in the next ticket without requiring any call sites to change.

```cpp
// ZEngine/Core/VFS/IVFSContext.h
#pragma once
#include <Core/VFS/IVFSBackend.h>
#include <Core/VFS/IVFSFile.h>
#include <Core/VFS/VFSPath.h>

    namespace ZEngine::Core::VFS
{
    // IVFSContext
    //
    // Top-level VFS object. Routes Open/Stat/List/Exists to the appropriate
    // backend via the mount table (next ticket).
    struct IVFSContext
    {
        virtual ~IVFSContext()                                                                                                             = default;

        [[nodiscard]] virtual VFSResult<IVFSFile*>                            Open(const VFSPath& path, VFSOpenFlags flags)                              = 0;
        virtual void                                            Close(IVFSFile* file)                                                      = 0;
        [[nodiscard]] virtual VFSResult<VFSFileStat>                          Stat(const VFSPath& path) const                                            = 0;
        virtual bool                                            Exists(const VFSPath& path) const                                          = 0;

        [[nodiscard]] virtual VFSResult<Core::Containers::Array<VFSDirEntry>> List(Core::Memory::ArenaAllocator* arena, const VFSPath& dir) const        = 0;

        // Mount table — stubs in this ticket, full impl in next ticket.
        // `priority`: higher value = checked first on path resolution.
        [[nodiscard]] virtual VFSResult<void>                                 Mount(IVFSBackend* backend, const VFSPath& logical_root, int priority = 0) = 0;
        [[nodiscard]] virtual VFSResult<void>                                 Unmount(const VFSPath& logical_root)                                       = 0;
    };

    // VFSDiskContext
    //
    // Passthrough to std::filesystem anchored at `native_root`.
    // All VFSPaths are resolved relative to that root.
    //
    // Sandbox check: ToNativePath() verifies that the resolved OS path is
    // still under m_native_root — prevents "../" escape from untrusted input.
    struct VFSDiskContext final : public IVFSContext
    {
        explicit VFSDiskContext(cstring native_root);
        ~VFSDiskContext() override;

        // ATOMIC WRITE PROTOCOL: For critical files (scenes, saves, pak manifests),
        // callers MUST NOT use Open(path, Write | Truncate) directly.
        // Instead, write to a temporary path and rename atomically:
        //   VFSPath tmp = path.Parent() / (path.Filename().Data + String(".tmp"));
        //   auto f = ctx.Open(tmp, Write | Create | Truncate);
        //   // ... write all data ...
        //   ctx.Close(f);
        //   ctx.Rename(tmp, path);  // atomic on POSIX; uses ReplaceFileW on Windows
        // This ensures the file is either fully new or unchanged on crash.
        // A helper VFSAtomicWrite(ctx, path, data, size) is provided in VFSUtils.h.
        [[nodiscard]] VFSResult<IVFSFile*>                            Open(const VFSPath& path, VFSOpenFlags flags) override;
        void                                            Close(IVFSFile* file) override;
        [[nodiscard]] VFSResult<VFSFileStat>                          Stat(const VFSPath& path) const override;
        bool                                            Exists(const VFSPath& path) const override;

        [[nodiscard]] VFSResult<Core::Containers::Array<VFSDirEntry>> List(Core::Memory::ArenaAllocator* arena, const VFSPath& dir) const override;

        // Stubs — log warning, return Ok. Full impl replaces this next ticket.
        [[nodiscard]] VFSResult<void>                                 Mount(IVFSBackend*, const VFSPath&, int) override;
        [[nodiscard]] VFSResult<void>                                 Unmount(const VFSPath&) override;

    private:
        // Writes the absolute OS path for `vfs_path` into `out_buffer`.
        // Returns false if the result would escape m_native_root.
        bool   ToNativePath(const VFSPath& vfs_path, char* out_buffer, size_t out_size) const;

        char   m_native_root[VFS_MAX_PATH] = {};
        size_t m_native_root_len           = 0;
    };

} // namespace ZEngine::Core::VFS
```

---

## 6. Normalization Rules

All normalization is applied in `VFSPath::Parse()` and `VFSPath::FromNative()`.

| Raw input | Normalized output | Notes |
|---|---|---|
| `/textures/rock.png` | `/textures/rock.png` | Already clean |
| `textures/rock.png` | `/textures/rock.png` | Leading slash added |
| `./textures/rock.png` | `/textures/rock.png` | `.` resolved |
| `textures/../rock.png` | `/rock.png` | `..` resolved |
| `//textures///rock.png` | `/textures/rock.png` | Consecutive slashes collapsed |
| `/textures/rock.png/` | `/textures/rock.png` | Trailing slash stripped |
| `C:\Assets\Textures\rock.png` | `/Assets/Textures/rock.png` | Drive letter stripped, `\` → `/` |
| `../../escape` | `Fail(InvalidPath)` | Root escape rejected |
| `` (empty) | `Fail(InvalidPath)` | Empty string rejected |
| Path > 255 chars after normalization | `Fail(InvalidPath)` | Buffer overflow prevented |

### Case sensitivity

`VFSPath` is **always byte-exact** (case-sensitive at the path layer). Case folding is a
backend responsibility:

- `VFSDiskBackend` on Windows / macOS HFS+ → case-insensitive lookup via OS
- `VFSDiskBackend` on Linux → case-sensitive
- `VFSZipBackend` → case-insensitive by default (ZIP spec)
- `VFSMemoryBackend` → case-sensitive

Call sites must always use the canonical casing of the asset path. The editor's scanner
(future ticket) will normalize casing when building the asset index.

---

## 7. Call Site Migration

The engineer migrates **two** existing call sites in this ticket as proof of concept.
All remaining call sites are migrated in a follow-up cleanup pass.

### 7.1 `ProjectViewUIComponent::Initialize`

```cpp
// BEFORE
m_assets_directory  = ParentLayer->CurrentApp->WorkingSpacePath; // std::filesystem::path
m_current_directory = m_assets_directory;

// AFTER
auto result = VFSPath::FromNative(
    ParentLayer->CurrentApp->WorkingSpacePath.string().c_str());
ZENGINE_VALIDATE_ASSERT(result.Succeeded(), "WorkingSpacePath is not a valid VFS path")
m_assets_directory_vfs  = result.Value();
m_current_directory_vfs = m_assets_directory_vfs;
```

Add `VFSPath m_assets_directory_vfs` and `VFSPath m_current_directory_vfs` to the component's
private members alongside the existing `std::filesystem::path` members (keep both during
the transition — remove the old ones once all methods are migrated).

### 7.2 `AssetManager::LoadTextureFileAsAsset`

```cpp
// BEFORE
Importers::AssetTexture* LoadTextureFileAsAsset(cstring file, bool absolute);

// AFTER — add an overload; keep the old signature for binary compatibility
Importers::AssetTexture* LoadTextureFileAsAsset(const VFSPath& path);
```

The new overload calls `ToNative` on the `VFSPath` internally to produce the OS path
passed to `stb_image`. The old `cstring` overload is preserved and internally calls
`VFSPath::FromNative(file)` then delegates to the new overload.

### 7.3 Icon loading in `ProjectViewUIComponent::Initialize`

```cpp
// BEFORE
const auto directory_icon_path = fmt::format("{0}{1}{2}",
    current_directoy.string(), PLATFORM_OS_BACKSLASH, "Settings/Icons/DirectoryIcon.png");
m_directory_icon = asset_mgr->LoadTextureFileAsAsset(directory_icon_path.c_str(), true);

// AFTER
auto icon_result = VFSPath::FromNative(directory_icon_path.c_str());
ZENGINE_VALIDATE_ASSERT(icon_result.Succeeded(), "Invalid directory icon path")
m_directory_icon = asset_mgr->LoadTextureFileAsAsset(icon_result.Value());
```

---

## 8. Unit Tests

File: `ZEngine/tests/test_vfspath.cpp`

```
Parse tests — success cases:
  Parse("/textures/rock.png")       → Succeeded(), CStr() == "/textures/rock.png"
  Parse("textures/rock.png")        → Succeeded(), CStr() == "/textures/rock.png"
  Parse("./textures/rock.png")      → Succeeded(), CStr() == "/textures/rock.png"
  Parse("textures/../rock.png")     → Succeeded(), CStr() == "/rock.png"
  Parse("//textures///rock.png")    → Succeeded(), CStr() == "/textures/rock.png"
  Parse("/textures/rock.png/")      → Succeeded(), CStr() == "/textures/rock.png"
  Parse("/")                        → Succeeded(), IsRoot() == true

Parse tests — failure cases:
  Parse("")                         → Failed(), Error() == InvalidPath
  Parse("../../escape")             → Failed(), Error() == InvalidPath
  Parse(path > 255 chars)           → Failed(), Error() == InvalidPath

FromNative tests:
  FromNative("C:\\Assets\\rock.png") [Windows] → CStr() == "/Assets/rock.png"
  FromNative("/usr/share/assets")   [POSIX]   → CStr() == "/usr/share/assets"

Accessor tests (input "/textures/rock.png"):
  Filename().Data / Length          → "rock.png", 8
  Stem().Data / Length              → "rock", 4
  Extension().Data / Length         → ".png", 4
  Parent().CStr()                   → "/textures"
  ComponentCount()                  → 2
  ComponentAt(0).Data               → "textures"
  ComponentAt(1).Data               → "rock.png"

Root accessor tests:
  VFSPath::Root().IsRoot()          → true
  VFSPath::Root().Parent().IsRoot() → true  (parent of root is root)
  VFSPath::Root().ComponentCount()  → 0

Composition tests:
  VFSPath::Parse("/textures").Value().Append("rock.png") → "/textures/rock.png"
  VFSPath::Parse("/textures").Value() / "rock.png"       → "/textures/rock.png"
  Append exceeding VFS_MAX_PATH                          → Failed(), InvalidPath

Comparison tests:
  Same input twice        → op==  true
  Different inputs        → op!=  true
  "/a/b" < "/a/c"        → true  (lexicographic on normalized string)

IsPrefixOf tests:
  "/textures".IsPrefixOf("/textures/rock.png")  → true
  "/textures".IsPrefixOf("/textures")           → true  (equal paths)
  "/tex".IsPrefixOf("/textures/rock.png")       → false (not component boundary)
  "/".IsPrefixOf("/anything")                   → true  (root is prefix of all)

Hash tests:
  Same input twice → equal Hash()
  Different inputs → different Hash()  (not guaranteed by spec, but true for all test cases)
```

---

## 9. Deliverables Checklist

```
[ ] ZEngine/Core/VFS/VFSError.h
      VFSError enum
      VFSResult<T> template
      VFSResult<void> specialisation

[ ] ZEngine/Core/VFS/VFSPath.h
      VFSPathComponent struct
      VFSPath struct with all declared methods
      VFSPathHasher struct

[ ] ZEngine/Core/VFS/VFSPath.cpp
      VFSPath::Parse(cstring)
      VFSPath::Parse(const char*, size_t)
      VFSPath::FromNative(cstring)
      VFSPath::Root()
      VFSPath::BuildComponents()
      VFSPath::ComputeHash()
      VFSPath::Filename(), Stem(), Extension(), Parent()
      VFSPath::ComponentAt()
      VFSPath::Append(cstring), Append(VFSPath)
      VFSPath::operator/()
      VFSPath::operator==, !=, <
      VFSPath::IsPrefixOf()
      VFSPath::ToNative()

[ ] ZEngine/Core/VFS/IVFSFile.h
      VFSOpenFlags enum + operator| + HasFlag
      VFSFileStat struct
      IVFSFile interface
      IVFSFile::ReadAll() default implementation

[ ] ZEngine/Core/VFS/IVFSBackend.h
      VFSDirEntry struct
      VFSBackendCaps enum + operator| + HasCap
      IVFSBackend interface

[ ] ZEngine/Core/VFS/IVFSContext.h
      IVFSContext interface
      VFSDiskContext declaration

[ ] ZEngine/Core/VFS/VFSDiskContext.cpp
      VFSDiskContext constructor + destructor
      VFSDiskContext::Open — creates a VFSDiskFile
      VFSDiskContext::Close
      VFSDiskContext::Stat
      VFSDiskContext::Exists
      VFSDiskContext::List
      VFSDiskContext::Mount  (stub — ZENGINE_CORE_WARN + return Ok)
      VFSDiskContext::Unmount (stub — ZENGINE_CORE_WARN + return Ok)
      VFSDiskContext::ToNativePath — sandbox check

[ ] ZEngine/tests/test_vfspath.cpp
      All test cases listed in §8

[ ] ZEngine/ZEngine/CMakeLists.txt
      Add Core/VFS/VFSPath.cpp and Core/VFS/VFSDiskContext.cpp to sources

[ ] ZEngine/ZEngine/Managers/AssetManager.h/.cpp
      Add LoadTextureFileAsAsset(const VFSPath&) overload
      Old cstring overload delegates to new one

[ ] ZEngine/Tetragrama/Components/ProjectViewUIComponent.h/.cpp
      Add VFSPath m_assets_directory_vfs
      Add VFSPath m_current_directory_vfs
      Migrate Initialize() to use VFSPath::FromNative
      Migrate icon loading to use new AssetManager overload
```

---

## 10. Out of Scope

The following are explicitly deferred to subsequent tickets:

| Feature | Ticket |
|---|---|
| Mount table with priority-ordered backend resolution | Next ticket |
| `VFSDiskBackend` as a standalone `IVFSBackend` (needed for mount table) | Next ticket |
| ZIP / PAK backend | Later |
| In-memory backend (for tests and scratch data) | Later |
| Async I/O / IOQueue | Later |
| File watching (inotify / FSEvents / RDCW) | Later |
| Async Scanner replacing `directory_iterator` in the browser | Later |
| `.meta` sidecar files and stable UUID persistence | Later |
| Asset Registry / multi-index store | Later |

---

## 11. Future Tickets

This is the first in a series. Each ticket depends on the one before it.

```
Ticket 1 (this) ─ VFSPath + IVFSFile + IVFSBackend + IVFSContext + VFSDiskContext
    │
    ▼
Ticket 2 ─ Mount table + VFSDiskBackend + VFSZipBackend
    │         IVFSContext routes to backends by path prefix
    │         Overlay semantics (priority-ordered resolution)
    │
    ▼
Ticket 3 ─ Async Scanner + VFSMemoryBackend
    │         Replaces directory_iterator in ProjectViewUIComponent
    │         Async tree walk using existing ThreadPoolHelper
    │
    ▼
Ticket 4 ─ FileWatcher (inotify / FSEvents / RDCW)
    │         Delivers change events via existing async queue
    │
    ▼
Ticket 5 ─ .meta sidecars + stable UUID persistence
    │         Replaces AssetManager's ephemeral UUID generation
    │
    ▼
Ticket 6 ─ AssetIndex (multi-index store) + DependencyGraph
              Replaces AssetManager's UUIDToHandle map
              Enables type-filtered queries and hot-reload cascades
```
