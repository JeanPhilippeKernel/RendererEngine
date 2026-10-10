#include <ZEngine/Core/VFS/Registry/AssetIndex.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <cstring>

namespace ZEngine::Core::VFS
{
    void AssetIndex::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        m_arena = arena;
        m_handles.Initialize(arena, MAX_ASSETS);
        m_by_uuid.init(arena, MAX_ASSETS);
        m_by_path.init(arena, MAX_ASSETS);
        for (uint8_t i = 0; i < ASSET_TYPE_COUNT; ++i)
            m_by_type[i].init(arena, MAX_ASSETS / ASSET_TYPE_COUNT);
    }

    RegisterResult AssetIndex::Register(const uuids::uuid& uuid, Managers::AssetType type, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta)
    {
        std::unique_lock lock(m_mutex);

        if (m_by_uuid.contains(uuid))
            return {.Error = RegisterError::DuplicateUUID};

        // Only check path uniqueness when a real path is provided.
        if (path.IsValid() && m_by_path.contains(path.Hash()))
            return {.Error = RegisterError::DuplicatePath};

        Helpers::Handle<AssetRecord> handle = m_handles.Create();
        if (!handle.Valid())
            return {.Error = RegisterError::SlotExhausted};

        AssetRecord& rec    = m_handles[handle];
        rec.UUID            = uuid;
        rec.Type            = type;
        rec.Path            = path;
        rec.Meta.SourceHash = meta.SourceHash;
        Helpers::secure_strcpy(rec.Meta.ImporterName, sizeof(rec.Meta.ImporterName), meta.ImporterName);
        Helpers::secure_strcpy(rec.Meta.ArtifactPath, sizeof(rec.Meta.ArtifactPath), meta.ArtifactPath);
        rec.SlotHandle                 = 0;
        rec.State                      = AssetState::Registered;

        // Populate Name from path filename component
        Core::VFS::VFSPathComponent fn = path.Filename();
        if (!fn.Empty() && fn.Data != nullptr)
        {
            size_t n = fn.Length < MAX_ASSET_NAME_LEN - 1 ? fn.Length : MAX_ASSET_NAME_LEN - 1;
            Helpers::secure_memcpy(rec.Name, MAX_ASSET_NAME_LEN, fn.Data, n);
            rec.Name[n] = '\0';
        }

        m_by_uuid.insert(uuid, handle);
        if (path.IsValid())
            m_by_path.insert(path.Hash(), handle);

        uint8_t type_idx = static_cast<uint8_t>(type);
        if (type_idx < ASSET_TYPE_COUNT)
            m_by_type[type_idx].push(handle);

        return {handle, RegisterError::None};
    }

    bool AssetIndex::Update(Helpers::Handle<AssetRecord> handle, const Core::VFS::MetaFileData& new_meta, AssetState new_state)
    {
        std::unique_lock lock(m_mutex);
        AssetRecord*     rec = m_handles.Access(handle);
        if (!rec)
            return false;
        rec->Meta.SourceHash = new_meta.SourceHash;
        Helpers::secure_strcpy(rec->Meta.ImporterName, sizeof(rec->Meta.ImporterName), new_meta.ImporterName);
        Helpers::secure_strcpy(rec->Meta.ArtifactPath, sizeof(rec->Meta.ArtifactPath), new_meta.ArtifactPath);
        rec->State = new_state;
        return true;
    }

    bool AssetIndex::SetState(Helpers::Handle<AssetRecord> handle, AssetState new_state)
    {
        std::unique_lock lock(m_mutex);
        AssetRecord*     rec = m_handles.Access(handle);
        if (!rec)
            return false;
        rec->State = new_state;
        return true;
    }

    bool AssetIndex::Remove(Helpers::Handle<AssetRecord> handle)
    {
        std::unique_lock lock(m_mutex);
        AssetRecord*     rec = m_handles.Access(handle);
        if (!rec)
            return false;

        m_by_uuid.remove(rec->UUID);
        m_by_path.remove(rec->Path.Hash());

        uint8_t type_idx = static_cast<uint8_t>(rec->Type);
        if (type_idx < ASSET_TYPE_COUNT)
        {
            auto& bucket = m_by_type[type_idx];
            for (size_t i = 0; i < bucket.size(); ++i)
            {
                if (bucket[i].Index == handle.Index && bucket[i].Generation == handle.Generation)
                {
                    bucket[i] = bucket[bucket.size() - 1];
                    bucket.pop();
                    break;
                }
            }
        }

        m_handles.Remove(handle);
        return true;
    }

    bool AssetIndex::Rename(Helpers::Handle<AssetRecord> handle, const Core::VFS::VFSPath& new_path)
    {
        std::unique_lock lock(m_mutex);
        AssetRecord*     rec = m_handles.Access(handle);
        if (!rec)
            return false;

        m_by_path.remove(rec->Path.Hash());
        rec->Path = new_path;
        m_by_path.insert(new_path.Hash(), handle);

        Core::VFS::VFSPathComponent fn = new_path.Filename();
        if (!fn.Empty() && fn.Data != nullptr)
        {
            size_t n = fn.Length < MAX_ASSET_NAME_LEN - 1 ? fn.Length : MAX_ASSET_NAME_LEN - 1;
            Helpers::secure_memcpy(rec->Name, MAX_ASSET_NAME_LEN, fn.Data, n);
            rec->Name[n] = '\0';
        }

        return true;
    }

    AssetRecord* AssetIndex::Access(Helpers::Handle<AssetRecord> handle)
    {
        std::shared_lock lock(m_mutex);
        return m_handles.Access(handle);
    }

    const AssetRecord* AssetIndex::Access(Helpers::Handle<AssetRecord> handle) const
    {
        std::shared_lock lock(m_mutex);
        // HandleManager::Access is not const — safe to cast since we hold a shared lock
        // and the access is read-only.
        return const_cast<Helpers::HandleManager<AssetRecord>&>(m_handles).Access(handle);
    }

    Helpers::Handle<AssetRecord> AssetIndex::FindByUUID(const uuids::uuid& uuid) const
    {
        std::shared_lock                    lock(m_mutex);
        const Helpers::Handle<AssetRecord>* h = m_by_uuid.find(uuid);
        return h ? *h : Helpers::Handle<AssetRecord>{};
    }

    Helpers::Handle<AssetRecord> AssetIndex::FindByPath(const Core::VFS::VFSPath& path) const
    {
        std::shared_lock                    lock(m_mutex);
        const Helpers::Handle<AssetRecord>* h = m_by_path.find(path.Hash());
        return h ? *h : Helpers::Handle<AssetRecord>{};
    }

    std::span<const Helpers::Handle<AssetRecord>> AssetIndex::FindByType(Managers::AssetType type) const
    {
        uint8_t idx = static_cast<uint8_t>(type);
        if (idx >= ASSET_TYPE_COUNT)
            return {};
        std::shared_lock lock(m_mutex);
        const auto&      bucket = m_by_type[idx];
        return std::span<const Helpers::Handle<AssetRecord>>(bucket.data(), bucket.size());
    }

    uint32_t AssetIndex::FindByNameSubstring(const char* substr, Core::Containers::Array<Helpers::Handle<AssetRecord>>& out) const
    {
        if (!substr || substr[0] == '\0')
            return 0;

        uint32_t         count = 0;
        auto&            hm    = const_cast<Helpers::HandleManager<AssetRecord>&>(m_handles);
        std::shared_lock lock(m_mutex);

        uint32_t         head = hm.Head();
        for (uint32_t i = 0; i < head; ++i)
        {
            Helpers::Handle<AssetRecord> h = hm.ToHandle(i);
            if (!h.Valid())
                continue;
            const AssetRecord* rec = hm.Access(h);
            if (!rec)
                continue;
            if (std::strstr(rec->Name, substr) != nullptr)
            {
                out.push(h);
                ++count;
            }
        }
        return count;
    }

    bool AssetIndex::IsLive(Helpers::Handle<AssetRecord> handle) const
    {
        std::shared_lock lock(m_mutex);
        return m_handles.IsLive(handle);
    }

    void AssetIndex::ForEach(void* ctx, void (*visitor)(void*, Helpers::Handle<AssetRecord>, const AssetRecord&)) const
    {
        auto&            hm = const_cast<Helpers::HandleManager<AssetRecord>&>(m_handles);
        std::shared_lock lock(m_mutex);
        uint32_t         head = hm.Head();
        for (uint32_t i = 0; i < head; ++i)
        {
            Helpers::Handle<AssetRecord> h = hm.ToHandle(i);
            if (!h.Valid())
                continue;
            const AssetRecord* rec = hm.Access(h);
            if (rec)
                visitor(ctx, h, *rec);
        }
    }

    uint32_t AssetIndex::Count() const
    {
        return static_cast<uint32_t>(m_handles.Size());
    }

    uint32_t AssetIndex::Capacity() const
    {
        return MAX_ASSETS;
    }

} // namespace ZEngine::Core::VFS
