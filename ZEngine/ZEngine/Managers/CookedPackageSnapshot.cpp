#include <ZEngine/Cook/CookPublication.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Managers/CookedPackageSnapshot.h>
#include <algorithm>
#include <limits>
#include <utility>

namespace ZEngine::Managers
{
    namespace VFS = Core::VFS;

    namespace
    {
        class TemporaryBlock
        {
        public:
            ~TemporaryBlock()
            {
                if (m_data)
                {
                    m_allocator->Free(m_data);
                }
            }
            VFS::VFSResult<void> Allocate(Cook::ICookAllocator& allocator, size_t bytes, size_t alignment)
            {
                if (!bytes || m_data)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
                }
                const auto allocation = allocator.Allocate(bytes, alignment, Cook::CookMemoryCategory::StagingManifest);
                if (allocation.Failed())
                {
                    return VFS::VFSResult<void>::Fail(allocation.Error());
                }
                if (!allocation.Value())
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::OutOfMemory);
                }
                m_allocator = &allocator;
                m_data      = allocation.Value();
                return VFS::VFSResult<void>::Ok();
            }
            void* Data() const noexcept
            {
                return m_data;
            }
            void* Release() noexcept
            {
                return std::exchange(m_data, nullptr);
            }

        private:
            Cook::ICookAllocator* m_allocator = nullptr;
            void*                 m_data      = nullptr;
        };

        bool IsCancelled(const std::atomic<bool>* cancelled)
        {
            return cancelled && cancelled->load(std::memory_order_acquire);
        }

        VFS::VFSResult<void> ReadManifest(VFS::IVFSFile& file, const Cook::CookLimits& limits, Cook::ICookAllocator& allocator, TemporaryBlock& bytes, size_t* size, const std::atomic<bool>* cancelled)
        {
            const auto before = file.Stat();
            if (before.Failed())
            {
                return VFS::VFSResult<void>::Fail(before.Error());
            }
            if (before.Value().IsDirectory || !before.Value().SizeBytes)
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            }
            if (before.Value().SizeBytes > limits.MaxManifestBytes || before.Value().SizeBytes > std::numeric_limits<size_t>::max())
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
            }
            const size_t length    = static_cast<size_t>(before.Value().SizeBytes);
            const auto   allocated = bytes.Allocate(allocator, length, alignof(uint8_t));
            if (allocated.Failed())
            {
                return allocated;
            }
            auto*            buffer     = static_cast<uint8_t*>(bytes.Data());
            constexpr size_t read_slice = ZKilo(64);
            for (size_t offset = 0; offset < length;)
            {
                if (IsCancelled(cancelled))
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Cancelled);
                }
                const size_t count = std::min(read_slice, length - offset);
                const auto   read  = file.Read({buffer + offset, count}, offset);
                if (read.Failed() || !read.Value() || read.Value() > count)
                {
                    return VFS::VFSResult<void>::Fail(read.Failed() ? read.Error() : VFS::VFSError::Corrupted);
                }
                offset += read.Value();
            }
            uint8_t    trailing = 0;
            const auto eof      = file.Read({&trailing, 1}, length);
            if (eof.Failed() || eof.Value())
            {
                return VFS::VFSResult<void>::Fail(eof.Failed() ? eof.Error() : VFS::VFSError::Corrupted);
            }
            const auto after = file.Stat();
            if (after.Failed())
            {
                return VFS::VFSResult<void>::Fail(after.Error());
            }
            if (after.Value().IsDirectory || after.Value().SizeBytes != length)
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            }
            *size = length;
            return VFS::VFSResult<void>::Ok();
        }

        VFS::VFSResult<void> ComputeSchemaKey(const ECS::SceneComponentSchemaRegistry& schemas, Cook::ICookAllocator& allocator, Cook::Digest256* result)
        {
            const auto words = Cook::CookSchemaKeyWorkspaceWords(schemas);
            if (words.Failed())
            {
                return VFS::VFSResult<void>::Fail(words.Error());
            }
            if (words.Value() > std::numeric_limits<size_t>::max() / sizeof(uint32_t))
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
            }
            TemporaryBlock workspace = {};
            if (words.Value())
            {
                const auto allocated = workspace.Allocate(allocator, words.Value() * sizeof(uint32_t), alignof(uint32_t));
                if (allocated.Failed())
                {
                    return allocated;
                }
            }
            return Cook::ComputeCookSchemaKey(allocator, schemas, {static_cast<uint32_t*>(workspace.Data()), words.Value()}, result);
        }

        VFS::VFSResult<void> BuildSelection(const Cook::CookManifest& manifest, const uuids::uuid& scene, Cook::ICookAllocator& allocator, TemporaryBlock& selected, const std::atomic<bool>* cancelled)
        {
            const auto* scene_record = manifest.FindScene(scene);
            if (!scene_record)
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::NotFound);
            }
            const auto assets = manifest.Assets();
            if (assets.empty())
            {
                return VFS::VFSResult<void>::Ok();
            }
            if (assets.size() > std::numeric_limits<size_t>::max() / sizeof(uint32_t))
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
            }
            auto status = selected.Allocate(allocator, assets.size(), alignof(uint8_t));
            if (status.Failed())
            {
                return status;
            }
            TemporaryBlock queue = {};
            status               = queue.Allocate(allocator, assets.size() * sizeof(uint32_t), alignof(uint32_t));
            if (status.Failed())
            {
                return status;
            }
            auto* flags   = static_cast<uint8_t*>(selected.Data());
            auto* pending = static_cast<uint32_t*>(queue.Data());
            (void) Helpers::secure_memset(flags, 0, assets.size(), assets.size());
            size_t     head = 0;
            size_t     tail = 0;
            const auto add  = [&](const Cook::CookAssetIdentity& identity) {
                const auto* asset = manifest.FindAsset(identity);
                if (!asset)
                {
                    return false;
                }
                const auto index = static_cast<size_t>(asset - assets.data());
                if (!flags[index])
                {
                    flags[index]    = 1;
                    pending[tail++] = static_cast<uint32_t>(index);
                }
                return true;
            };
            const auto scene_index = static_cast<uint32_t>(scene_record - manifest.Scenes().data());
            for (const auto& root : manifest.Roots(scene_index))
            {
                if (!add(root))
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                }
            }
            while (head < tail)
            {
                if (IsCancelled(cancelled))
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Cancelled);
                }
                for (const auto& dependency : manifest.Dependencies(pending[head++]))
                {
                    if (!add(dependency.Identity))
                    {
                        return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                    }
                }
            }
            return VFS::VFSResult<void>::Ok();
        }
    } // namespace

    CookedPackageSnapshot::~CookedPackageSnapshot()
    {
        Reset();
    }

    CookedPackageSnapshot::CookedPackageSnapshot(CookedPackageSnapshot&& other) noexcept : m_root(std::move(other.m_root)), m_manifest(std::move(other.m_manifest)), m_allocator(std::exchange(other.m_allocator, nullptr)), m_schemas(std::exchange(other.m_schemas, nullptr)), m_selected(std::exchange(other.m_selected, nullptr)), m_scene(std::exchange(other.m_scene, {})), m_digest(std::exchange(other.m_digest, {})) {}

    CookedPackageSnapshot& CookedPackageSnapshot::operator=(CookedPackageSnapshot&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_root      = std::move(other.m_root);
            m_manifest  = std::move(other.m_manifest);
            m_allocator = std::exchange(other.m_allocator, nullptr);
            m_schemas   = std::exchange(other.m_schemas, nullptr);
            m_selected  = std::exchange(other.m_selected, nullptr);
            m_scene     = std::exchange(other.m_scene, {});
            m_digest    = std::exchange(other.m_digest, {});
        }
        return *this;
    }

    void CookedPackageSnapshot::Reset() noexcept
    {
        if (m_selected)
        {
            m_allocator->Free(m_selected);
        }
        m_selected = nullptr;
        m_manifest.Reset();
        m_root.Reset();
        m_allocator = nullptr;
        m_schemas   = nullptr;
        m_scene     = {};
        m_digest    = {};
    }

    VFS::VFSResult<void> CookedPackageSnapshot::Capture(VFS::VFSReadRootLease&& root, const uuids::uuid& scene, const ECS::SceneComponentSchemaRegistry& schemas, const Cook::CookLimits& limits, Cook::ICookAllocator& allocator, Cook::CookDiagnostics* diagnostics, CookedPackageSnapshot* output, const std::atomic<bool>* cancelled)
    {
        if (!output || output->IsValid() || !root.IsValid() || scene.is_nil() || !schemas.Frozen())
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        const auto validated = Cook::ValidateCookLimits(limits);
        if (validated.Failed())
        {
            return validated;
        }
        if (IsCancelled(cancelled))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Cancelled);
        }
        Cook::Digest256 schema_key = {};
        const auto      schema     = ComputeSchemaKey(schemas, allocator, &schema_key);
        if (schema.Failed())
        {
            return schema;
        }
        const auto path = VFS::VFSRelativePath::Parse(Cook::CookManifestName);
        if (path.Failed())
        {
            return VFS::VFSResult<void>::Fail(path.Error());
        }
        const auto opened = root.Open(path.Value());
        if (opened.Failed() || !opened.Value())
        {
            return VFS::VFSResult<void>::Fail(opened.Failed() ? opened.Error() : VFS::VFSError::IOError);
        }
        TemporaryBlock bytes  = {};
        size_t         size   = 0;
        const auto     read   = ReadManifest(*opened.Value(), limits, allocator, bytes, &size, cancelled);
        const auto     closed = root.CloseChecked(opened.Value());
        if (read.Failed() || closed.Failed())
        {
            return VFS::VFSResult<void>::Fail(read.Failed() ? read.Error() : closed.Error());
        }
        if (IsCancelled(cancelled))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Cancelled);
        }
        Cook::CookMemoryCharge fixed   = {};
        const auto             charged = allocator.TryCharge(Core::Hash::SHA256::FixedWorkingBytes() + sizeof(fixed), Cook::CookMemoryCategory::StagingManifest, &fixed);
        if (charged.Failed())
        {
            return charged;
        }
        Core::Hash::SHA256 hash   = {};
        Cook::Digest256    digest = {};
        const auto         hashed = hash.Update({static_cast<const uint8_t*>(bytes.Data()), size});
        if (hashed.Failed())
        {
            return hashed;
        }
        const auto finalized = hash.Finalize(&digest);
        if (finalized.Failed())
        {
            return finalized;
        }
        CookedPackageSnapshot candidate = {};
        const auto            parsed    = candidate.m_manifest.Read({static_cast<const uint8_t*>(bytes.Data()), size}, Cook::CookTarget::DesktopVulkanLE64, limits, allocator, diagnostics);
        if (parsed.Failed() || parsed.Value() != Cook::CookManifestReadStatus::Valid)
        {
            return VFS::VFSResult<void>::Fail(parsed.Failed() ? parsed.Error() : VFS::VFSError::Corrupted);
        }
        if (candidate.m_manifest.Recipe().SchemaKey != schema_key)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
        }
        TemporaryBlock selected  = {};
        const auto     selection = BuildSelection(candidate.m_manifest, scene, allocator, selected, cancelled);
        if (selection.Failed())
        {
            return selection;
        }
        if (IsCancelled(cancelled))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Cancelled);
        }
        candidate.m_allocator = &allocator;
        candidate.m_schemas   = &schemas;
        candidate.m_selected  = static_cast<uint8_t*>(selected.Release());
        candidate.m_scene     = scene;
        candidate.m_digest    = digest;
        candidate.m_root      = std::move(root);
        *output               = std::move(candidate);
        return VFS::VFSResult<void>::Ok();
    }

    const Cook::CookManifestScene* CookedPackageSnapshot::Scene() const noexcept
    {
        return IsValid() ? m_manifest.FindScene(m_scene) : nullptr;
    }

    const Cook::CookManifestAsset* CookedPackageSnapshot::FindSelectedAsset(const Cook::CookAssetIdentity& identity) const noexcept
    {
        const auto* asset = IsValid() ? m_manifest.FindAsset(identity) : nullptr;
        if (!asset)
        {
            return nullptr;
        }
        const auto index = static_cast<size_t>(asset - m_manifest.Assets().data());
        return m_selected && m_selected[index] ? asset : nullptr;
    }

    VFS::VFSResult<void> CookedPackageSnapshot::VisitSelectedAssets(AssetVisitor visitor, void* context, const std::atomic<bool>* cancelled) const
    {
        if (!IsValid() || !visitor)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        const auto assets = m_manifest.Assets();
        for (size_t i = 0; i < assets.size(); ++i)
        {
            if (IsCancelled(cancelled))
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Cancelled);
            }
            if (m_selected && m_selected[i])
            {
                const auto result = visitor(context, assets[i]);
                if (result.Failed())
                {
                    return result;
                }
            }
        }
        return IsCancelled(cancelled) ? VFS::VFSResult<void>::Fail(VFS::VFSError::Cancelled) : VFS::VFSResult<void>::Ok();
    }

    Core::Containers::ArrayView<const Cook::CookDependency> CookedPackageSnapshot::Dependencies(const Cook::CookAssetIdentity& identity) const noexcept
    {
        const auto* asset = FindSelectedAsset(identity);
        return asset ? m_manifest.Dependencies(static_cast<uint32_t>(asset - m_manifest.Assets().data())) : Core::Containers::ArrayView<const Cook::CookDependency>{};
    }

    VFS::VFSResult<void> CookedPackageSnapshot::ValidateScene(const ECS::SceneDocument& document, const ECS::SceneComponentSchemaRegistry& schemas, ECS::SceneDiagnostics* diagnostics, const std::atomic<bool>* cancelled) const
    {
        const auto* scene = Scene();
        if (!scene || !document.Settings() || document.Settings()->UUID != m_scene || &schemas != m_schemas || !schemas.Frozen())
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
        if (IsCancelled(cancelled))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Cancelled);
        }
        const auto     roots = m_manifest.Roots(static_cast<uint32_t>(scene - m_manifest.Scenes().data()));
        TemporaryBlock seen  = {};
        if (!roots.empty())
        {
            const auto allocated = seen.Allocate(*m_allocator, roots.size(), alignof(uint8_t));
            if (allocated.Failed())
            {
                return allocated;
            }
            (void) Helpers::secure_memset(seen.Data(), 0, roots.size(), roots.size());
        }
        struct VisitorState
        {
            Core::Containers::ArrayView<const Cook::CookAssetIdentity> Roots     = {};
            uint8_t*                                                   Seen      = nullptr;
            const std::atomic<bool>*                                   Cancelled = nullptr;
            VFS::VFSError                                              Error     = VFS::VFSError::OK;
        } state          = {roots, static_cast<uint8_t*>(seen.Data()), cancelled};
        const auto visit = [](void* context, const ECS::SceneReferenceOccurrence& occurrence) {
            auto& state = *static_cast<VisitorState*>(context);
            if (IsCancelled(state.Cancelled))
            {
                state.Error = VFS::VFSError::Cancelled;
                return false;
            }
            const auto& reference = occurrence.Reference;
            if (reference.Kind == ECS::SceneReferenceKind::Entity)
            {
                return true;
            }
            if (reference.Kind != ECS::SceneReferenceKind::Asset || !reference.AssetKindKey)
            {
                state.Error = VFS::VFSError::Corrupted;
                return false;
            }
            const auto kind = Cook::ParseCookAssetKind(reference.AssetKindKey);
            if (kind == Cook::CookAssetKind::Unknown)
            {
                state.Error = VFS::VFSError::Corrupted;
                return false;
            }
            if (reference.UUID.is_nil())
            {
                return true;
            }
            if (state.Roots.empty())
            {
                state.Error = VFS::VFSError::Corrupted;
                return false;
            }
            const Cook::CookAssetIdentity identity = {kind, reference.UUID};
            const auto                    root     = std::lower_bound(state.Roots.begin(), state.Roots.end(), identity);
            if (root == state.Roots.end() || *root != identity)
            {
                state.Error = VFS::VFSError::Corrupted;
                return false;
            }
            state.Seen[static_cast<size_t>(root - state.Roots.begin())] = 1;
            return true;
        };
        if (!document.EnumerateReferences(schemas, visit, &state, diagnostics))
        {
            return VFS::VFSResult<void>::Fail(state.Error == VFS::VFSError::OK ? VFS::VFSError::Corrupted : state.Error);
        }
        for (size_t i = 0; i < roots.size(); ++i)
        {
            if (!state.Seen[i])
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            }
        }
        return IsCancelled(cancelled) ? VFS::VFSResult<void>::Fail(VFS::VFSError::Cancelled) : VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookedPackageSnapshot::CaptureScene(Core::Memory::IAllocationRoute& route, size_t max_bytes, CookedObjectSnapshot* output, const std::atomic<bool>* cancelled)
    {
        const auto* scene = Scene();
        return scene ? CookedObjectSnapshot::Capture(Cook::CookObjectReader(m_root), scene->Result.Object, Cook::CookSceneObjectExtension, route, max_bytes, output, cancelled) : VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
    }

    VFS::VFSResult<void> CookedPackageSnapshot::CaptureAsset(const Cook::CookAssetIdentity& identity, Core::Memory::IAllocationRoute& route, size_t max_bytes, CookedObjectSnapshot* output, const std::atomic<bool>* cancelled)
    {
        const auto* asset = FindSelectedAsset(identity);
        return asset && asset->Format ? CookedObjectSnapshot::Capture(Cook::CookObjectReader(m_root), asset->Result.Object, asset->Format->Extension, route, max_bytes, output, cancelled) : VFS::VFSResult<void>::Fail(VFS::VFSError::NotFound);
    }
} // namespace ZEngine::Managers
