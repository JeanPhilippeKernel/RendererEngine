# ZEngine — Save System

**Priority:** P2 — Required for game progress persistence
**Status:** Design
**Depends on:** `vfs-design.md` (Ticket 1), `scene-serialization.md`
**Blocks:** Game progress, player data, Steam Cloud integration

---

## 1. Overview

The save system is split into two distinct layers that serve different purposes and
operate independently. Understanding the boundary between them is essential — blurring
it leads to bloated save files, redundant serialization work, and fragile load logic.

### Layer 1 — Scene Save (`scene-serialization.md`)

Scene serialization saves the full ECS state: every entity, every component, every
position, every animation frame. Its purpose is restoring the world to an exact snapshot
— used for level checkpoints where the player's position and all world object states must
be reproduced perfectly. A scene save for a typical level is in the range of megabytes.
It is written by `ISceneSerializer` and managed by the scene pipeline, not by this system.

### Layer 2 — Game State Save (this document)

The game state save stores only the lightweight, player-facing data that must persist
across sessions: inventory contents, quest flags, stats, current level identifier, unlocks,
and settings. It does not contain ECS component data. Its purpose is remembering _what the
player has done_, not _where every object in the world is_.

Target size: under 64 KB. Binary format, slot-based, written and read by `SaveManager`.

These two layers compose: when the player saves at a checkpoint, the game writes both a
scene save (for world state) and a game state save (for player data). On load, both are
read and the results are applied independently.

---

## 2. Save Slot Model

The system supports up to 8 save slots, numbered 0 through 7. The maximum is a
compile-time constant `SaveSystem::kMaxSlots = 8`. Slot 0 is reserved for autosave.
Slots 1–7 are manual save slots.

Each slot is one file on disk:

```
save_slot_0.zsav   ← autosave
save_slot_1.zsav   ← manual slot 1
save_slot_2.zsav   ← manual slot 2
...
save_slot_7.zsav   ← manual slot 7
settings.zsav      ← not slot-based; see §9
```

Slots are independent. Deleting slot 3 does not affect slot 2 or slot 4. Attempting to
load a slot that does not exist returns `VFSError::NotFound`.

---

## 3. Binary Save Format

Every `.zsav` file is structured as a fixed-size header followed by a variable-size
payload. No compression in v1. All multi-byte integers are little-endian.

```
ZSavHeader  (24 bytes, fixed)
ZSavPayload (variable, DataSize bytes)
```

### 3.1 Header Layout

```cpp
// ZEngine/SaveSystem/SaveFormat.h
#pragma once
#include <cstdint>

namespace ZEngine::SaveSystem
{
    // Four-byte magic: ASCII 'Z','S','A','V' = 0x5A534156 in little-endian
    static constexpr uint32_t kSavMagic   = 0x5A534156u;
    static constexpr uint32_t kSavVersion = 1u;

    static constexpr uint32_t kFlagAutosave = (1u << 0);

    // sizeof(ZSavHeader) must be 28 bytes. Add static_assert in SaveManager.cpp.
    struct ZSavHeader
    {
        uint32_t Magic;        // kSavMagic
        uint32_t Version;      // kSavVersion
        uint64_t Timestamp;    // Unix seconds (UTC) at time of write
        uint32_t SlotIndex;    // 0–7; kMaxSlots-1 max
        uint32_t Flags;        // kFlagAutosave | future flags
        uint32_t DataSize;     // byte count of payload following this header
        uint32_t Checksum;     // FNV-32 of the payload bytes (not the header)
                               // stored as little-endian on all platforms
                               // read with: uint32_t c = *reinterpret_cast<const uint32_t*>(&Checksum)
                               // (safe: ZSavHeader is memcpy'd to/from byte buffer)
    };
    static_assert(sizeof(ZSavHeader) == 28, "ZSavHeader size mismatch");
}
```

### 3.2 Payload Layout

The payload is a flat sequence of `ZSavRecord` entries. There is no count prefix —
the deserializer reads records until it has consumed `DataSize` bytes.

```cpp
// ZEngine/SaveSystem/SaveFormat.h (continued)

namespace ZEngine::SaveSystem
{
    // Variable-length record. On disk: KeyHash (4), ValueSize (4), then ValueSize bytes.
    // Not directly memcpy-able due to trailing flexible data — use the serializer helpers.
    struct ZSavRecordHeader
    {
        uint32_t KeyHash;    // FNV-32 of the key string
        uint32_t ValueSize;  // byte count of the value data that follows
    };
    static_assert(sizeof(ZSavRecordHeader) == 8, "ZSavRecordHeader size mismatch");
    // Immediately following on disk: uint8_t Value[ValueSize];

    // Maximum size for a single record value. Enforced during Deserialize to
    // prevent malicious or corrupted saves from causing OOM.
    static constexpr uint32_t kMaxRecordValueSize = 1024 * 1024u;  // 1 MB
}
```

The key string is never stored on disk — only its FNV-32 hash. This means key strings
are compile-time constants in practice. Key collisions are a programmer error; assert in
debug builds when two different key strings produce the same hash.

### 3.3 FNV-32 Checksum

```cpp
// ZEngine/SaveSystem/SaveChecksum.h
#pragma once
#include <cstdint>

namespace ZEngine::SaveSystem
{
    inline uint32_t FNV32(const uint8_t* data, uint32_t size) noexcept
    {
        uint32_t hash = 0x811C9DC5u;
        for (uint32_t i = 0; i < size; ++i)
        {
            hash ^= static_cast<uint32_t>(data[i]);
            hash *= 0x01000193u;
        }
        return hash;
    }

    inline uint32_t FNV32String(const char* key) noexcept
    {
        uint32_t hash = 0x811C9DC5u;
        while (*key)
        {
            hash ^= static_cast<uint32_t>(static_cast<unsigned char>(*key++));
            hash *= 0x01000193u;
        }
        return hash;
    }
}
```

The four `Checksum` bytes in `ZSavHeader` are the little-endian representation of
`FNV32(payload_ptr, DataSize)`. On load, recompute the checksum over the payload and
compare; mismatch returns `VFSError::Corrupted` — no silent acceptance of bad data.

---

## 4. `GameSaveData` — Key-Value Store

`GameSaveData` is the in-memory representation of a save's payload. The game code
populates it with typed setter calls, then hands it to `SaveManager::Save`. On load,
`SaveManager::Load` fills a `GameSaveData` that game code interrogates with typed
getter calls.

### 4.1 Internal Storage

```cpp
// ZEngine/SaveSystem/GameSaveData.h
#pragma once
#include <Core/Containers/Array.h>
#include <Core/Containers/UnorderedHashMap.h>
#include <Core/Containers/Strings.h>
#include <Core/Memory/ArenaAllocator.h>
#include <SaveSystem/SaveChecksum.h>
#include <cstdint>
#include <cstring>

namespace ZEngine::SaveSystem
{
    enum class SaveValueType : uint8_t
    {
        Int32   = 0,
        Float32 = 1,
        Bool    = 2,
        String  = 3,
        Blob    = 4,
    };

    struct SaveRecord
    {
        SaveValueType Type;
        uint32_t      Size;   // byte count of Data
        uint8_t*      Data;   // pointer into owning arena; never heap-allocated
    };

    class GameSaveData
    {
    public:
        // Must be called before any Set/Get. arena must outlive this object.
        void Initialize(Core::Memory::ArenaAllocator* arena);

        // --- Setters ---
        void SetInt   (const char* key, int32_t value);
        void SetFloat (const char* key, float value);
        void SetBool  (const char* key, bool value);
        void SetString(const char* key, const char* value);           // copies string bytes
        void SetBlob  (const char* key, const void* data, uint32_t size); // copies blob bytes

        // --- Getters (return default_value if key not found or type mismatches) ---
        int32_t     GetInt   (const char* key, int32_t default_value  = 0)       const;
        float       GetFloat (const char* key, float default_value    = 0.f)     const;
        bool        GetBool  (const char* key, bool default_value     = false)   const;
        const char* GetString(const char* key, const char* default_value = "")   const;
        // Returns actual size written into out_buf; 0 if not found or size mismatch.
        uint32_t    GetBlob  (const char* key, void* out_buf, uint32_t buf_size) const;

        // --- Serialization ---
        // Appends the binary payload for all records to out. Does not write the header.
        void Serialize  (Core::Memory::ArenaAllocator* scratch,
                         Core::Containers::Array<uint8_t>& out) const;

        // Reads binary payload written by Serialize. Returns false on malformed data.
        bool Deserialize(const uint8_t* data, uint32_t size);

        // Number of records stored.
        uint32_t Count() const;

        // Clears all records (does not release arena memory).
        void Clear();

    private:
        static constexpr uint32_t kMaxRecords = 10000; // prevent malicious/corrupted saves from exhausting memory

        // In Deserialize: validate record count before inserting
        // ZENGINE_VALIDATE_ASSERT(record_count <= kMaxRecords,
        //     "Save file contains too many records — possible corruption or version mismatch");

        // In GameSaveData::Deserialize, after reading each record header:
        // static constexpr uint32_t kMaxRecordValueSize = 1024 * 1024u;  // 1 MB per record
        //
        // ZENGINE_VALIDATE_ASSERT(record_header.ValueSize <= kMaxRecordValueSize,
        //     "Save file corrupted: record '%u' claims value size %u (max %u)",
        //     record_header.KeyHash, record_header.ValueSize, kMaxRecordValueSize);

        Core::Memory::ArenaAllocator*                             m_Arena  = nullptr;
        Core::Containers::UnorderedHashMap<uint32_t, SaveRecord>  m_Records;

        // Returns a pointer to a newly allocated copy of data in m_Arena.
        uint8_t* AllocCopy(const void* data, uint32_t size);

        // Look up a record by key hash; return nullptr if absent or type wrong.
        const SaveRecord* Find(const char* key, SaveValueType expected_type) const;
    };
}
```

### 4.2 Serialization Implementation Notes

`Serialize` walks `m_Records` and emits one `ZSavRecordHeader` + value bytes per entry.
Order is unspecified (hash map iteration). `Deserialize` reads `ZSavRecordHeader` entries
sequentially, allocating each value into `m_Arena` and inserting into `m_Records`.

```cpp
// In GameSaveData::Deserialize, record reading loop:
uint32_t bytes_consumed = 0;
while (bytes_consumed < payload_size) {
    // Validate: enough bytes remain for a record header
    ZENGINE_VALIDATE_ASSERT(
        bytes_consumed + sizeof(ZSavRecordHeader) <= payload_size,
        "Save corrupted: record header extends past payload (consumed=%u, payload=%u)",
        bytes_consumed, payload_size);

    ZSavRecordHeader rec_hdr;
    memcpy(&rec_hdr, data + bytes_consumed, sizeof(ZSavRecordHeader));
    bytes_consumed += sizeof(ZSavRecordHeader);

    // Validate: value data fits within remaining payload
    ZENGINE_VALIDATE_ASSERT(
        bytes_consumed + rec_hdr.ValueSize <= payload_size,
        "Save corrupted: record value (KeyHash=0x%08X, size=%u) extends past payload",
        rec_hdr.KeyHash, rec_hdr.ValueSize);

    // Validate: value size within per-record limit
    ZENGINE_VALIDATE_ASSERT(
        rec_hdr.ValueSize <= kMaxRecordValueSize,
        "Save corrupted: record value size %u exceeds maximum %u",
        rec_hdr.ValueSize, kMaxRecordValueSize);

    // ... read value bytes ...
    bytes_consumed += rec_hdr.ValueSize;
}
```

Key hashes are computed by `FNV32String(key)` at every setter and getter call. Because the
map is keyed by hash, `SetInt("score", 100)` followed by `GetInt("score", 0)` do not touch
any heap memory and perform one hash lookup.

---

## 5. `SaveManager`

`SaveManager` owns the slot lifecycle: constructing the on-disk file from a `GameSaveData`,
reading it back, managing atomic writes, and providing slot metadata without loading data.

```cpp
// ZEngine/SaveSystem/SaveManager.h
#pragma once
#include <Core/Memory/ArenaAllocator.h>
#include <Core/Containers/Array.h>
#include <VFS/IVFSContext.h>
#include <VFS/VFSResult.h>
#include <SaveSystem/GameSaveData.h>
#include <SaveSystem/SaveFormat.h>
#include <cstdint>

namespace ZEngine::SaveSystem
{
    static constexpr uint32_t kMaxSlots = 8u;

    struct SaveSlotInfo
    {
        bool     Exists;
        uint32_t Version;      // ZSavHeader::Version from file
        uint64_t Timestamp;    // ZSavHeader::Timestamp (Unix seconds)
        bool     IsAutosave;   // true if kFlagAutosave is set
    };

    class SaveManager
    {
    public:
        // arena must outlive SaveManager. ctx is the VFS context used for all file I/O.
        void Initialize(Core::Memory::ArenaAllocator* arena, VFS::IVFSContext* ctx);

        // Write slot to disk. slot must be in [0, kMaxSlots).
        // Performs atomic write: tmp file → checksum verify → rename.
        VFS::VFSResult<void> Save  (uint32_t slot, const GameSaveData& data);

        // Read slot from disk into out_data. Verifies magic and checksum.
        // Returns VFSError::NotFound if slot file absent.
        // Returns VFSError::Corrupted if magic or checksum mismatch.
        VFS::VFSResult<void> Load  (uint32_t slot, GameSaveData& out_data);

        // Delete the save file for slot. No-op (success) if file does not exist.
        VFS::VFSResult<void> Delete(uint32_t slot);

        // Returns slot metadata without loading the full payload.
        // Does not allocate; reads only the 28-byte header.
        SaveSlotInfo GetSlotInfo(uint32_t slot) const;

        // Fills out with SaveSlotInfo for all kMaxSlots slots.
        void ListSlots(Core::Containers::Array<SaveSlotInfo>& out) const;

        // Writes to slot 0 with kFlagAutosave set in the header.
        VFS::VFSResult<void> Autosave(const GameSaveData& data);

        // Returns the save directory path (resolved by PlatformPaths).
        const VFS::VFSPath& GetSaveDirectory() const;

    private:
        Core::Memory::ArenaAllocator* m_Arena   = nullptr;
        VFS::IVFSContext*             m_VFS     = nullptr;
        VFS::VFSPath                  m_SaveDir;

        // Returns "save_slot_N.zsav" path under m_SaveDir.
        VFS::VFSPath SlotPath(uint32_t slot) const;

        // Returns "save_slot_N.zsav.tmp" path under m_SaveDir.
        VFS::VFSPath SlotTmpPath(uint32_t slot) const;

        // Serializes header + payload into out_buf (arena-allocated).
        // Fills header.Checksum from payload bytes before returning.
        void BuildFileBuffer(uint32_t slot, uint32_t flags,
                             const GameSaveData& data,
                             Core::Containers::Array<uint8_t>& out_buf);

        // Validates magic and checksum of a loaded file buffer.
        // Returns false (does not assert) on mismatch so Load can return VFSError::Corrupted.
        bool ValidateBuffer(const uint8_t* buf, uint32_t size) const;
    };
}
```

### 5.1 Atomic Write Protocol

`Save(slot, data)` follows this sequence:

Atomic write protocol:
1. Write all data to `save_slot_N.zsav.tmp`
2. Flush and close the file
3. Rename `save_slot_N.zsav.tmp` → `save_slot_N.zsav`:
   - POSIX: `rename(2)` is atomic at the filesystem level
   - Windows: `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` is NOT atomic.
     On crash between write and move, the `.tmp` file may remain.
     The next successful Save will overwrite it.
     `SaveManager::Initialize()` scans for and deletes `.tmp` files older than 60 seconds.

// No save data is ever partially written — the original file is replaced only
// after the .tmp is fully flushed. The worst case is a missed save, not corruption.

Full save sequence:
1. Build the full file buffer in a scratch arena: `ZSavHeader` + serialized payload.
2. Compute `FNV32(payload_ptr, DataSize)` and store the result in `header.Checksum`.
3. Write the buffer to `save_slot_N.zsav.tmp` via the VFS.
4. Call `VFSContext::Flush` on the temp file to ensure OS-level durability.
5. Rename `save_slot_N.zsav.tmp` → `save_slot_N.zsav` (see atomicity note above).
6. Return `VFSResult<void>` success.

If any step fails, the temp file is left on disk (not deleted) for diagnostic purposes
and the original save file is untouched. A stale `.tmp` file from a previous crash is
handled by `SaveManager::Initialize()` cleanup (see above).

### 5.2 Load Protocol

`Load(slot, out_data)` follows this sequence:

1. Read the full file from `save_slot_N.zsav` into a scratch arena buffer.
2. Check that `buf_size >= sizeof(ZSavHeader)`. If not: return `VFSError::Corrupted`.
3. Cast the first 28 bytes to `const ZSavHeader*`.
4. Check `header.Magic == kSavMagic`. If not: return `VFSError::Corrupted`.
5. Check `header.Version <= kSavVersion`. If `header.Version > kSavVersion`: return
   `VFSError::VersionMismatch` (future-proofing; v1 has no migrations).
6. Recompute `FNV32(payload_ptr, header.DataSize)` and compare to `header.Checksum`.
   If mismatch: log `ZENGINE_CORE_WARN` with slot index, return `VFSError::Corrupted`.
7. Call `out_data.Deserialize(payload_ptr, header.DataSize)`.
8. Return success.

Under no circumstances is a save with a failed checksum partially loaded.

---

## 6. Platform Save Path Helper

```cpp
// ZEngine/SaveSystem/PlatformPaths.h
#pragma once
#include <VFS/VFSPath.h>

namespace ZEngine::SaveSystem
{
    struct PlatformPaths
    {
        // Returns the platform-appropriate save directory for the given application name.
        // The returned path does not include a trailing separator.
        // Does not create the directory — caller (SaveManager::Initialize) creates it
        // via VFSContext::CreateDirectoriesRecursive.
        //
        // Windows : %APPDATA%\<app_name>\saves
        // Linux   : $XDG_DATA_HOME/<app_name>/saves  (falls back to ~/.local/share/)
        // macOS   : ~/Library/Application Support/<app_name>/saves
        static VFS::VFSPath GetSaveDirectory(const char* app_name);

    // The save directory may not exist on first run.
    // Caller must create it before writing:
    //   VFSResult<void> r = vfs->CreateDirectoriesRecursive(save_dir);
    //   if (r.Failed()) {
    //       ZENGINE_CORE_WARN("SaveManager: cannot create save directory %s", save_dir.CStr());
    //       return VFSResult<void>::Fail(r.Error());
    //   }
    // This is done once in SaveManager::Initialize, not per-save.
    };
}
```

### 6.1 Platform Implementations

**Windows** (`PlatformPaths.win32.cpp`):

```cpp
#include <shlobj.h>   // SHGetFolderPathW

VFS::VFSPath PlatformPaths::GetSaveDirectory(const char* app_name)
{
    wchar_t appdata[MAX_PATH] = {};
    ZENGINE_VALIDATE_ASSERT(
        SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdata)),
        "SHGetFolderPathW failed — cannot resolve APPDATA"
    );
    // Convert to narrow string and compose path.
    // ... (narrow conversion helper, then append \app_name\saves)
}
```

**Linux** (`PlatformPaths.linux.cpp`):

```cpp
VFS::VFSPath PlatformPaths::GetSaveDirectory(const char* app_name)
{
    const char* xdg = getenv("XDG_DATA_HOME");
    // If XDG_DATA_HOME is set and non-empty, use it; otherwise fall back to ~/.local/share/
    const char* base = (xdg && xdg[0] != '\0') ? xdg : nullptr;
    // ... compose base/<app_name>/saves
}
```

**macOS** (`PlatformPaths.macos.mm`):

```objc
#import <Foundation/Foundation.h>

VFS::VFSPath PlatformPaths::GetSaveDirectory(const char* app_name)
{
    NSArray* paths = NSSearchPathForDirectoriesInDomains(
        NSApplicationSupportDirectory, NSUserDomainMask, YES);
    NSString* appSupport = [paths firstObject];
    // ... compose appSupport/<app_name>/saves
}
```

`SaveManager::Initialize` calls `PlatformPaths::GetSaveDirectory`, stores the result in
`m_SaveDir`, then calls `m_VFS->CreateDirectoriesRecursive(m_SaveDir)` to ensure the
directory exists before any read or write is attempted.

---

## 7. Steam Cloud Integration

Steam Cloud support is opt-in and gated on runtime availability. `SaveManager` does not
link against the Steamworks SDK directly — it calls through a thin `SteamManager`
interface that is a no-op stub when Steam is unavailable (e.g., non-Steam build, editor).

```cpp
// ZEngine/SaveSystem/SteamManager.h (stub interface)
#pragma once
#include <cstdint>

namespace ZEngine::SaveSystem
{
    struct SteamManager
    {
        // Returns true if the Steamworks API is initialized and cloud is enabled.
        static bool IsAvailable();

        // Upload file bytes to Steam Cloud. filename is relative (e.g., "save_slot_1.zsav").
        // Synchronous in v1; returns false on failure.
        static bool CloudWrite(const char* filename, const void* data, uint32_t size);

        // Download file bytes from Steam Cloud into out_data (caller's arena).
        // Returns 0 if file does not exist or Steam unavailable; otherwise byte count.
        static uint32_t CloudRead(const char* filename,
                                  Core::Memory::ArenaAllocator* arena,
                                  uint8_t*& out_data);
    };
}
```

`SaveManager::Save` — after the local atomic write succeeds:

```cpp
if (SteamManager::IsAvailable())
{
    const char* filename = /* base name of SlotPath(slot) */;
    if (!SteamManager::CloudWrite(filename, file_buf.Data(), file_buf.Size()))
        ZENGINE_CORE_WARN("Steam Cloud write failed for slot %u — local save is intact", slot);
}
```

`SaveManager::Load` — if the local file is absent (`VFSError::NotFound`):

```cpp
if (SteamManager::IsAvailable())
{
    uint8_t* cloud_buf = nullptr;
    uint32_t cloud_size = SteamManager::CloudRead(filename, m_Arena, cloud_buf);
    if (cloud_size > 0)
    {
        // Write the cloud data to local disk via atomic write, then proceed with load.
    }
}
```

Steam Cloud is a best-effort layer. A failure to upload or download does not fail the
local save/load operation. All errors are logged via `ZENGINE_CORE_WARN`.

---

## 8. Autosave

`SaveManager::Autosave(const GameSaveData& data)` is a thin wrapper around `Save(0, data)`
that also sets `kFlagAutosave` in the header flags field:

```cpp
VFS::VFSResult<void> SaveManager::Autosave(const GameSaveData& data)
{
    // Internally, BuildFileBuffer is called with flags = kFlagAutosave.
    return SaveWithFlags(0u, kFlagAutosave, data);
}
```

`GetSlotInfo(0).IsAutosave` is `true` iff `kFlagAutosave` is set in that slot's header.

The UI layer polls `GetSlotInfo(0)` or subscribes to a game-side `OnAutosaveComplete`
event to show the "Autosaving..." indicator. `SaveManager` itself does not touch UI.

Autosave frequency is the game's responsibility — `SaveManager` has no timer. A typical
pattern: call `Autosave` on level transition, on checkpoint trigger, and on application
focus loss.

---

## 9. Settings Save

Game settings (display resolution, audio volumes, keybindings, accessibility options)
are stored in a separate file:

```
<save_dir>/settings.zsav
```

This file uses the same `ZSavHeader` + `ZSavPayload` format as slot saves. `SlotIndex`
in the header is `0xFFFFFFFF` (sentinel for "not a slot file"). It is not versioned
separately from the main format version.

```cpp
// SaveManager additions for settings
VFS::VFSResult<void> SaveSettings(const GameSaveData& settings);
VFS::VFSResult<void> LoadSettings(GameSaveData& out_settings);
```

Settings are loaded once at startup before the first frame and saved whenever a setting
changes (debounced — not on every slider tick). Settings are never autosaved; they are
always written immediately when the user confirms a change.

Settings keys by convention are prefixed `"settings."` (e.g., `"settings.audio.master_volume"`,
`"settings.display.vsync"`) to avoid accidental collision with game state keys if a
`GameSaveData` is shared.

---

## 10. Checksum Specification

FNV-32 is applied over the payload bytes only (the `DataSize` bytes after the header).
The header itself is not checksummed.

```
checksum_value = FNV32(file_buffer + sizeof(ZSavHeader), header.DataSize)
```

The `header.Checksum` field is a `uint32_t` stored as little-endian on all platforms.
Assign it directly from the FNV-32 result (the integer representation matches little-endian byte order on little-endian platforms; on big-endian platforms a byte-swap is needed):

```
header.Checksum = checksum_value;  // stored little-endian
```

On verification:

```cpp
uint32_t stored   = header.Checksum;  // direct read — little-endian on all supported platforms
uint32_t computed = FNV32(payload_ptr, header.DataSize);
if (stored != computed)
{
    ZENGINE_CORE_WARN("Save slot %u: checksum mismatch (stored 0x%08X, computed 0x%08X) — rejecting",
        slot, stored, computed);
    return VFSError::Corrupted;
}
```

A future v2 format may upgrade to CRC-32C or xxHash3 if performance or collision
resistance becomes a concern. The `Version` field in the header provides the migration
hook — v1 readers reject files with `Version > 1` via `VFSError::VersionMismatch`.

---

## 11. File Layout

```
ZEngine/
  SaveSystem/
    SaveFormat.h          — ZSavHeader, ZSavRecordHeader, constants, FNV-32 helpers
    SaveChecksum.h        — FNV32 / FNV32String inline functions
    GameSaveData.h        — GameSaveData class declaration
    GameSaveData.cpp      — SetInt/GetInt/Serialize/Deserialize implementations
    SaveManager.h         — SaveManager class declaration, SaveSlotInfo
    SaveManager.cpp       — Save/Load/Delete/Autosave/ListSlots implementations
    PlatformPaths.h       — PlatformPaths::GetSaveDirectory declaration
    PlatformPaths.win32.cpp  — Windows implementation (SHGetFolderPathW)
    PlatformPaths.linux.cpp  — Linux implementation (XDG / ~/.local/share)
    PlatformPaths.macos.mm   — macOS implementation (NSApplicationSupportDirectory)
    SteamManager.h        — SteamManager stub interface
    SteamManager.cpp      — No-op stub (real impl lives in platform/steam layer)
```

All files are in namespace `ZEngine::SaveSystem`. No file in this module uses `new`,
`delete`, `std::unique_ptr`, or exceptions. All memory is allocated from the `ArenaAllocator`
passed to `SaveManager::Initialize` or to `GameSaveData::Initialize`.

---

## 12. Deliverables Checklist

- [ ] `SaveFormat.h` — header struct, record header struct, magic/version constants, flag constants
- [ ] `SaveChecksum.h` — `FNV32` and `FNV32String` inline implementations
- [ ] `GameSaveData.h` / `GameSaveData.cpp` — all Set*/Get* methods, `Serialize`, `Deserialize`
- [ ] `SaveManager.h` / `SaveManager.cpp` — `Initialize`, `Save`, `Load`, `Delete`, `Autosave`,
  `GetSlotInfo`, `ListSlots`, atomic write, checksum validation
- [ ] `PlatformPaths.h` — declaration of `GetSaveDirectory`
- [ ] `PlatformPaths.win32.cpp` — Windows impl via `SHGetFolderPathW`
- [ ] `PlatformPaths.linux.cpp` — Linux impl via XDG / fallback
- [ ] `PlatformPaths.macos.mm` — macOS impl via `NSSearchPathForDirectoriesInDomains`
- [ ] `SteamManager.h` — interface declaration
- [ ] `SteamManager.cpp` — no-op stub; real impl separate
- [ ] Unit tests: round-trip `GameSaveData` serialize/deserialize, checksum rejection on
  tampered payload, slot isolation (write slot 2 does not affect slot 3 state)
- [ ] Integration test: `SaveManager::Save` then `SaveManager::Load` on all 8 slots,
  verify all typed values survive the round-trip
- [ ] Corrupt-file test: flip one byte in the payload, verify `Load` returns
  `VFSError::Corrupted` and does not partially populate `out_data`
- [ ] Platform path test: verify `GetSaveDirectory` returns a writable path on all
  three platforms
