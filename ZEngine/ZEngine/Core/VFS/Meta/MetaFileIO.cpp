#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <nlohmann/json.hpp>
#include <rapidhash.h>
#include <uuid.h>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <random>
#include <vector>

namespace ZEngine::Core::VFS
{
    namespace
    {
        static int64_t NowNs()
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }

        static void CopyStr(const std::string& src, char* dst, size_t cap)
        {
            size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
            Helpers::secure_memcpy(dst, cap, src.data(), n);
            dst[n] = '\0';
        }
    } // namespace

    // MetaFileIO

    VFSResult<VFSPath> MetaFileIO::MetaPathFor(const VFSPath& asset_path)
    {
        if (!asset_path.IsValid() || asset_path.IsRoot())
            return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
        return VFSPath::Parse((std::string(asset_path.CStr()) + ".meta").c_str());
    }

    VFSResult<MetaFileData> MetaFileIO::Read(IVFSContext& ctx, const VFSPath& asset_path)
    {
        auto meta_path = MetaPathFor(asset_path);
        if (meta_path.Failed())
            return VFSResult<MetaFileData>::Fail(meta_path.Error());
        auto open_result = ctx.Open(meta_path.Value(), VFSOpenFlags::Read);
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
        if (size > MaxFileSize)
        {
            ctx.Close(file);
            return VFSResult<MetaFileData>::Fail(VFSError::SizeLimitExceeded);
        }

        std::vector<uint8_t> buf(static_cast<size_t>(size));
        auto                 read_result = file->ReadAll({buf.data(), buf.size()});
        ctx.Close(file);

        if (read_result.Failed())
            return VFSResult<MetaFileData>::Fail(read_result.Error());
        if (read_result.Value() != size)
            return VFSResult<MetaFileData>::Fail(VFSError::IOError);

        auto j = nlohmann::json::parse(buf.begin(), buf.end(), nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded() || !j.is_object())
            return VFSResult<MetaFileData>::Fail(VFSError::Corrupted);

        MetaFileData out{};

        if (!j.contains("uuid") || !j["uuid"].is_string())
            return VFSResult<MetaFileData>::Fail(VFSError::Corrupted);
        const auto parsed = uuids::uuid::from_string(j["uuid"].get<std::string>());
        if (!parsed.has_value() || parsed->is_nil())
            return VFSResult<MetaFileData>::Fail(VFSError::Corrupted);
        out.AssetUUID = *parsed;

        if (j.contains("importer") && j["importer"].is_string())
            CopyStr(j["importer"].get<std::string>(), out.ImporterName, sizeof(out.ImporterName));

        if (j.contains("source_path") && j["source_path"].is_string())
            CopyStr(j["source_path"].get<std::string>(), out.SourcePath, sizeof(out.SourcePath));

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
                if (!s.contains("key") || !s.contains("value") || !s["key"].is_string() || !s["value"].is_string())
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
        auto meta_path = MetaPathFor(asset_path);
        if (meta_path.Failed())
            return VFSResult<void>::Fail(meta_path.Error());
        if (data.AssetUUID.is_nil() || data.SettingsCount > META_MAX_SETTINGS)
            return VFSResult<void>::Fail(VFSError::Corrupted);
        nlohmann::json j;
        j["uuid"]           = uuids::to_string(data.AssetUUID);
        j["importer"]       = data.ImporterName;
        j["source_path"]    = data.SourcePath;
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

        const std::string serialized = j.dump(4);
        if (serialized.size() > MaxFileSize)
            return VFSResult<void>::Fail(VFSError::SizeLimitExceeded);
        return WriteFileAtomically(ctx, meta_path.Value(), {reinterpret_cast<const uint8_t*>(serialized.data()), serialized.size()});
    }

    VFSResult<MetaFileData> MetaFileIO::GetOrCreate(IVFSContext& ctx, const VFSPath& asset_path, const char* importer_name, uint64_t current_hash, bool use_embedded_identity)
    {
        // Scanner and importer callers must not mint different identities for
        // the same absent sidecar. Writes remain atomic; serialize this RMW.
        static std::mutex identity_mutex;
        std::lock_guard   identity_lock(identity_mutex);
        auto              read_result = Read(ctx, asset_path);

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
            auto write                = Write(ctx, asset_path, existing);
            if (write.Failed())
                return VFSResult<MetaFileData>::Fail(write.Error());
            return VFSResult<MetaFileData>::Ok(existing);
        }

        if (read_result.Error() != VFSError::NotFound && read_result.Error() != VFSError::Corrupted)
            return VFSResult<MetaFileData>::Fail(read_result.Error());

        // Resolve and persist identity under the same lock; scanners must not seed
        // sidecars separately or mint random identities for unreadable cooked assets.
        MetaFileData fresh{};
        if (use_embedded_identity && (asset_path.Extension().Equals(".zemesh") || asset_path.Extension().Equals(".zematerial")))
        {
            auto embedded = ReadEmbeddedAssetUUID(ctx, asset_path);
            if (embedded.Failed())
                return VFSResult<MetaFileData>::Fail(embedded.Error());
            fresh.AssetUUID = embedded.Value();
        }
        else
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
        auto write             = Write(ctx, asset_path, fresh);
        if (write.Failed())
            return VFSResult<MetaFileData>::Fail(write.Error());
        return VFSResult<MetaFileData>::Ok(fresh);
    }

    VFSResult<uint64_t> MetaFileIO::ComputeHash(IVFSContext& ctx, const VFSPath& asset_path)
    {
        auto open_result = ctx.Open(asset_path, VFSOpenFlags::Read);
        if (open_result.Failed())
            return VFSResult<uint64_t>::Fail(open_result.Error());

        IVFSFile* file        = open_result.Value();

        auto      size_result = file->Size();
        if (size_result.Failed())
        {
            ctx.Close(file);
            return VFSResult<uint64_t>::Fail(size_result.Error());
        }

        // Stream through the file in 4 KB chunks, chaining rapidhash via the seed parameter.
        uint8_t  chunk[4096];
        uint64_t hash   = 0;
        uint64_t offset = 0;

        while (offset < size_result.Value())
        {
            const size_t count  = static_cast<size_t>(std::min<uint64_t>(sizeof(chunk), size_result.Value() - offset));
            size_t       filled = 0;
            while (filled < count)
            {
                auto read = file->Read({chunk + filled, count - filled}, offset + filled);
                if (read.Failed() || read.Value() == 0 || read.Value() > count - filled)
                {
                    ctx.Close(file);
                    return VFSResult<uint64_t>::Fail(read.Failed() ? read.Error() : VFSError::IOError);
                }
                filled += read.Value();
            }
            // Keep chunk boundaries stable even when the backend returns partial reads.
            hash    = rapidhash_withSeed(chunk, count, hash);
            offset += count;
        }
        ctx.Close(file);
        return VFSResult<uint64_t>::Ok(hash);
    }

} // namespace ZEngine::Core::VFS
