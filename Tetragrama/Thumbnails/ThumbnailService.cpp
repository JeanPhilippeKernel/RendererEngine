#include <Tetragrama/Thumbnails/ThumbnailService.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Profiling/Profiling.h>
#include <ZEngine/Rendering/Renderers/Graphics/AssetPreviewPass.h>
#include <algorithm>
#include <cstdio>

using namespace ZEngine::Core::VFS;
using namespace ZEngine::Rendering;

namespace Tetragrama::Thumbnails
{
    void ThumbnailService::AssetLoad::Run(void* context)
    {
        // A separate service lease, not an evictable card, owns this request.
        // Engine shutdown joins workers before Deinitialize retires leases,
        // including queued tasks the pool discards without executing.
        auto& work     = *static_cast<AssetLoad*>(context);
        work.Succeeded = work.Load();
        work.Done.store(true, std::memory_order_release); // Last request access; UI may now reuse it.
    }

    bool ThumbnailService::AssetLoad::Load()
    {
        if (Cancelled.load(std::memory_order_acquire) || !Assets->Registry)
            return false;
        AssetRecordSnapshot current;
        if (!Assets->Registry->TryReadRecord(Record.Record.UUID, &current) || !IsPreviewable(current))
            return false;
        if (current.Record.Type != Record.Record.Type || current.Record.Path != Record.Record.Path || current.Record.Meta.SourceHash != Record.Record.Meta.SourceHash)
            return false;
        if (current.Record.Type == ZEngine::Managers::AssetType::MESH)
            return Assets->LoadMeshAsset(*VFS, current.Record.UUID, {}, &Cancelled).Succeeded();
        if (current.Record.Type != ZEngine::Managers::AssetType::MATERIAL)
            return false;

        // Each fixed load slot reuses its own transient material storage. Only
        // the worker touches it; deinitialization releases it after pool join.
        if (!Scratch.m_memory)
            Scratch.Initialize(ZMega(4), 0, "Thumbnail/MaterialRead");
        if (!Scratch.m_memory)
            return false;
        Scratch.Clear();
        return ZEngine::Managers::AssetManager::IngestMaterialFromUUID(&Scratch, current.Record.UUID);
    }

    bool ThumbnailService::SubmitAssetLoad(void*, void* context, void (*run)(void*))
    {
        auto* pool = ZEngine::Helpers::ThreadPoolHelper::Pool.get();
        if (pool)
            for (uint32_t worker = 0; worker < pool->WorkerCount; ++worker)
                if (pool->SubmitToWorker(worker, context, run))
                    return true;
        return false; // Never execute file IO inline on the UI/render thread.
    }

    bool ThumbnailService::EnsureAssetLoaded(const AssetRecordSnapshot& record)
    {
        using ZEngine::Managers::AssetManager;
        using ZEngine::Managers::AssetType;
        if (record.Record.Type == AssetType::TEXTURE)
            return true;
        auto* assets = AssetManager::Instance();
        if (!assets || assets->Registry != m_registry)
            return false;
        {
            std::unique_lock lock(assets->IngestMutex, std::try_to_lock);
            if (!lock.owns_lock())
                return false;
            uint32_t draws = 0;
            if (record.Record.Type == AssetType::MESH ? assets->TryGetMeshDrawCount(record.Record.UUID, &draws) : assets->UUIDToMaterialSlot.contains(record.Record.UUID))
                return true;
        }
        AssetLoad* available = nullptr;
        for (auto& work : m_asset_loads)
        {
            if (work.Active && work.Record.Record.UUID == record.Record.UUID && work.Record.Record.Meta.SourceHash == record.Record.Meta.SourceHash)
                return false; // Includes cancelled leases awaiting worker completion.
            if (!work.Active)
                available = &work;
        }
        if (!available || !m_vfs || !TakeSubmissionBudget())
            return false;
        auto* work   = available;
        work->Assets = assets;
        work->VFS    = m_vfs;
        work->Record = record;
        work->Cancelled.store(false, std::memory_order_relaxed);
        work->Done.store(false, std::memory_order_relaxed);
        work->Succeeded = false;
        work->Active    = true;
        if (!m_submit_asset_load(m_load_submit_context, work, &AssetLoad::Run))
        {
            work->Active = false;
            return false; // Bounded queue pressure retries on a later frame.
        }
        return false;
    }

    void ThumbnailService::PollAssetLoads()
    {
        for (auto& work : m_asset_loads)
        {
            if (!work.Active || !work.Done.load(std::memory_order_acquire))
                continue;
            if (!work.Succeeded && !work.Cancelled.load(std::memory_order_acquire))
                if (auto* entry = Find(work.Record.Record.UUID, work.Record.Record.Meta.SourceHash))
                    MarkFailed(*entry);
            work.Active = false;
        }
    }

    void ThumbnailService::CancelAssetLoads(const uuids::uuid& uuid)
    {
        for (auto& work : m_asset_loads)
            if (work.Active && work.Record.Record.UUID == uuid)
                work.Cancelled.store(true, std::memory_order_release);
    }

    void ThumbnailService::Initialize(AssetRegistry* registry, RenderResourceManager* resource_manager, cstring project_root, Renderers::AssetPreviewPass* previews, IVFSContext* vfs)
    {
        Deinitialize();
        if (!registry || !resource_manager || !project_root || project_root[0] == '\0' || ZEngine::Helpers::secure_strlen(project_root) >= sizeof(m_project_root))
        {
            return;
        }

        m_registry         = registry;
        m_resource_manager = resource_manager;
        m_previews         = previews;
        m_vfs              = vfs;
        ZEngine::Helpers::secure_strcpy(m_project_root, sizeof(m_project_root), project_root);
        const bool stale_listener   = m_registry->AddOnStaleListener(this, &ThumbnailService::OnAssetStale);
        const bool removed_listener = m_registry->AddOnRemovedListener(this, &ThumbnailService::OnAssetRemoved);
        if (!stale_listener || !removed_listener)
        {
            Deinitialize();
        }
    }

    void ThumbnailService::Deinitialize()
    {
        // As with the editor's other asset workers, callers join the pool first.
        // Reclaim even abandoned queued requests: no worker-owned heap wrapper.
        for (auto& work : m_asset_loads)
        {
            if (work.Active)
                work.Cancelled.store(true, std::memory_order_release);
            work.Active = false;
            work.Scratch.Shutdown();
        }
        m_vfs                 = nullptr;
        m_load_submit_context = nullptr;
        m_submit_asset_load   = &SubmitAssetLoad;
        if (m_registry)
        {
            m_registry->RemoveOnStaleListener(this, &ThumbnailService::OnAssetStale);
            m_registry->RemoveOnRemovedListener(this, &ThumbnailService::OnAssetRemoved);
        }
        for (Entry& entry : m_entries)
        {
            // RRM retains ownership of unfinished decode/upload work and retires
            // it asynchronously. Dropping pending handles here leaks its tracker.
            ReleaseAndClear(entry);
        }
        m_invalidations.clear();
        m_invalidation_overflow.value.store(false, std::memory_order_relaxed);
        m_registry             = nullptr;
        m_resource_manager     = nullptr;
        m_previews             = nullptr;
        m_project_root[0]      = '\0';
        m_frame                = 0;
        m_resource_retry_frame = 0;
        m_next_cleanup_frame   = 60;
        m_submitted_this_frame = 0;
        m_evictions            = 0;
        m_budget_deferrals     = 0;
    }

    ThumbnailService::Entry* ThumbnailService::Find(const uuids::uuid& uuid, uint64_t source_hash)
    {
        for (Entry& entry : m_entries)
        {
            if (entry.CurrentState != State::Empty && entry.CurrentState != State::Discarded && entry.AssetUUID == uuid && entry.SourceHash == source_hash)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    ThumbnailService::Entry* ThumbnailService::PrepareRequest(const uuids::uuid& uuid, uint64_t source_hash)
    {
        if (Entry* existing = Find(uuid, source_hash))
        {
            existing->LastUseFrame = m_frame;
            if (existing->CurrentState == State::Failed)
            {
                if (m_frame < existing->NextRetryFrame)
                {
                    return nullptr;
                }
                // Preserve FailureCount across actual retries, not just calls to
                // MarkFailed(), so a persistently bad source reaches the cap.
                existing->CurrentState   = State::Pending;
                existing->NextRetryFrame = 0;
            }
            return existing;
        }

        Invalidate(uuid);
        return Allocate(uuid, source_hash);
    }

    ThumbnailService::Entry* ThumbnailService::Allocate(const uuids::uuid& uuid, uint64_t source_hash)
    {
        Entry* oldest = nullptr;
        for (Entry& entry : m_entries)
        {
            if (entry.CurrentState == State::Empty)
            {
                entry.AssetUUID    = uuid;
                entry.SourceHash   = source_hash;
                entry.LastUseFrame = m_frame;
                entry.CurrentState = State::Pending;
                return &entry;
            }
            // Failed entries must not permanently consume the whole table. Do not
            // evict a resident texture already borrowed by this frame's UI tree.
            const bool evictable = entry.CurrentState == State::Failed || ((entry.CurrentState == State::Resident || entry.CurrentState == State::FullyTransparent || (entry.CurrentState == State::Pending && !entry.Texture.Valid())) && entry.LastUseFrame < m_frame);
            if (evictable && (!oldest || entry.LastUseFrame < oldest->LastUseFrame))
            {
                oldest = &entry;
            }
        }
        if (!oldest)
        {
            return nullptr;
        }

        ReleaseAndClear(*oldest);
        ++m_evictions;
        oldest->AssetUUID    = uuid;
        oldest->SourceHash   = source_hash;
        oldest->LastUseFrame = m_frame;
        oldest->CurrentState = State::Pending;
        return oldest;
    }

    bool ThumbnailService::BuildCachePath(const uuids::uuid& uuid, uint64_t source_hash, char (&out_path)[MAX_FILE_PATH_COUNT], uint64_t dependency_hash, uint32_t renderer_revision) const
    {
        if (m_project_root[0] == '\0')
        {
            return false;
        }

        char uuid_text[37] = {};
        uuids::to_string(uuid, uuid_text);
        const int count = renderer_revision ? std::snprintf(out_path, sizeof(out_path), "%s/.zodiacengine/cache/thumbnails/%s/%016llx-%016llx-%u-%u.png", m_project_root, uuid_text, static_cast<unsigned long long>(source_hash), static_cast<unsigned long long>(dependency_hash), kSchemaRevision, renderer_revision) : std::snprintf(out_path, sizeof(out_path), "%s/.zodiacengine/cache/thumbnails/%s/%016llx-%u.png", m_project_root, uuid_text, static_cast<unsigned long long>(source_hash), kSchemaRevision);
        if (count <= 0 || static_cast<size_t>(count) >= sizeof(out_path))
        {
            out_path[0] = '\0';
            return false;
        }
        return true;
    }

    bool ThumbnailService::IsPreviewable(const AssetRecordSnapshot& record)
    {
        using ZEngine::Managers::AssetType;
        return (record.Record.Type == AssetType::TEXTURE || record.Record.Type == AssetType::MESH || record.Record.Type == AssetType::MATERIAL) && !record.Record.UUID.is_nil() && record.Record.Path.IsValid() && (record.Record.State == AssetState::Registered || record.Record.State == AssetState::Loaded);
    }

    bool ThumbnailService::IsFileDecoded(const AssetRecordSnapshot& record) const
    {
        return record.Record.Type == ZEngine::Managers::AssetType::TEXTURE;
    }

    Textures::TextureHandle ThumbnailService::Acquire(const AssetRecordSnapshot& record, bool* fully_transparent, Materials::AlphaInfo* alpha_info)
    {
        ZENGINE_PROFILE_SCOPE("ThumbnailProfile.Acquire");
        if (fully_transparent)
            *fully_transparent = false;
        if (alpha_info)
            *alpha_info = {};
        if (!m_resource_manager)
        {
            return {};
        }
        if (!IsPreviewable(record))
        {
            // Stale/importing snapshots still carry the preceding SourceHash.
            // Never decode new source bytes or reuse a PNG under that old key.
            Invalidate(record.Record.UUID);
            return {};
        }

        const uuids::uuid uuid        = record.Record.UUID;
        const uint64_t    source_hash = record.Record.Meta.SourceHash;
        Entry*            entry       = PrepareRequest(uuid, source_hash);
        if (!entry)
            return {};
        if (entry->Kind != record.Record.Type && (entry->Texture.Valid() || entry->CurrentState != State::Pending))
        {
            Invalidate(uuid);
            entry = PrepareRequest(uuid, source_hash);
            if (!entry)
                return {};
        }
        entry->Kind = record.Record.Type;
        // Material fingerprints require their authored factors/dependencies.
        // Mesh fingerprints do not: let a warm PNG avoid geometry loading.
        if (record.Record.Type == ZEngine::Managers::AssetType::MATERIAL && !EnsureAssetLoaded(record))
            return {};
        uint64_t dependency_hash = 0;
        uint64_t persistent_hash = 0;
        bool     transparent     = false;
        if (record.Record.Type != ZEngine::Managers::AssetType::TEXTURE && (!m_previews || !Renderers::AssetPreviewPass::DependencyFingerprint(record, &dependency_hash, &persistent_hash, &transparent, alpha_info)))
        {
            Invalidate(uuid);
            return {};
        }
        if (entry->DependencyHash != dependency_hash || (transparent != (entry->CurrentState == State::FullyTransparent)))
        {
            Invalidate(uuid);
            entry = PrepareRequest(uuid, source_hash);
            if (!entry)
                return {};
        }
        entry->Kind           = record.Record.Type;
        entry->DependencyHash = dependency_hash;
        entry->PersistentHash = persistent_hash;
        if (transparent)
        {
            // A proven invisible constant surface needs neither a GPU target nor
            // a transparent PNG. Keep the result keyed/invalidated like any preview.
            entry->CurrentState = State::FullyTransparent;
            if (fully_transparent)
                *fully_transparent = true;
            return {};
        }
        if (entry->CurrentState == State::Resident)
        {
            if (!m_resource_manager->GetTexture(entry->Texture))
            {
                ReleaseAndClear(*entry);
                return {};
            }
            return entry->Texture;
        }
        if (entry->Texture.Valid())
        {
            return {};
        }
        char           cache_path[MAX_FILE_PATH_COUNT] = {};
        const uint32_t renderer_revision               = entry->Kind == ZEngine::Managers::AssetType::TEXTURE ? 0 : Renderers::AssetPreviewPass::RendererRevision;
        BuildCachePath(uuid, source_hash, cache_path, persistent_hash, renderer_revision);
        const RenderResourceManager::ThumbnailCacheIdentity identity = {.AssetUUID = uuid, .SourceHash = source_hash, .DependencyHash = persistent_hash, .SchemaRevision = kSchemaRevision, .RendererRevision = renderer_revision};
        if (entry->Kind != ZEngine::Managers::AssetType::TEXTURE)
        {
            // A cache-only worker miss is a normal transition, not a failed
            // preview. The next budgeted attempt renders through the graph.
            entry->GraphRequest = entry->CacheMiss || !cache_path[0];
            if (entry->GraphRequest && !EnsureAssetLoaded(record))
                return {};
            if (!TakeSubmissionBudget())
                return {};
            entry->Texture = entry->GraphRequest ? m_previews->Request(record, dependency_hash, cache_path, &identity) : m_resource_manager->SubmitThumbnailTextureFile(cache_path, kThumbnailExtent, true, cache_path, &identity, true);
            if (!entry->Texture.Valid() && !ReclaimForResourcePressure())
                MarkFailed(*entry);
            return {};
        }

        if (!TakeSubmissionBudget())
            return {};
        char native_path[MAX_FILE_PATH_COUNT] = {};
        record.Record.Path.ResolveNative(m_project_root, native_path, sizeof(native_path));
        // Lookup, metadata/checksum validation, corrupt-cache fallback and writes
        // all happen in the same bounded worker request, never during UI build.
        entry->Texture = m_resource_manager->SubmitThumbnailTextureFile(native_path, kThumbnailExtent, true, cache_path, &identity);
        if (!entry->Texture.Valid() && !ReclaimForResourcePressure())
        {
            MarkFailed(*entry);
        }
        return {};
    }

    ThumbnailService::Statistics ThumbnailService::GetStatistics() const
    {
        Statistics stats         = {};
        stats.SubmittedThisFrame = m_submitted_this_frame;
        stats.Evictions          = m_evictions;
        stats.BudgetDeferrals    = m_budget_deferrals;
        for (const auto& work : m_asset_loads)
            stats.AssetLoads += work.Active;
        for (const Entry& entry : m_entries)
        {
            if (entry.CurrentState == State::Resident)
                ++stats.Resident;
            else if (entry.CurrentState == State::FullyTransparent)
                ++stats.FullyTransparent;
            else if (entry.CurrentState == State::Pending && !entry.Texture.Valid())
                ++stats.Queued;
            else if (entry.CurrentState == State::Pending || entry.CurrentState == State::Discarded)
                ++stats.InFlight;
            else if (entry.CurrentState == State::Failed)
                ++stats.Failed;
        }
        stats.ResidentPixelBytes = static_cast<uint64_t>(stats.Resident) * kThumbnailExtent * kThumbnailExtent * 4;
        return stats;
    }

    bool ThumbnailService::TakeSubmissionBudget()
    {
        // Most visible cards arrive after the four frame attempts were spent.
        // Reject those in O(1), before scanning the cache for outstanding work.
        if (m_frame < m_resource_retry_frame || m_submitted_this_frame >= kSubmissionsPerFrame)
        {
            ++m_budget_deferrals;
            return false;
        }
        const auto stats = GetStatistics();
        if (stats.InFlight + stats.AssetLoads >= kMaxInFlight)
        {
            ++m_budget_deferrals;
            return false;
        }
        // Failed attempts count too, so allocator/queue pressure cannot cause
        // an unbounded burst of attempted allocations in one UI frame.
        ++m_submitted_this_frame;
        return true;
    }

    bool ThumbnailService::ReclaimForResourcePressure()
    {
        // Some devices expose far fewer bindless slots than the 512-entry CPU
        // table. Make room for the current view even before that table is full.
        Entry* oldest = nullptr;
        for (Entry& entry : m_entries)
            if (entry.CurrentState == State::Resident && entry.LastUseFrame < m_frame && (!oldest || entry.LastUseFrame < oldest->LastUseFrame))
                oldest = &entry;
        if (!oldest)
            return false;
        ReleaseAndClear(*oldest);
        ++m_evictions;
        // Release is timeline-deferred. Avoid further allocation/eviction bursts
        // while the render thread retires the reclaimed resource.
        m_resource_retry_frame = m_frame + 4;
        ++m_budget_deferrals;
        return true;
    }

    void ThumbnailService::Update()
    {
        ZENGINE_PROFILE_SCOPE("ThumbnailProfile.Update");
#if ZENGINE_PROFILING
        const auto stats = GetStatistics();
        ZENGINE_PROFILE_VALUE("ThumbnailProfile.Resident", stats.Resident);
        ZENGINE_PROFILE_VALUE("ThumbnailProfile.InFlight", stats.InFlight);
        ZENGINE_PROFILE_VALUE("ThumbnailProfile.AssetLoads", stats.AssetLoads);
        ZENGINE_PROFILE_VALUE("ThumbnailProfile.Queued", stats.Queued);
        ZENGINE_PROFILE_VALUE("ThumbnailProfile.Failed", stats.Failed);
        ZENGINE_PROFILE_VALUE("ThumbnailProfile.PixelBytes", stats.ResidentPixelBytes);
        ZENGINE_PROFILE_VALUE("ThumbnailProfile.Evictions", stats.Evictions);
        ZENGINE_PROFILE_VALUE("ThumbnailProfile.BudgetDeferrals", stats.BudgetDeferrals);
#endif
        ++m_frame;
        m_submitted_this_frame = 0;
        PollAssetLoads();
        if (m_invalidation_overflow.value.exchange(false, std::memory_order_acq_rel))
        {
            InvalidateAll();
            m_invalidations.clear();
        }
        else
        {
            Invalidation invalidation = {};
            while (m_invalidations.pop(invalidation))
            {
                Invalidate(invalidation.AssetUUID);
                m_next_cleanup_frame = std::min(m_next_cleanup_frame, m_frame + 120);
            }
        }

        if (!m_resource_manager)
        {
            return;
        }

        // Missing UUIDs are only meaningful once the startup/reconciliation scan
        // has finished. Retry a saturated worker queue later, never run inline.
        if (m_frame >= m_next_cleanup_frame)
        {
            auto* context = ZEngine::Engine::GetContext();
            if (context && !context->VFSScanner.IsScanning() && m_resource_manager->SubmitThumbnailCacheCleanup(m_project_root, kSchemaRevision, Renderers::AssetPreviewPass::RendererRevision))
                m_next_cleanup_frame = m_frame + 3600;
        }

        for (Entry& entry : m_entries)
        {
            if ((entry.CurrentState != State::Pending && entry.CurrentState != State::Discarded) || !entry.Texture.Valid())
            {
                continue;
            }
            auto state = RenderResourceManager::TextureDecodeState::Failed;
            if (!entry.GraphRequest)
                state = m_resource_manager->GetTextureDecodeState(entry.Texture);
            else if (m_previews)
            {
                const auto preview = m_previews->Observe(entry.Texture);
                state              = preview == Renderers::AssetPreviewPass::Result::Pending ? RenderResourceManager::TextureDecodeState::Pending : preview == Renderers::AssetPreviewPass::Result::Succeeded ? RenderResourceManager::TextureDecodeState::Succeeded : RenderResourceManager::TextureDecodeState::Failed;
            }
            if (state == RenderResourceManager::TextureDecodeState::Pending || state == RenderResourceManager::TextureDecodeState::Untracked)
            {
                continue;
            }

            if (state == RenderResourceManager::TextureDecodeState::Failed)
            {
                if (!entry.GraphRequest)
                    m_resource_manager->ForgetTextureDecode(entry.Texture);
                else if (m_previews)
                    m_previews->Forget(entry.Texture);
                if (entry.CurrentState == State::Discarded)
                {
                    ReleaseAndClear(entry);
                    continue;
                }
                if (entry.Kind != ZEngine::Managers::AssetType::TEXTURE && !entry.GraphRequest)
                {
                    m_resource_manager->ReleaseTexture(entry.Texture);
                    entry.Texture   = {};
                    entry.CacheMiss = true;
                    continue;
                }
                MarkFailed(entry);
                continue;
            }

            if (!entry.GraphRequest)
                m_resource_manager->ForgetTextureDecode(entry.Texture);
            else if (m_previews)
                m_previews->Forget(entry.Texture);
            if (entry.CurrentState == State::Discarded)
            {
                ReleaseAndClear(entry);
            }
            else
            {
                AssetRecordSnapshot current         = {};
                uint64_t            dependency_hash = 0;
                const bool          valid           = m_registry && m_registry->TryReadRecord(entry.AssetUUID, &current) && IsPreviewable(current) && current.Record.Type == entry.Kind && current.Record.Meta.SourceHash == entry.SourceHash && (entry.Kind == ZEngine::Managers::AssetType::TEXTURE || (Renderers::AssetPreviewPass::DependencyFingerprint(current, &dependency_hash) && dependency_hash == entry.DependencyHash));
                if (!valid)
                {
                    ReleaseAndClear(entry);
                }
                else
                {
                    entry.CurrentState = State::Resident;
                }
            }
        }
    }

    void ThumbnailService::QueueInvalidation(const uuids::uuid& uuid)
    {
        if (!m_invalidations.push({.AssetUUID = uuid}))
        {
            m_invalidation_overflow.value.store(true, std::memory_order_release);
        }
    }

    void ThumbnailService::Invalidate(const uuids::uuid& uuid)
    {
        CancelAssetLoads(uuid);
        for (Entry& entry : m_entries)
        {
            if (entry.CurrentState == State::Empty || entry.CurrentState == State::Discarded || entry.AssetUUID != uuid)
            {
                continue;
            }
            if (entry.CurrentState == State::Pending && entry.Texture.Valid())
            {
                entry.CurrentState = State::Discarded;
            }
            else
            {
                ReleaseAndClear(entry);
            }
        }
    }

    void ThumbnailService::InvalidateAll()
    {
        for (Entry& entry : m_entries)
        {
            if (entry.CurrentState == State::Empty || entry.CurrentState == State::Discarded)
            {
                continue;
            }
            if (entry.CurrentState == State::Pending && entry.Texture.Valid())
            {
                entry.CurrentState = State::Discarded;
            }
            else
            {
                ReleaseAndClear(entry);
            }
        }
    }

    void ThumbnailService::ReleaseAndClear(Entry& entry)
    {
        CancelAssetLoads(entry.AssetUUID);
        if (entry.Texture.Valid() && m_resource_manager && !(entry.GraphRequest && m_previews && m_previews->Discard(entry.Texture)))
        {
            m_resource_manager->ReleaseTexture(entry.Texture);
        }
        entry = {};
    }

    void ThumbnailService::MarkFailed(Entry& entry)
    {
        if (entry.Texture.Valid() && m_resource_manager && !(entry.GraphRequest && m_previews && m_previews->Discard(entry.Texture)))
        {
            m_resource_manager->ReleaseTexture(entry.Texture);
        }
        entry.Texture      = {};
        entry.CurrentState = State::Failed;
        if (entry.FailureCount < 4)
        {
            ++entry.FailureCount;
        }

        uint64_t retry_delay = 60;
        for (uint8_t retry = 1; retry < entry.FailureCount; ++retry)
        {
            retry_delay *= 2;
        }
        entry.NextRetryFrame = m_frame + retry_delay;
    }

    void ThumbnailService::OnAssetStale(void* context, const uuids::uuid& uuid)
    {
        if (auto* service = static_cast<ThumbnailService*>(context))
        {
            service->QueueInvalidation(uuid);
        }
    }

    void ThumbnailService::OnAssetRemoved(void* context, const uuids::uuid& uuid, ZEngine::Managers::AssetType)
    {
        if (auto* service = static_cast<ThumbnailService*>(context))
        {
            service->QueueInvalidation(uuid);
        }
    }
} // namespace Tetragrama::Thumbnails
