# Ticket 5 — .meta Sidecars, MetaFileIO, and Stable UUID Persistence

**Goal**: Assign a stable UUID to every project asset on first import; persist that UUID in a
`.meta` sidecar file (JSON, committed to VCS); load the UUID on subsequent imports instead of
generating a new one. This fixes `AssetManager`'s current UUID-per-launch instability and
enables deterministic scene YAML references.

---

## 1. Problem Statement

`AssetManager` currently calls `uuids::uuid_random_generator{}()` at import time.
Every editor restart produces different UUIDs for the same file, breaking:

- Scene YAML files that reference `MeshUUID`, `MaterialUUID`, etc.
- Incremental re-import (engine cannot tell if the asset changed or was re-imported)
- Multi-engineer workflows (each local checkout has different UUIDs in memory)

The fix: treat the `.meta` file as the ground truth for UUID. If `mesh.glb.meta` exists,
read the UUID from it. If not, generate once, write it, never regenerate.

---

## 2. Public API

### 2.1 `ImportStatus`

```cpp
// ZEngine/VFS/Meta/ImportStatus.h
#pragma once
#include <cstdint>

namespace ZEngine::VFS
{
    enum class ImportStatus : uint8_t
    {
        Unknown   = 0,  // not yet evaluated
        UpToDate  = 1,  // .meta exists, source SHA matches → skip
        Stale     = 2,  // .meta exists, source SHA differs → reimport
        New       = 3,  // no .meta found → first import
    };
}
```

### 2.2 `MetaFileData`

```cpp
// ZEngine/VFS/Meta/MetaFileData.h
#pragma once
#include <Core/ZEngineDef.h>
#include <uuid.h>
#include <VFS/Meta/ImportStatus.h>

namespace ZEngine::VFS
{
    constexpr uint32_t META_MAX_SETTINGS = 32;

    struct MetaKeyValuePair
    {
        char Key[64]   = {};
        char Value[128] = {};
    };

    struct MetaFileData
    {
        uuids::uuid      AssetUUID                           = {};
        char             ImporterName[64]                    = {};
        char             LastSourceSha256[65]                = {};  // hex + null
        int64_t          LastImportTimeNs                    = 0;
        char             ArtifactPath[MAX_FILE_PATH_COUNT]   = {};
        MetaKeyValuePair Settings[META_MAX_SETTINGS]         = {};
        uint32_t         SettingsCount                       = 0;

        // Runtime only — NOT written to or read from JSON
        ImportStatus Status = ImportStatus::Unknown;
    };
}
```

### 2.3 `MetaFileIO`

```cpp
// ZEngine/VFS/Meta/MetaFileIO.h
#pragma once
#include <VFS/IVFSContext.h>
#include <VFS/Meta/MetaFileData.h>
#include <VFS/VFSResult.h>

namespace ZEngine::VFS
{
    class MetaFileIO
    {
    public:
        // Derive sidecar path: "/project/mesh.glb" → "/project/mesh.glb.meta"
        static VFSPath MetaPathFor(const VFSPath& asset_path);

        // Read .meta from VFS; fills MetaFileData. Status = Unknown if parse fails.
        static VFSResult<MetaFileData> Read(IVFSContext& ctx, const VFSPath& asset_path);

        // Write MetaFileData to .meta (creates or overwrites).
        static VFSResult<void> Write(IVFSContext& ctx, const VFSPath& asset_path,
                                     const MetaFileData& data);

        // High-level helper used by scanner/importer:
        //   - If .meta exists and SHA matches → return UpToDate
        //   - If .meta exists and SHA differs  → update SHA, write, return Stale
        //   - If no .meta                      → generate UUID, write, return New
        static VFSResult<MetaFileData> GetOrCreate(IVFSContext& ctx,
                                                   const VFSPath& asset_path,
                                                   const char*    importer_name,
                                                   const char*    current_sha256);

        // Compute SHA-256 of the asset file content (for change detection)
        static VFSResult<void> ComputeSHA256(IVFSContext& ctx,
                                             const VFSPath& asset_path,
                                             char out_hex[65]);
    };
}
```

---

## 3. JSON Schema

A `.meta` file is a UTF-8 JSON object. Example for `mesh.glb.meta`:

```json
{
    "uuid":              "550e8400-e29b-41d4-a716-446655440000",
    "importer":          "AssimpImporter",
    "source_sha256":     "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    "import_time_ns":    1748000000000000000,
    "artifact_path":     "/.cache/mesh_550e8400.zasset",
    "settings": [
        { "key": "GenerateLODs",    "value": "false" },
        { "key": "FlipUVs",         "value": "true"  }
    ]
}
```

Fields not present in the schema are silently ignored on read (forward compatibility).
`ImportStatus` is a runtime field only — it MUST NOT appear in the JSON.

---

## 4. Implementation: `MetaFileIO::Read`

Use nlohmann_json no-exception API throughout:

```cpp
VFSResult<MetaFileData> MetaFileIO::Read(IVFSContext& ctx, const VFSPath& asset_path)
{
    VFSPath meta_path = MetaPathFor(asset_path);

    auto open_result = ctx.OpenFile(meta_path, VFSOpenFlags::Read);
    if (!open_result.IsOk())
        return VFSResult<MetaFileData>::Fail(open_result.Error());

    auto& file = open_result.Value();
    uint64_t size = file->Size();

    // Read into a small stack buffer or arena — avoid heap when file is small
    Core::Containers::Array<char> buf;
    buf.Resize(size + 1);
    file->ReadAt(buf.Data(), size, 0);
    buf[size] = '\0';

    auto j = nlohmann::json::parse(buf.Data(), nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
        return VFSResult<MetaFileData>::Fail(VFSError::InvalidData);

    MetaFileData out{};

    if (j.contains("uuid") && j["uuid"].is_string())
    {
        auto parsed = uuids::uuid::from_string(j["uuid"].get<std::string>());
        if (parsed.has_value())
            out.AssetUUID = parsed.value();
    }

    auto copy_str = [](const std::string& src, char* dst, size_t cap) {
        size_t n = std::min(src.size(), cap - 1);
        std::memcpy(dst, src.data(), n);
        dst[n] = '\0';
    };

    if (j.contains("importer")        && j["importer"].is_string())
        copy_str(j["importer"].get<std::string>(),     out.ImporterName,    sizeof(out.ImporterName));
    if (j.contains("source_sha256")   && j["source_sha256"].is_string())
        copy_str(j["source_sha256"].get<std::string>(), out.LastSourceSha256, sizeof(out.LastSourceSha256));
    if (j.contains("import_time_ns")  && j["import_time_ns"].is_number_integer())
        out.LastImportTimeNs = j["import_time_ns"].get<int64_t>();
    if (j.contains("artifact_path")   && j["artifact_path"].is_string())
        copy_str(j["artifact_path"].get<std::string>(), out.ArtifactPath,   sizeof(out.ArtifactPath));

    if (j.contains("settings") && j["settings"].is_array())
    {
        for (auto& s : j["settings"])
        {
            if (out.SettingsCount >= META_MAX_SETTINGS) break;
            if (!s.contains("key") || !s.contains("value")) continue;
            auto& kv = out.Settings[out.SettingsCount++];
            copy_str(s["key"].get<std::string>(),   kv.Key,   sizeof(kv.Key));
            copy_str(s["value"].get<std::string>(), kv.Value, sizeof(kv.Value));
        }
    }

    out.Status = ImportStatus::Unknown;  // caller sets this
    return VFSResult<MetaFileData>::Ok(out);
}
```

---

## 5. Implementation: `MetaFileIO::GetOrCreate`

```cpp
VFSResult<MetaFileData> MetaFileIO::GetOrCreate(
    IVFSContext& ctx,
    const VFSPath& asset_path,
    const char*    importer_name,
    const char*    current_sha256)
{
    auto read_result = Read(ctx, asset_path);

    if (read_result.IsOk())
    {
        MetaFileData& existing = read_result.Value();
        if (std::strcmp(existing.LastSourceSha256, current_sha256) == 0)
        {
            existing.Status = ImportStatus::UpToDate;
            return VFSResult<MetaFileData>::Ok(existing);
        }

        // SHA changed → update and rewrite
        std::strncpy(existing.LastSourceSha256, current_sha256,
                     sizeof(existing.LastSourceSha256) - 1);
        existing.LastImportTimeNs = NowNs();
        existing.Status           = ImportStatus::Stale;
        Write(ctx, asset_path, existing);   // best-effort; ignore error
        return VFSResult<MetaFileData>::Ok(existing);
    }

    // No .meta → first import: generate UUID
    MetaFileData fresh{};
    fresh.AssetUUID = uuids::uuid_random_generator{}();
    std::strncpy(fresh.ImporterName,     importer_name,   sizeof(fresh.ImporterName) - 1);
    std::strncpy(fresh.LastSourceSha256, current_sha256,  sizeof(fresh.LastSourceSha256) - 1);
    fresh.LastImportTimeNs = NowNs();
    fresh.Status           = ImportStatus::New;
    Write(ctx, asset_path, fresh);
    return VFSResult<MetaFileData>::Ok(fresh);
}
```

`NowNs()` is a file-local helper:
```cpp
static int64_t NowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
```

---

## 6. AssetManager Modifications

### 6.1 New method: `GetOrCreateUUID`

```cpp
// ZEngine/Managers/AssetManager.h — add to struct AssetManager
struct AssetManager
{
    // ... existing fields ...

    // Returns the stable UUID for asset_path.
    // Reads .meta if present; generates + writes one if not.
    // Never returns a null UUID.
    static uuids::uuid GetOrCreateUUID(
        VFS::IVFSContext& ctx,
        const VFS::VFSPath& asset_path,
        const char* importer_name);
};
```

### 6.2 Remove UUID generation from `AssimpImporter`

Before (problematic):
```cpp
// AssimpImporter.cpp — REMOVE this pattern
asset.MeshUUID = uuids::uuid_random_generator{}();
```

After:
```cpp
// AssimpImporter.cpp — REPLACE with
char sha256[65] = {};
VFS::MetaFileIO::ComputeSHA256(ctx, vfs_path, sha256);
auto meta = VFS::MetaFileIO::GetOrCreate(ctx, vfs_path, "AssimpImporter", sha256);
asset.MeshUUID = meta.IsOk() ? meta.Value().AssetUUID : uuids::uuid_random_generator{}();
```

The fallback `uuid_random_generator` is kept for the case where VFS is not yet available
(e.g., unit tests that construct `AssetMesh` directly).

---

## 7. Scanner Integration

`VFSScanner::ScanDirectory` is extended to call `MetaFileIO::GetOrCreate` for every discovered
asset file. Add a scan result counter:

```cpp
// VFSScanner.h — add to ScanStats
struct ScanStats
{
    uint32_t FilesVisited    = 0;
    uint32_t DirsVisited     = 0;
    uint32_t MetasCreated    = 0;  // NEW
    uint32_t MetasUpdated    = 0;  // NEW (SHA changed)
    uint32_t MetasUpToDate   = 0;  // NEW
};
```

In `ScanDirectory`, after pushing a file entry to the cache:
```cpp
if (IsAssetExtension(entry.Name))
{
    char sha256[65] = {};
    MetaFileIO::ComputeSHA256(*m_ctx, entry.VFSPath, sha256);
    auto meta = MetaFileIO::GetOrCreate(*m_ctx, entry.VFSPath,
                                        "VFSScanner", sha256);
    if (meta.IsOk())
    {
        switch (meta.Value().Status)
        {
            case ImportStatus::New:       ++m_stats.MetasCreated;   break;
            case ImportStatus::Stale:     ++m_stats.MetasUpdated;   break;
            case ImportStatus::UpToDate:  ++m_stats.MetasUpToDate;  break;
            default: break;
        }
    }
}
```

`IsAssetExtension` checks for `.glb`, `.gltf`, `.fbx`, `.png`, `.jpg`, `.hdr`, `.ktx`.

---

## 8. VCS Integration Notes

- `.meta` files MUST be committed to version control alongside the assets they describe.
- The canonical `.gitignore` entry for this project should **not** exclude `*.meta`.
- `ArtifactPath` (the compiled `.zasset` binary) should be in `.gitignore` (it's a build artifact).
- During CI import, `GetOrCreate` returns `UpToDate` for all files → no UUID churn, deterministic builds.

---

## 9. Unit Tests

File: `ZEngine/tests/VFS/MetaFileIOTest.cpp`

### Test 1 — Round-trip: write then read returns identical data
```cpp
TEST(MetaFileIO, RoundTrip)
{
    MemoryVFSContext ctx;
    VFSPath path = VFSPath::Parse("/project/mesh.glb").Value();

    MetaFileData in{};
    in.AssetUUID = uuids::uuid_random_generator{}();
    snprintf(in.ImporterName,     sizeof(in.ImporterName),     "AssimpImporter");
    snprintf(in.LastSourceSha256, sizeof(in.LastSourceSha256),
             "abc123def456abc123def456abc123def456abc123def456abc123def456abcd");
    in.LastImportTimeNs = 1748000000000000000LL;
    in.SettingsCount = 1;
    snprintf(in.Settings[0].Key,   sizeof(in.Settings[0].Key),   "FlipUVs");
    snprintf(in.Settings[0].Value, sizeof(in.Settings[0].Value), "true");

    ASSERT_TRUE(MetaFileIO::Write(ctx, path, in).IsOk());

    auto out_result = MetaFileIO::Read(ctx, path);
    ASSERT_TRUE(out_result.IsOk());
    const MetaFileData& out = out_result.Value();

    EXPECT_EQ(in.AssetUUID, out.AssetUUID);
    EXPECT_STREQ(in.ImporterName,     out.ImporterName);
    EXPECT_STREQ(in.LastSourceSha256, out.LastSourceSha256);
    EXPECT_EQ(in.LastImportTimeNs,    out.LastImportTimeNs);
    ASSERT_EQ(out.SettingsCount, 1u);
    EXPECT_STREQ(out.Settings[0].Key,   "FlipUVs");
    EXPECT_STREQ(out.Settings[0].Value, "true");
}
```

### Test 2 — Read returns Fail when file does not exist
```cpp
TEST(MetaFileIO, ReadMissingReturnsError)
{
    MemoryVFSContext ctx;  // empty filesystem
    VFSPath path = VFSPath::Parse("/project/ghost.glb").Value();
    auto result = MetaFileIO::Read(ctx, path);
    EXPECT_FALSE(result.IsOk());
}
```

### Test 3 — GetOrCreate on missing file creates .meta and returns New
```cpp
TEST(MetaFileIO, GetOrCreateNewFile)
{
    MemoryVFSContext ctx;
    ctx.WriteFile("/project/mesh.glb", "dummy_binary_content");

    VFSPath path = VFSPath::Parse("/project/mesh.glb").Value();
    auto result = MetaFileIO::GetOrCreate(ctx, path, "AssimpImporter", "sha_first");

    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().Status, ImportStatus::New);
    EXPECT_FALSE(result.Value().AssetUUID.is_nil());

    // .meta file must now exist on VFS
    EXPECT_TRUE(ctx.FileExists("/project/mesh.glb.meta"));
}
```

### Test 4 — GetOrCreate with matching SHA returns UpToDate
```cpp
TEST(MetaFileIO, GetOrCreateMatchingSHAReturnsUpToDate)
{
    MemoryVFSContext ctx;
    VFSPath path = VFSPath::Parse("/project/mesh.glb").Value();

    // First call creates it
    auto first = MetaFileIO::GetOrCreate(ctx, path, "AssimpImporter", "sha_abc");
    ASSERT_TRUE(first.IsOk());

    // Second call with same SHA
    auto second = MetaFileIO::GetOrCreate(ctx, path, "AssimpImporter", "sha_abc");
    ASSERT_TRUE(second.IsOk());
    EXPECT_EQ(second.Value().Status, ImportStatus::UpToDate);
    EXPECT_EQ(first.Value().AssetUUID, second.Value().AssetUUID);  // UUID must not change
}
```

### Test 5 — GetOrCreate with different SHA returns Stale, UUID unchanged
```cpp
TEST(MetaFileIO, GetOrCreateChangedSHAReturnsStale)
{
    MemoryVFSContext ctx;
    VFSPath path = VFSPath::Parse("/project/mesh.glb").Value();

    auto first = MetaFileIO::GetOrCreate(ctx, path, "AssimpImporter", "sha_old");
    ASSERT_TRUE(first.IsOk());
    uuids::uuid original_uuid = first.Value().AssetUUID;

    auto second = MetaFileIO::GetOrCreate(ctx, path, "AssimpImporter", "sha_new");
    ASSERT_TRUE(second.IsOk());
    EXPECT_EQ(second.Value().Status, ImportStatus::Stale);
    EXPECT_EQ(second.Value().AssetUUID, original_uuid);  // UUID preserved across reimport
}
```

### Test 6 — MetaPathFor appends .meta suffix
```cpp
TEST(MetaFileIO, MetaPathForAppendsMetaSuffix)
{
    VFSPath asset = VFSPath::Parse("/project/textures/diffuse.png").Value();
    VFSPath meta  = MetaFileIO::MetaPathFor(asset);
    EXPECT_STREQ(meta.CStr(), "/project/textures/diffuse.png.meta");
}
```

### Test 7 — Truncated key/value strings do not overflow buffers
```cpp
TEST(MetaFileIO, OverlongSettingsAreTruncated)
{
    MemoryVFSContext ctx;
    VFSPath path = VFSPath::Parse("/project/x.glb").Value();

    // Build a JSON .meta with a key that is 200 chars long
    std::string long_key(200, 'k');
    std::string json = R"({"uuid":"550e8400-e29b-41d4-a716-446655440000",)"
                       R"("importer":"Test","source_sha256":"aaa","import_time_ns":0,)"
                       R"("artifact_path":"","settings":[{"key":")" + long_key + R"(","value":"v"}]})";
    ctx.WriteFile("/project/x.glb.meta", json);

    auto result = MetaFileIO::Read(ctx, path);
    ASSERT_TRUE(result.IsOk());
    // Key must be null-terminated within 64 bytes
    EXPECT_EQ(result.Value().Settings[0].Key[63], '\0');
}
```

### Test 8 — Malformed JSON returns Fail
```cpp
TEST(MetaFileIO, MalformedJSONReturnsError)
{
    MemoryVFSContext ctx;
    VFSPath path = VFSPath::Parse("/project/x.glb").Value();
    ctx.WriteFile("/project/x.glb.meta", "{not valid json{{{{");

    auto result = MetaFileIO::Read(ctx, path);
    EXPECT_FALSE(result.IsOk());
}
```

### Test 9 — ImportStatus field is never written to JSON
```cpp
TEST(MetaFileIO, StatusNotSerialised)
{
    MemoryVFSContext ctx;
    VFSPath path = VFSPath::Parse("/project/x.glb").Value();

    MetaFileData d{};
    d.AssetUUID = uuids::uuid_random_generator{}();
    d.Status    = ImportStatus::Stale;   // should NOT appear in output
    MetaFileIO::Write(ctx, path, d);

    std::string raw = ctx.ReadRaw("/project/x.glb.meta");
    EXPECT_EQ(raw.find("status"), std::string::npos);
    EXPECT_EQ(raw.find("stale"),  std::string::npos);
}
```

### Test 10 — Settings count capped at META_MAX_SETTINGS
```cpp
TEST(MetaFileIO, SettingsCountCappedAtMax)
{
    MemoryVFSContext ctx;
    VFSPath path = VFSPath::Parse("/project/x.glb").Value();

    // Build JSON with META_MAX_SETTINGS + 5 settings entries
    nlohmann::json j;
    j["uuid"]           = uuids::to_string(uuids::uuid_random_generator{}());
    j["importer"]       = "Test";
    j["source_sha256"]  = "aaa";
    j["import_time_ns"] = 0;
    j["artifact_path"]  = "";
    j["settings"]       = nlohmann::json::array();
    for (int i = 0; i < static_cast<int>(META_MAX_SETTINGS) + 5; ++i)
        j["settings"].push_back({{"key", std::to_string(i)}, {"value", "v"}});
    ctx.WriteFile("/project/x.glb.meta", j.dump());

    auto result = MetaFileIO::Read(ctx, path);
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value().SettingsCount, META_MAX_SETTINGS);
}
```

---

## 10. Deliverables Checklist

- [ ] `ZEngine/VFS/Meta/ImportStatus.h`
- [ ] `ZEngine/VFS/Meta/MetaFileData.h`
- [ ] `ZEngine/VFS/Meta/MetaFileIO.h` + `MetaFileIO.cpp`
- [ ] `AssetManager::GetOrCreateUUID()` added
- [ ] `AssimpImporter`: UUID generation replaced with `MetaFileIO::GetOrCreate`
- [ ] `VFSScanner::ScanDirectory`: `.meta` creation integrated, `ScanStats` updated
- [ ] `tests/VFS/MetaFileIOTest.cpp` (10 tests)
- [ ] `.gitignore`: ensure `*.meta` is NOT excluded; `*.zasset` IS excluded
- [ ] Manual smoke test: delete `mesh.glb.meta`, restart editor → same UUID reappears; modify `mesh.glb` → UUID preserved, SHA updated
