#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/ZEngineDef.h>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace ZEngine::Core::VFS
{
    void AssetRegistry::Initialize(Core::Memory::ArenaAllocator* arena, uint64_t scratch_size)
    {
        arena->CreateSubArena(scratch_size, &m_scratch);
        m_index.Initialize(arena);
        m_graph.Initialize(arena);
    }

    RegisterResult AssetRegistry::Register(const uuids::uuid& uuid, Managers::AssetType type, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta)
    {
        return m_index.Register(uuid, type, path, meta);
    }

    RegisterResult AssetRegistry::RegisterLoaded(const uuids::uuid& uuid, Managers::AssetType type, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta, Managers::AssetHandle slot_handle)
    {
        RegisterResult result = m_index.Register(uuid, type, path, meta);
        if (!result.IsOk())
        {
            // DuplicateUUID means VFSScanner pre-registered this file. Update the
            // existing record's SlotHandle so the RRM callback receives the correct slot.
            if (result.Error == RegisterError::DuplicateUUID)
            {
                Helpers::Handle<AssetRecord> h   = m_index.FindByUUID(uuid);
                AssetRecord*                 rec = m_index.Access(h);
                if (rec)
                {
                    rec->SlotHandle = slot_handle;
                    rec->State      = AssetState::Loaded;
                    return RegisterResult{h};
                }
            }
            return result;
        }

        AssetRecord* rec = m_index.Access(result.Handle);
        ZENGINE_VALIDATE_ASSERT(rec != nullptr, "Register succeeded but Access returned null")
        rec->SlotHandle = slot_handle;
        rec->State      = AssetState::Loaded;
        return result;
    }

    bool AssetRegistry::Remove(const uuids::uuid& uuid)
    {
        m_graph.RemoveAsset(uuid);
        Helpers::Handle<AssetRecord> h = m_index.FindByUUID(uuid);
        if (!h.Valid())
            return false;
        return m_index.Remove(h);
    }

    bool AssetRegistry::Remove(Helpers::Handle<AssetRecord> handle)
    {
        AssetRecord* rec = m_index.Access(handle);
        if (!rec)
            return false;
        m_graph.RemoveAsset(rec->UUID);
        return m_index.Remove(handle);
    }

    bool AssetRegistry::AddDependency(const uuids::uuid& dependent, const uuids::uuid& dependency)
    {
        return m_graph.AddEdge(dependent, dependency);
    }

    bool AssetRegistry::RemoveDependency(const uuids::uuid& dependent, const uuids::uuid& dependency)
    {
        return m_graph.RemoveEdge(dependent, dependency);
    }

    bool AssetRegistry::SetState(const uuids::uuid& uuid, AssetState new_state)
    {
        Helpers::Handle<AssetRecord> h = m_index.FindByUUID(uuid);
        if (!h.Valid())
            return false;
        bool ok = m_index.SetState(h, new_state);
        if (ok && new_state == AssetState::Loaded)
        {
            if (m_reload_cb)
                m_reload_cb(m_reload_cb_ctx, std::span<const uuids::uuid>(&uuid, 1));
            if (m_ready_cb)
            {
                const AssetRecord* rec = m_index.Access(h);
                if (rec)
                    m_ready_cb(m_ready_cb_ctx, uuid, rec->SlotHandle);
            }
        }
        return ok;
    }

    bool AssetRegistry::UpdateMeta(const uuids::uuid& uuid, const Core::VFS::MetaFileData& new_meta, AssetState new_state)
    {
        Helpers::Handle<AssetRecord> h = m_index.FindByUUID(uuid);
        if (!h.Valid())
            return false;
        return m_index.Update(h, new_meta, new_state);
    }

    AssetRecord* AssetRegistry::FindByUUID(const uuids::uuid& uuid)
    {
        Helpers::Handle<AssetRecord> h = m_index.FindByUUID(uuid);
        return h.Valid() ? m_index.Access(h) : nullptr;
    }

    const AssetRecord* AssetRegistry::FindByUUID(const uuids::uuid& uuid) const
    {
        Helpers::Handle<AssetRecord> h = m_index.FindByUUID(uuid);
        return h.Valid() ? m_index.Access(h) : nullptr;
    }

    AssetRecord* AssetRegistry::FindByPath(const Core::VFS::VFSPath& path)
    {
        Helpers::Handle<AssetRecord> h = m_index.FindByPath(path);
        return h.Valid() ? m_index.Access(h) : nullptr;
    }

    const AssetRecord* AssetRegistry::FindByPath(const Core::VFS::VFSPath& path) const
    {
        Helpers::Handle<AssetRecord> h = m_index.FindByPath(path);
        return h.Valid() ? m_index.Access(h) : nullptr;
    }

    AssetRecord* AssetRegistry::Access(Helpers::Handle<AssetRecord> handle)
    {
        return handle.Valid() ? m_index.Access(handle) : nullptr;
    }

    void AssetRegistry::SetHotReloadCallback(void* ctx, void (*cb)(void*, std::span<const uuids::uuid>))
    {
        m_reload_cb_ctx = ctx;
        m_reload_cb     = cb;
    }

    void AssetRegistry::SetOnReadyCallback(void* ctx, void (*cb)(void*, const uuids::uuid&, Managers::AssetHandle))
    {
        m_ready_cb_ctx = ctx;
        m_ready_cb     = cb;
    }

    void AssetRegistry::SetOnStaleCallback(void* ctx, void (*cb)(void*, const uuids::uuid&))
    {
        m_stale_cb_ctx = ctx;
        m_stale_cb     = cb;
    }

    void AssetRegistry::OnAssetModified(const Core::VFS::VFSPath& path)
    {
        Helpers::Handle<AssetRecord> handle = m_index.FindByPath(path);
        if (!handle.Valid())
            return;

        AssetRecord* rec = m_index.Access(handle);
        if (!rec)
            return;

        m_index.SetState(handle, AssetState::Stale);

        m_scratch.Clear();
        Core::Containers::Array<uuids::uuid> cascade;
        cascade.init(&m_scratch, 64);

        m_graph.CollectCascade(rec->UUID, cascade, &m_scratch);

        // Mark all cascade members (skip index 0 — already marked above)
        for (uint32_t i = 1; i < cascade.size(); ++i)
        {
            Helpers::Handle<AssetRecord> dep_h = m_index.FindByUUID(cascade[i]);
            if (dep_h.Valid())
                m_index.SetState(dep_h, AssetState::Stale);
        }

        if (m_reload_cb)
            m_reload_cb(m_reload_cb_ctx, std::span<const uuids::uuid>(cascade.data(), cascade.size()));

        if (m_stale_cb)
        {
            for (uint32_t i = 0; i < cascade.size(); ++i)
                m_stale_cb(m_stale_cb_ctx, cascade[i]);
        }
    }

    void AssetRegistry::OnAssetDeleted(const Core::VFS::VFSPath& path)
    {
        Helpers::Handle<AssetRecord> handle = m_index.FindByPath(path);
        if (!handle.Valid())
            return;

        AssetRecord* rec = m_index.Access(handle);
        if (!rec)
            return;

        m_scratch.Clear();
        Core::Containers::Array<uuids::uuid> cascade;
        cascade.init(&m_scratch, 16);
        m_graph.CollectCascade(rec->UUID, cascade, &m_scratch);

        if (m_reload_cb && !cascade.empty())
            m_reload_cb(m_reload_cb_ctx, std::span<const uuids::uuid>(cascade.data(), cascade.size()));

        Remove(rec->UUID);
    }

    void AssetRegistry::OnAssetRenamed(const Core::VFS::VFSPath& old_path, const Core::VFS::VFSPath& new_path)
    {
        Helpers::Handle<AssetRecord> handle = m_index.FindByPath(old_path);
        if (!handle.Valid())
            return;
        m_index.Rename(handle, new_path);
    }

    void AssetRegistry::OnScanFileDiscovered(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, Managers::AssetType type)
    {
        auto meta_result = Core::VFS::MetaFileIO::Read(ctx, path);
        if (!meta_result.Succeeded())
            return;

        const Core::VFS::MetaFileData& meta     = meta_result.Value();

        Helpers::Handle<AssetRecord>   existing = m_index.FindByPath(path);
        if (existing.Valid())
        {
            AssetRecord* rec = m_index.Access(existing);
            if (rec && rec->Meta.SourceHash != meta.SourceHash)
            {
                m_index.SetState(existing, AssetState::Stale);
                m_index.Update(existing, meta, AssetState::Stale);
            }
            return;
        }

        Register(meta.AssetUUID, type, path, meta);
    }

    bool AssetRegistry::PassesFilter(const AssetRecord& rec, const QueryFilter& f) const
    {
        if (f.State.has_value() && rec.State != f.State.value())
            return false;
        if (f.NameLike != nullptr && f.NameLike[0] != '\0')
        {
            if (std::strstr(rec.Name, f.NameLike) == nullptr)
                return false;
        }
        if (f.Ext != nullptr && f.Ext[0] != '\0')
        {
            if (!ExtensionMatches(rec.Path, f.Ext))
                return false;
        }
        return true;
    }

    struct QueryCtx
    {
        const AssetRegistry*                                   registry;
        const AssetRegistry::QueryFilter*                      filter;
        Core::Containers::Array<Helpers::Handle<AssetRecord>>* out;
    };

    AssetRegistry::QueryResult AssetRegistry::Query(const QueryFilter& filter, Core::Memory::ArenaAllocator* arena) const
    {
        QueryResult result{};
        result.Handles.init(arena, 64);

        if (filter.Type.has_value())
        {
            auto                                                  type_span = m_index.FindByType(filter.Type.value());

            // Copy handles out before releasing the internal span
            Core::Containers::Array<Helpers::Handle<AssetRecord>> type_copy;
            type_copy.init(arena, static_cast<uint32_t>(type_span.size() ? type_span.size() : 1));
            for (auto& h : type_span)
                type_copy.push(h);

            for (uint32_t i = 0; i < type_copy.size(); ++i)
            {
                const AssetRecord* rec = m_index.Access(type_copy[i]);
                if (!rec)
                    continue;
                if (!PassesFilter(*rec, filter))
                    continue;
                result.Handles.push(type_copy[i]);
            }
        }
        else
        {
            struct ForEachCtx
            {
                const AssetRegistry*                                   self;
                const QueryFilter*                                     filter;
                Core::Containers::Array<Helpers::Handle<AssetRecord>>* out;
            };

            ForEachCtx fe_ctx{this, &filter, &result.Handles};
            m_index.ForEach(&fe_ctx, [](void* raw, Helpers::Handle<AssetRecord> h, const AssetRecord& rec) {
                auto* fc = static_cast<ForEachCtx*>(raw);
                if (!fc->self->PassesFilter(rec, *fc->filter))
                    return;
                fc->out->push(h); // handle passed directly — no re-entrant FindByUUID needed
            });
        }

        result.Count = static_cast<uint32_t>(result.Handles.size());
        return result;
    }

    uint32_t AssetRegistry::RecordCount() const
    {
        return m_index.Count();
    }
    uint32_t AssetRegistry::EdgeCount() const
    {
        return m_graph.EdgeCount();
    }

    uint32_t AssetRegistry::DumpGraphDOT(char* out_buf, uint32_t out_len) const
    {
        if (!out_buf || out_len < 16)
            return 0;
        int written = std::snprintf(out_buf, out_len, "digraph AssetDeps {\n");
        if (written < 0 || static_cast<uint32_t>(written) >= out_len)
            return 0;

        struct DotCtx
        {
            const AssetRegistry* self;
            char*                buf;
            uint32_t             len;
            uint32_t             pos;
        };

        DotCtx dot{this, out_buf, out_len, static_cast<uint32_t>(written)};
        m_index.ForEach(&dot, [](void* raw, Helpers::Handle<AssetRecord>, const AssetRecord& rec) {
            auto* dc = static_cast<DotCtx*>(raw);
            if (dc->pos >= dc->len)
                return;

            Core::Containers::Array<uuids::uuid> deps;
            dc->self->m_graph.CopyDependents(rec.UUID, deps);
        });

        int tail = std::snprintf(out_buf + dot.pos, out_len - dot.pos, "}\n");
        if (tail < 0)
            return 0;
        return dot.pos + static_cast<uint32_t>(tail);
    }

    Managers::AssetType AssetRegistry::InferTypeFromExtension(const Core::VFS::VFSPath& path)
    {
        Core::VFS::VFSPathComponent ext = path.Extension();
        if (ext.Empty() || !ext.Data)
            return Managers::AssetType::MESH;

        if (ext.Equals(".png") || ext.Equals(".jpg") || ext.Equals(".jpeg") || ext.Equals(".hdr") || ext.Equals(".ktx") || ext.Equals(".ktx2"))
            return Managers::AssetType::TEXTURE;
        if (ext.Equals(".zematerial"))
            return Managers::AssetType::MATERIAL;
        if (ext.Equals(".zemesh"))
            return Managers::AssetType::MESH;

        return Managers::AssetType::MESH;
    }

    bool AssetRegistry::ExtensionMatches(const Core::VFS::VFSPath& path, const char* ext)
    {
        const char* raw  = path.CStr();
        size_t      rlen = std::strlen(raw);
        size_t      elen = std::strlen(ext);
        if (elen == 0 || rlen < elen)
            return false;
        const char* suffix = raw + rlen - elen;
        for (size_t i = 0; i < elen; ++i)
        {
            if (::tolower(static_cast<unsigned char>(suffix[i])) != ::tolower(static_cast<unsigned char>(ext[i])))
                return false;
        }
        return true;
    }

} // namespace ZEngine::Core::VFS
