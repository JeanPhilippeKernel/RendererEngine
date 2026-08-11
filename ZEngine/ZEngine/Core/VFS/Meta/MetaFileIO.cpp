#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <nlohmann/json.hpp>
#include <rapidhash.h>
#include <uuid.h>
#include <chrono>
#include <cstring>
#include <random>

namespace ZEngine::Core::VFS
{
    namespace
    {
        // .meta files are small JSON — 8 KB is a generous upper bound.
        static constexpr size_t k_meta_read_cap = 8192;

        static int64_t          NowNs()
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }

        static void CopyStr(const std::string& src, char* dst, size_t cap)
        {
            size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
            std::memcpy(dst, src.data(), n);
            dst[n] = '\0';
        }
    } // namespace

    // -------------------------------------------------------------------------
    // MetaFileIO
    // -------------------------------------------------------------------------

    VFSPath MetaFileIO::MetaPathFor(const VFSPath& asset_path)
    {
        char        buf[MAX_FILE_PATH_COUNT] = {};
        const char* raw                      = asset_path.CStr();
        size_t      len                      = Helpers::secure_strlen(raw);
        size_t      avail                    = MAX_FILE_PATH_COUNT - 1;
        size_t      copy                     = len < avail ? len : avail;
        Helpers::secure_memcpy(buf, MAX_FILE_PATH_COUNT, raw, copy);
        const char suffix[] = ".meta";
        size_t     suf_len  = sizeof(suffix) - 1;
        if (copy + suf_len < MAX_FILE_PATH_COUNT)
            Helpers::secure_memcpy(buf + copy, MAX_FILE_PATH_COUNT - copy, suffix, suf_len + 1);
        return VFSPath::Parse(buf).Value();
    }

    VFSResult<MetaFileData> MetaFileIO::Read(IVFSContext& ctx, const VFSPath& asset_path)
    {
        VFSPath meta_path   = MetaPathFor(asset_path);

        auto    open_result = ctx.Open(meta_path, VFSOpenFlags::Read);
        if (open_result.Failed())
            return VFSResult<MetaFileData>::Fail(open_result.Error());

        IVFSFile* file        = open_result.Value();

        auto      size_result = file->Size();
        if (size_result.Failed())
        {
            ctx.Close(file);
            return VFSResult<MetaFileData>::Fail(size_result.Error());
        }

        uint64_t size = size_result.Value();
        if (size >= k_meta_read_cap)
        {
            ctx.Close(file);
            return VFSResult<MetaFileData>::Fail(VFSError::Corrupted);
        }

        uint8_t buf[k_meta_read_cap];
        auto    read_result = file->ReadAll({buf, (size_t) size});
        ctx.Close(file);

        if (read_result.Failed())
            return VFSResult<MetaFileData>::Fail(read_result.Error());

        buf[size] = '\0';

        auto j    = nlohmann::json::parse(reinterpret_cast<const char*>(buf), nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded())
            return VFSResult<MetaFileData>::Fail(VFSError::Corrupted);

        MetaFileData out{};

        if (j.contains("uuid") && j["uuid"].is_string())
        {
            auto parsed = uuids::uuid::from_string(j["uuid"].get<std::string>());
            if (parsed.has_value())
                out.AssetUUID = parsed.value();
        }

        if (j.contains("importer") && j["importer"].is_string())
            CopyStr(j["importer"].get<std::string>(), out.ImporterName, sizeof(out.ImporterName));

        if (j.contains("source_hash") && j["source_hash"].is_number_unsigned())
            out.SourceHash = j["source_hash"].get<uint64_t>();

        if (j.contains("import_time_ns") && j["import_time_ns"].is_number_integer())
            out.LastImportTimeNs = j["import_time_ns"].get<int64_t>();

        if (j.contains("artifact_path") && j["artifact_path"].is_string())
            CopyStr(j["artifact_path"].get<std::string>(), out.ArtifactPath, sizeof(out.ArtifactPath));

        if (j.contains("settings") && j["settings"].is_array())
        {
            for (const auto& s : j["settings"])
            {
                if (out.SettingsCount >= META_MAX_SETTINGS)
                    break;
                if (!s.contains("key") || !s.contains("value"))
                    continue;
                auto& kv = out.Settings[out.SettingsCount++];
                CopyStr(s["key"].get<std::string>(), kv.Key, sizeof(kv.Key));
                CopyStr(s["value"].get<std::string>(), kv.Value, sizeof(kv.Value));
            }
        }

        out.Status = ImportStatus::Unknown;
        return VFSResult<MetaFileData>::Ok(out);
    }

    VFSResult<void> MetaFileIO::Write(IVFSContext& ctx, const VFSPath& asset_path, const MetaFileData& data)
    {
        nlohmann::json j;
        j["uuid"]           = uuids::to_string(data.AssetUUID);
        j["importer"]       = data.ImporterName;
        j["source_hash"]    = data.SourceHash;
        j["import_time_ns"] = data.LastImportTimeNs;
        j["artifact_path"]  = data.ArtifactPath;

        j["settings"]       = nlohmann::json::array();
        for (uint32_t i = 0; i < data.SettingsCount; ++i)
        {
            j["settings"].push_back({
                {  "key",   data.Settings[i].Key},
                {"value", data.Settings[i].Value},
            });
        }

        std::string serialized                   = j.dump(4);

        // Build tmp path: <meta_path>.tmp
        VFSPath     meta_path                    = MetaPathFor(asset_path);
        char        tmp_buf[MAX_FILE_PATH_COUNT] = {};
        const char* meta_raw                     = meta_path.CStr();
        size_t      meta_len                     = Helpers::secure_strlen(meta_raw);
        size_t      avail                        = MAX_FILE_PATH_COUNT - 1;
        size_t      copy                         = meta_len < avail ? meta_len : avail;
        Helpers::secure_memcpy(tmp_buf, MAX_FILE_PATH_COUNT, meta_raw, copy);
        const char tmp_suffix[] = ".tmp";
        size_t     suf_len      = sizeof(tmp_suffix) - 1;
        if (copy + suf_len < MAX_FILE_PATH_COUNT)
            Helpers::secure_memcpy(tmp_buf + copy, MAX_FILE_PATH_COUNT - copy, tmp_suffix, suf_len + 1);
        VFSPath tmp_path    = VFSPath::Parse(tmp_buf).Value();

        auto    open_result = ctx.Open(tmp_path, VFSOpenFlags::Write);
        if (open_result.Failed())
            return VFSResult<void>::Fail(open_result.Error());

        IVFSFile*   file         = open_result.Value();
        const auto* bytes        = reinterpret_cast<const uint8_t*>(serialized.data());
        auto        write_result = file->Write({bytes, serialized.size()}, 0);
        auto        flush_result = file->Flush();
        file->Close();
        ctx.Close(file);

        if (write_result.Failed())
            return VFSResult<void>::Fail(write_result.Error());
        if (flush_result.Failed())
            return VFSResult<void>::Fail(flush_result.Error());

        return ctx.Rename(tmp_path, meta_path);
    }

    VFSResult<MetaFileData> MetaFileIO::GetOrCreate(IVFSContext& ctx, const VFSPath& asset_path, const char* importer_name, uint64_t current_hash)
    {
        auto read_result = Read(ctx, asset_path);

        if (read_result.Succeeded())
        {
            MetaFileData& existing = read_result.Value();

            if (existing.SourceHash == current_hash)
            {
                existing.Status = ImportStatus::UpToDate;
                return VFSResult<MetaFileData>::Ok(existing);
            }

            existing.SourceHash       = current_hash;
            existing.LastImportTimeNs = NowNs();
            existing.Status           = ImportStatus::Stale;
            Write(ctx, asset_path, existing); // best-effort; ignore failure
            return VFSResult<MetaFileData>::Ok(existing);
        }

        // No .meta or corrupt .meta — generate a fresh UUID.
        MetaFileData fresh{};
        {
            std::random_device           rd;
            std::mt19937                 generator(rd());
            uuids::uuid_random_generator gen{generator};
            fresh.AssetUUID = gen();
        }
        Helpers::secure_strcpy(fresh.ImporterName, sizeof(fresh.ImporterName), importer_name);
        fresh.SourceHash       = current_hash;
        fresh.LastImportTimeNs = NowNs();
        fresh.Status           = ImportStatus::New;
        Write(ctx, asset_path, fresh);
        return VFSResult<MetaFileData>::Ok(fresh);
    }

    VFSResult<uint64_t> MetaFileIO::ComputeHash(IVFSContext& ctx, const VFSPath& asset_path)
    {
        auto open_result = ctx.Open(asset_path, VFSOpenFlags::Read);
        if (open_result.Failed())
            return VFSResult<uint64_t>::Fail(open_result.Error());

        IVFSFile* file = open_result.Value();

        // Stream through the file in 4 KB chunks, chaining rapidhash via the seed parameter.
        uint8_t   chunk[4096];
        uint64_t  hash   = 0;
        uint64_t  offset = 0;

        while (true)
        {
            auto read_result = file->Read({chunk, sizeof(chunk)}, offset);
            if (read_result.Failed())
            {
                ctx.Close(file);
                return VFSResult<uint64_t>::Fail(read_result.Error());
            }
            size_t n = read_result.Value();
            if (n == 0)
                break;
            hash    = rapidhash_withSeed(chunk, n, hash);
            offset += (uint64_t) n;
        }
        ctx.Close(file);
        return VFSResult<uint64_t>::Ok(hash);
    }

} // namespace ZEngine::Core::VFS
