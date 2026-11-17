#include <ZEngine/Managers/CookedCpuAssetPool.h>
#include <ZEngine/Managers/CookedCpuAssetSet.h>
#include <rapidhash.h>
#include <algorithm>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace ZEngine::Managers
{
    namespace VFS   = Core::VFS;
    namespace Codec = Importers::AssetCodec;
    namespace Image = Importers::ImageCodec;

    namespace
    {
        struct MeshPayload
        {
            void*                         Storage   = nullptr;
            Core::Memory::ArenaAllocator  Arena     = {};
            Importers::AssetMesh          Mesh      = {};
            Importers::AssetNodeHierarchy Hierarchy = {};
        };

        struct MaterialPayload
        {
            Codec::MaterialAssetReader          Reader   = {};
            const Codec::MaterialAssetDocument* Document = nullptr;
        };

        struct TexturePayload
        {
            Image::DecodedImage Image = {};
        };

        struct EnvironmentPayload
        {
            CookedObjectSnapshot            Object = {};
            Codec::EnvironmentMapFileHeader Header = {};
        };

        bool IsCancelled(const std::atomic<bool>* cancelled)
        {
            return cancelled && cancelled->load(std::memory_order_acquire);
        }

        template <typename T>
        T* AllocatePayload(Core::Memory::IAllocationRoute& route)
        {
            static_assert(alignof(T) <= alignof(std::max_align_t));
            void* storage = route.Allocate(sizeof(T));
            return storage ? new (storage) T{} : nullptr;
        }

        void DestroyPayload(Core::Memory::IAllocationRoute& route, Cook::CookAssetKind kind, void* payload)
        {
            if (!payload)
            {
                return;
            }
            switch (kind)
            {
                case Cook::CookAssetKind::Mesh:
                {
                    auto* mesh    = static_cast<MeshPayload*>(payload);
                    void* storage = mesh->Storage;
                    mesh->~MeshPayload();
                    if (storage)
                    {
                        route.Free(storage);
                    }
                    break;
                }
                case Cook::CookAssetKind::Material:
                    static_cast<MaterialPayload*>(payload)->~MaterialPayload();
                    break;
                case Cook::CookAssetKind::Texture:
                    static_cast<TexturePayload*>(payload)->~TexturePayload();
                    break;
                case Cook::CookAssetKind::EnvironmentMap:
                    static_cast<EnvironmentPayload*>(payload)->~EnvironmentPayload();
                    break;
                default:
                    ZENGINE_VALIDATE_ASSERT(false, "Invalid cooked CPU payload kind")
                    break;
            }
            route.Free(payload);
        }
    } // namespace

    struct CookedCpuAssetPool::Entry
    {
        CookedCpuAssetVersion Version    = {};
        void*                 Payload    = nullptr;
        uint32_t              References = 0;
        uint32_t              Next       = Invalid;
    };

    CookedCpuAssetPool::~CookedCpuAssetPool()
    {
        const bool closed = Shutdown();
        ZENGINE_VALIDATE_ASSERT(closed, "Cooked CPU asset pool destroyed while versions are retained.")
        (void) closed;
    }

    bool CookedCpuAssetPool::Initialize(Core::Memory::IAllocationRoute& route, uint32_t capacity)
    {
        if (m_route || !route.WorkingMemoryBudget() || capacity > UINT32_MAX / 2 || size_t(capacity) > std::numeric_limits<size_t>::max() / sizeof(Entry))
        {
            return false;
        }
        if (!capacity)
        {
            m_route = &route;
            return true;
        }
        uint32_t bucket_count = 1;
        while (bucket_count < capacity * 2)
        {
            if (bucket_count > UINT32_MAX / 2)
            {
                return false;
            }
            bucket_count *= 2;
        }
        if (size_t(bucket_count) > std::numeric_limits<size_t>::max() / sizeof(uint32_t))
        {
            return false;
        }
        static_assert(alignof(Entry) <= alignof(std::max_align_t));
        auto* entries = static_cast<Entry*>(route.Allocate(size_t(capacity) * sizeof(Entry)));
        if (!entries)
        {
            return false;
        }
        auto* buckets = static_cast<uint32_t*>(route.Allocate(size_t(bucket_count) * sizeof(uint32_t)));
        if (!buckets)
        {
            route.Free(entries);
            return false;
        }
        for (uint32_t i = 0; i < capacity; ++i)
        {
            new (entries + i) Entry{};
            entries[i].Next = i + 1 < capacity ? i + 1 : Invalid;
        }
        std::fill(buckets, buckets + bucket_count, Invalid);
        m_route        = &route;
        m_entries      = entries;
        m_buckets      = buckets;
        m_capacity     = capacity;
        m_bucket_count = bucket_count;
        m_free         = 0;
        return true;
    }

    bool CookedCpuAssetPool::Shutdown() noexcept
    {
        if (m_live)
        {
            return false;
        }
        if (m_entries)
        {
            for (uint32_t i = 0; i < m_capacity; ++i)
            {
                m_entries[i].~Entry();
            }
            m_route->Free(m_entries);
            m_route->Free(m_buckets);
        }
        m_route        = nullptr;
        m_entries      = nullptr;
        m_buckets      = nullptr;
        m_capacity     = 0;
        m_bucket_count = 0;
        m_free         = Invalid;
        return true;
    }

    uint32_t CookedCpuAssetPool::Bucket(const CookedCpuAssetVersion& version) const noexcept
    {
        const auto uuid = version.Identity.UUID.as_bytes();
        const auto seed = rapidhash_withSeed(uuid.data(), uuid.size(), static_cast<uint64_t>(version.Identity.Kind));
        return static_cast<uint32_t>(rapidhash_withSeed(version.ObjectDigest.Bytes.data(), version.ObjectDigest.Bytes.size(), seed)) & (m_bucket_count - 1);
    }

    uint32_t CookedCpuAssetPool::Find(const CookedCpuAssetVersion& version) const noexcept
    {
        if (!m_bucket_count)
        {
            return Invalid;
        }
        for (uint32_t index = m_buckets[Bucket(version)]; index != Invalid; index = m_entries[index].Next)
        {
            const auto& existing = m_entries[index].Version;
            if (existing.Identity == version.Identity && existing.ObjectDigest == version.ObjectDigest && existing.Format == version.Format && existing.ObjectBytes == version.ObjectBytes)
            {
                return index;
            }
        }
        return Invalid;
    }

    bool CookedCpuAssetPool::Retain(uint32_t index) noexcept
    {
        if (index >= m_capacity || !m_entries[index].References || m_entries[index].References == UINT32_MAX)
        {
            return false;
        }
        ++m_entries[index].References;
        return true;
    }

    bool CookedCpuAssetPool::Publish(const CookedCpuAssetVersion& version, void* payload, uint32_t* output) noexcept
    {
        if (!payload || !output || !m_bucket_count || m_free == Invalid || Find(version) != Invalid)
        {
            return false;
        }
        const uint32_t index  = m_free;
        auto&          entry  = m_entries[index];
        m_free                = entry.Next;
        const uint32_t bucket = Bucket(version);
        entry.Version         = version;
        entry.Payload         = payload;
        entry.References      = 1;
        entry.Next            = m_buckets[bucket];
        m_buckets[bucket]     = index;
        ++m_live;
        *output = index;
        return true;
    }

    void CookedCpuAssetPool::Release(uint32_t index) noexcept
    {
        if (index >= m_capacity || !m_entries[index].References)
        {
            ZENGINE_VALIDATE_ASSERT(false, "Invalid cooked CPU version release")
            return;
        }
        auto& entry = m_entries[index];
        if (--entry.References)
        {
            return;
        }
        auto* link = &m_buckets[Bucket(entry.Version)];
        while (*link != Invalid && *link != index)
        {
            link = &m_entries[*link].Next;
        }
        if (*link != index)
        {
            ZENGINE_VALIDATE_ASSERT(false, "Cooked CPU version is missing from its hash bucket")
            return;
        }
        *link = entry.Next;
        DestroyPayload(*m_route, entry.Version.Identity.Kind, entry.Payload);
        entry      = {};
        entry.Next = m_free;
        m_free     = index;
        --m_live;
    }

    struct CookedCpuAssetSet::Record
    {
        CookedCpuAssetVersion Version     = {};
        void*                 Payload     = nullptr;
        uint32_t              SharedIndex = CookedCpuAssetPool::Invalid;
    };

    struct CookedCpuAssetSet::BuildContext
    {
        CookedCpuAssetSet*          Set       = nullptr;
        CookedPackageSnapshot*      Package   = nullptr;
        const CookedCpuAssetLimits* Limits    = nullptr;
        const std::atomic<bool>*    Cancelled = nullptr;
    };

    CookedCpuAssetSet::~CookedCpuAssetSet()
    {
        Reset();
    }

    CookedCpuAssetSet::CookedCpuAssetSet(CookedCpuAssetSet&& other) noexcept : m_route(std::exchange(other.m_route, nullptr)), m_pool(std::exchange(other.m_pool, nullptr)), m_records(std::exchange(other.m_records, nullptr)), m_count(std::exchange(other.m_count, 0)), m_capacity(std::exchange(other.m_capacity, 0)), m_manifest_digest(std::exchange(other.m_manifest_digest, {})), m_scene_uuid(std::exchange(other.m_scene_uuid, {})), m_valid(std::exchange(other.m_valid, false)) {}

    CookedCpuAssetSet& CookedCpuAssetSet::operator=(CookedCpuAssetSet&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_route           = std::exchange(other.m_route, nullptr);
            m_pool            = std::exchange(other.m_pool, nullptr);
            m_records         = std::exchange(other.m_records, nullptr);
            m_count           = std::exchange(other.m_count, 0);
            m_capacity        = std::exchange(other.m_capacity, 0);
            m_manifest_digest = std::exchange(other.m_manifest_digest, {});
            m_scene_uuid      = std::exchange(other.m_scene_uuid, {});
            m_valid           = std::exchange(other.m_valid, false);
        }
        return *this;
    }

    void CookedCpuAssetSet::Reset() noexcept
    {
        for (size_t i = m_count; i > 0; --i)
        {
            auto& record = m_records[i - 1];
            if (record.Payload)
            {
                if (record.SharedIndex != CookedCpuAssetPool::Invalid)
                {
                    m_pool->Release(record.SharedIndex);
                }
                else
                {
                    DestroyPayload(*m_route, record.Version.Identity.Kind, record.Payload);
                }
            }
            record.~Record();
        }
        if (m_records)
        {
            m_route->Free(m_records);
        }
        m_route           = nullptr;
        m_pool            = nullptr;
        m_records         = nullptr;
        m_count           = 0;
        m_capacity        = 0;
        m_manifest_digest = {};
        m_scene_uuid      = {};
        m_valid           = false;
    }

    VFS::VFSResult<void> CookedCpuAssetSet::BuildAsset(void* context, const Cook::CookManifestAsset& asset)
    {
        auto& build = *static_cast<BuildContext*>(context);
        auto& set   = *build.Set;
        auto& route = *set.m_route;
        if (IsCancelled(build.Cancelled))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Cancelled);
        }
        if (!asset.Format || set.m_count == set.m_capacity)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
        auto& record   = *new (set.m_records + set.m_count++) Record{};
        record.Version = {asset.Identity, asset.Result.Object.Digest, asset.Result.InputKey, asset.Format, asset.Result.Object.Size};
        if (set.m_pool)
        {
            const uint32_t index = set.m_pool->Find(record.Version);
            if (index != CookedCpuAssetPool::Invalid)
            {
                void* payload = set.m_pool->m_entries[index].Payload;
                bool  matches = false;
                switch (asset.Identity.Kind)
                {
                    case Cook::CookAssetKind::Mesh:
                        matches = CookedAssetReader::MatchesMeshDependencies(*build.Package, asset.Identity, static_cast<const MeshPayload*>(payload)->Mesh);
                        break;
                    case Cook::CookAssetKind::Material:
                    {
                        const auto* material = static_cast<const MaterialPayload*>(payload)->Document;
                        matches              = material && CookedAssetReader::MatchesMaterialDependencies(*build.Package, asset.Identity, *material);
                        break;
                    }
                    case Cook::CookAssetKind::Texture:
                    case Cook::CookAssetKind::EnvironmentMap:
                        matches = build.Package->Dependencies(asset.Identity).empty();
                        break;
                    default:
                        break;
                }
                if (!matches)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                }
                if (!set.m_pool->Retain(index))
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                }
                record.Payload     = payload;
                record.SharedIndex = index;
                return IsCancelled(build.Cancelled) ? VFS::VFSResult<void>::Fail(VFS::VFSError::Cancelled) : VFS::VFSResult<void>::Ok();
            }
            if (!set.m_pool->HasCapacity())
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
            }
        }
        CookedObjectSnapshot object   = {};
        const auto           captured = build.Package->CaptureAsset(asset.Identity, route, build.Limits->MaxObjectBytes, &object, build.Cancelled);
        if (captured.Failed())
        {
            return captured;
        }
        VFS::VFSResult<void> result = VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
        switch (asset.Identity.Kind)
        {
            case Cook::CookAssetKind::Mesh:
            {
                size_t bytes = 0;
                {
                    Core::Memory::WorkingMemoryCharge fixed = {};
                    if (!fixed.TryAcquire(route.WorkingMemoryBudget(), Codec::MeshAssetReadFixedWorkingBytes() + sizeof(fixed)))
                    {
                        return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                    }
                    result = Codec::MeasureMeshAssetReadWorkspace(object.Source(), &bytes, asset.Identity.UUID, build.Limits->Mesh);
                }
                if (result.Failed())
                {
                    return result;
                }
                if (!bytes || bytes > build.Limits->MaxMeshWorkspaceBytes)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                }
                if (bytes > std::numeric_limits<size_t>::max() - (DEFAULT_ALIGNMENT - 1))
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                }
                auto* mesh = AllocatePayload<MeshPayload>(route);
                if (!mesh)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::OutOfMemory);
                }
                record.Payload       = mesh;
                size_t storage_bytes = bytes + DEFAULT_ALIGNMENT - 1;
                mesh->Storage        = route.Allocate(storage_bytes);
                if (!mesh->Storage)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::OutOfMemory);
                }
                // A route guarantees max_align_t, which can be less than the
                // alignment assumed by the mesh workspace measurement.
                void* aligned = mesh->Storage;
                if (!std::align(DEFAULT_ALIGNMENT, bytes, aligned, storage_bytes) || !mesh->Arena.InitializeBorrowed(aligned, bytes, "CookedCpuMesh"))
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                }
                result = CookedAssetReader::ReadMesh(*build.Package, asset.Identity, object, &mesh->Arena, *route.WorkingMemoryBudget(), &mesh->Mesh, &mesh->Hierarchy, build.Limits->Mesh);
                break;
            }
            case Cook::CookAssetKind::Material:
            {
                auto* material = AllocatePayload<MaterialPayload>(route);
                if (!material)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::OutOfMemory);
                }
                record.Payload = material;
                result         = CookedAssetReader::ReadMaterial(*build.Package, asset.Identity, object, material->Reader, &material->Document);
                break;
            }
            case Cook::CookAssetKind::Texture:
            {
                auto* texture = AllocatePayload<TexturePayload>(route);
                if (!texture)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::OutOfMemory);
                }
                record.Payload = texture;
                result         = CookedAssetReader::ReadTexture(*build.Package, asset.Identity, object, route, build.Limits->Texture, &texture->Image);
                break;
            }
            case Cook::CookAssetKind::EnvironmentMap:
            {
                auto* environment = AllocatePayload<EnvironmentPayload>(route);
                if (!environment)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::OutOfMemory);
                }
                record.Payload                          = environment;
                Core::Memory::WorkingMemoryCharge fixed = {};
                if (!fixed.TryAcquire(route.WorkingMemoryBudget(), ZKilo(4) + sizeof(fixed)))
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                }
                uint8_t stream[ZKilo(4)] = {};
                result                   = CookedAssetReader::ReadEnvironment(*build.Package, asset.Identity, object, stream, &environment->Header, build.Limits->Environment);
                if (result.Succeeded())
                {
                    environment->Object = std::move(object);
                }
                break;
            }
            default:
                break;
        }
        if (result.Failed())
        {
            return result;
        }
        if (IsCancelled(build.Cancelled))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Cancelled);
        }
        if (set.m_pool && !set.m_pool->Publish(record.Version, record.Payload, &record.SharedIndex))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookedCpuAssetSet::Capture(CookedPackageSnapshot& package, Core::Memory::IAllocationRoute& route, const CookedCpuAssetLimits& limits, CookedCpuAssetSet* output, const std::atomic<bool>* cancelled)
    {
        return CaptureImpl(package, route, nullptr, limits, output, cancelled);
    }

    VFS::VFSResult<void> CookedCpuAssetSet::CaptureShared(CookedPackageSnapshot& package, Core::Memory::IAllocationRoute& route, CookedCpuAssetPool& pool, const CookedCpuAssetLimits& limits, CookedCpuAssetSet* output, const std::atomic<bool>* cancelled)
    {
        if (pool.m_route != &route)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        return CaptureImpl(package, route, &pool, limits, output, cancelled);
    }

    VFS::VFSResult<void> CookedCpuAssetSet::CaptureImpl(CookedPackageSnapshot& package, Core::Memory::IAllocationRoute& route, CookedCpuAssetPool* pool, const CookedCpuAssetLimits& limits, CookedCpuAssetSet* output, const std::atomic<bool>* cancelled)
    {
        if (!package.IsValid() || !output || output->IsValid() || !limits.MaxObjectBytes || !route.WorkingMemoryBudget())
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        Core::Memory::WorkingMemoryCharge fixed = {};
        if (!fixed.TryAcquire(route.WorkingMemoryBudget(), sizeof(BuildContext) + sizeof(CookedObjectSnapshot) + sizeof(fixed)))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        struct CountContext
        {
            size_t Count   = 0;
            size_t Maximum = 0;
        } count            = {0, limits.MaxAssets};
        const auto counted = package.VisitSelectedAssets(
            [](void* context, const Cook::CookManifestAsset&) {
                auto& state = *static_cast<CountContext*>(context);
                if (state.Count == state.Maximum)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                }
                ++state.Count;
                return VFS::VFSResult<void>::Ok();
            },
            &count,
            cancelled);
        if (counted.Failed())
        {
            return counted;
        }
        if (count.Count > std::numeric_limits<size_t>::max() / sizeof(Record))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        CookedCpuAssetSet candidate = {};
        candidate.m_route           = &route;
        candidate.m_pool            = pool;
        candidate.m_capacity        = count.Count;
        if (count.Count)
        {
            candidate.m_records = static_cast<Record*>(route.Allocate(count.Count * sizeof(Record)));
            if (!candidate.m_records)
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::OutOfMemory);
            }
        }
        BuildContext context = {&candidate, &package, &limits, cancelled};
        const auto   built   = package.VisitSelectedAssets(BuildAsset, &context, cancelled);
        if (built.Failed())
        {
            return built;
        }
        candidate.m_manifest_digest = package.ManifestDigest();
        candidate.m_scene_uuid      = package.Scene()->UUID;
        candidate.m_valid           = true;
        *output                     = std::move(candidate);
        return VFS::VFSResult<void>::Ok();
    }

    const CookedCpuAssetSet::Record* CookedCpuAssetSet::Find(const Cook::CookAssetIdentity& identity) const noexcept
    {
        if (!m_valid || !m_count)
        {
            return nullptr;
        }
        const auto* found = std::lower_bound(m_records, m_records + m_count, identity, [](const Record& record, const Cook::CookAssetIdentity& value) { return record.Version.Identity < value; });
        return found != m_records + m_count && found->Version.Identity == identity ? found : nullptr;
    }

    const CookedCpuAssetVersion* CookedCpuAssetSet::VersionAt(size_t index) const noexcept
    {
        return m_valid && index < m_count ? &m_records[index].Version : nullptr;
    }

    const CookedCpuAssetVersion* CookedCpuAssetSet::FindVersion(const Cook::CookAssetIdentity& identity) const noexcept
    {
        const auto* record = Find(identity);
        return record ? &record->Version : nullptr;
    }

    const Importers::AssetMesh* CookedCpuAssetSet::FindMesh(const Cook::CookAssetIdentity& identity) const noexcept
    {
        const auto* record = identity.Kind == Cook::CookAssetKind::Mesh ? Find(identity) : nullptr;
        return record ? &static_cast<const MeshPayload*>(record->Payload)->Mesh : nullptr;
    }

    const Importers::AssetNodeHierarchy* CookedCpuAssetSet::FindHierarchy(const Cook::CookAssetIdentity& identity) const noexcept
    {
        const auto* record = identity.Kind == Cook::CookAssetKind::Mesh ? Find(identity) : nullptr;
        return record ? &static_cast<const MeshPayload*>(record->Payload)->Hierarchy : nullptr;
    }

    const Codec::MaterialAssetDocument* CookedCpuAssetSet::FindMaterial(const Cook::CookAssetIdentity& identity) const noexcept
    {
        const auto* record = identity.Kind == Cook::CookAssetKind::Material ? Find(identity) : nullptr;
        return record ? static_cast<const MaterialPayload*>(record->Payload)->Document : nullptr;
    }

    const Image::DecodedImage* CookedCpuAssetSet::FindTexture(const Cook::CookAssetIdentity& identity) const noexcept
    {
        const auto* record = identity.Kind == Cook::CookAssetKind::Texture ? Find(identity) : nullptr;
        return record ? &static_cast<const TexturePayload*>(record->Payload)->Image : nullptr;
    }

    const Codec::EnvironmentMapFileHeader* CookedCpuAssetSet::FindEnvironment(const Cook::CookAssetIdentity& identity) const noexcept
    {
        const auto* record = identity.Kind == Cook::CookAssetKind::EnvironmentMap ? Find(identity) : nullptr;
        return record ? &static_cast<const EnvironmentPayload*>(record->Payload)->Header : nullptr;
    }

    std::span<const uint8_t> CookedCpuAssetSet::EnvironmentObject(const Cook::CookAssetIdentity& identity) const noexcept
    {
        const auto* record = identity.Kind == Cook::CookAssetKind::EnvironmentMap ? Find(identity) : nullptr;
        return record ? static_cast<const EnvironmentPayload*>(record->Payload)->Object.Bytes() : std::span<const uint8_t>{};
    }
} // namespace ZEngine::Managers
