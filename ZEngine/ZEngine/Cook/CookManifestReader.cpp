#include <ZEngine/Cook/CookManifestInternal.h>
#include <ZEngine/Cook/CookStorageLayout.h>
#include <ZEngine/Core/Serialization/JSONReader.h>
#include <ZEngine/Helpers/UUID.h>
#include <algorithm>
#include <new>
#include <type_traits>
#include <utility>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        using StringView = Core::Containers::StringView;

        struct ManifestCounts
        {
            uint32_t Scenes                                  = 0;
            uint32_t Assets                                  = 0;
            uint32_t Roots                                   = 0;
            uint32_t Dependencies                            = 0;
            uint32_t TextBytes                               = 0;
            bool     operator==(const ManifestCounts&) const = default;
        };

        struct ManifestTables
        {
            CookManifestScene* Scenes       = nullptr;
            CookManifestAsset* Assets       = nullptr;
            CookAssetIdentity* Roots        = nullptr;
            CookDependency*    Dependencies = nullptr;
            char*              Text         = nullptr;
            ManifestCounts     Capacity     = {};
        };

        struct ManifestDependencyScratch
        {
            Core::Containers::FixedArray<char, 128> Slot             = {};
            CookAssetIdentity                       PreviousIdentity = {};
            uint32_t                                PreviousBytes    = 0;
        };

        struct ManifestBuildScratch
        {
            ManifestCounts            Counts           = {};
            CookRecipeKeys            Recipe           = {};
            Detail::CookStorageLayout Layout           = {};
            size_t                    SceneOffset      = 0;
            size_t                    AssetOffset      = 0;
            size_t                    RootOffset       = 0;
            size_t                    DependencyOffset = 0;
            size_t                    TextOffset       = 0;
            CookManifest              Candidate        = {};
        };

        class ManifestParser
        {
        public:
            ManifestParser(Core::Containers::ArrayView<const uint8_t> bytes, CookTarget target, const CookLimits& limits, ICookAllocator& allocator, ManifestTables tables = {}) : Reader(bytes), Limits(limits), Tables(tables), Allocator(allocator)
            {
                Recipe.Target = target;
            }

            // Syntax probing, measurement and construction are sequential. Keep
            // one decode buffer and restart only the bounded parser state.
            Core::Containers::ArrayView<char> StringBuffer() noexcept
            {
                return Buffer;
            }

            void Restart(Core::Containers::ArrayView<const uint8_t> bytes, CookTarget target, ManifestTables tables)
            {
                Reader.Reset(bytes);
                Tables  = tables;
                Counts  = {};
                Recipe  = {.Target = target};
                Failure = VFS::VFSError::OK;
            }

            bool Read()
            {
                static constexpr cstring names[] = {"manifest_version", "recipe_revision", "target", "schema_key", "settings", "settings_key", "scenes", "assets"};
                if (!Record(
                        names,
                        [&](uint32_t field) {
                            uint32_t   value = 0;
                            StringView text  = {};
                            switch (field)
                            {
                                case 0:
                                    return UInt32(&value) && value == CookManifestVersion;
                                case 1:
                                    return UInt32(&Recipe.Revision) && Recipe.Revision > 0;
                                case 2:
                                    return Reader.String(Buffer, &text) && text == CookTargetKey(Recipe.Target);
                                case 3:
                                    return Digest(&Recipe.SchemaKey);
                                case 4:
                                    if (!Reader.BeginObject())
                                        return false;
                                    return !Reader.Member(Buffer, &text) && Reader.Error() == VFS::VFSError::OK;
                                case 5:
                                    return Digest(&Recipe.SettingsKey);
                                case 6:
                                    return Scenes();
                                case 7:
                                    return Assets();
                                default:
                                    return false;
                            }
                        }) ||
                    !Reader.Finish() || !Counts.Scenes)
                    return false;
                CookMemoryCharge fixed = {};
                if (!Reserve(sizeof(Digest256) + sizeof(fixed), &fixed))
                    return false;
                Digest256 settings = {};
                return ComputeCookSettingsKey(Recipe.Target, &settings).Succeeded() && settings == Recipe.SettingsKey;
            }

            VFS::VFSError Error() const noexcept
            {
                return Failure != VFS::VFSError::OK ? Failure : Reader.Error() != VFS::VFSError::OK ? Reader.Error() : VFS::VFSError::Corrupted;
            }

            Core::Serialization::JSONReader Reader;
            const CookLimits&               Limits;
            ManifestTables                  Tables = {};
            ManifestCounts                  Counts = {};
            CookRecipeKeys                  Recipe = {};

        private:
            template <size_t N, typename ReadField>
            bool Record(const cstring (&names)[N], ReadField&& read)
            {
                static_assert(N < 32);
                if (!Reader.BeginObject())
                    return false;
                uint32_t   fields = 0;
                StringView key    = {};
                while (Reader.Member(Buffer, &key))
                {
                    uint32_t index = 0;
                    while (index < N && key != names[index])
                        ++index;
                    if (index == N || (fields & (uint32_t{1} << index)))
                        return false;
                    fields |= uint32_t{1} << index;
                    if (!read(index))
                        return false;
                }
                return Reader.Error() == VFS::VFSError::OK && fields == (uint32_t{1} << N) - 1;
            }

            bool Reject(VFS::VFSError error = VFS::VFSError::SizeLimitExceeded)
            {
                if (Failure == VFS::VFSError::OK)
                    Failure = error;
                return false;
            }

            bool Reserve(size_t bytes, CookMemoryCharge* fixed)
            {
                const auto status = Allocator.TryCharge(bytes, CookMemoryCategory::StagingManifest, fixed);
                return status.Succeeded() || Reject(status.Error());
            }

            bool UInt32(uint32_t* output)
            {
                uint64_t value = 0;
                if (!Reader.UInt64(&value) || value > UINT32_MAX)
                    return false;
                *output = static_cast<uint32_t>(value);
                return true;
            }

            bool UUID(uuids::uuid* output)
            {
                StringView text = {};
                return Reader.String(Buffer, &text) && Helpers::ParseCanonicalUUID(text.data(), text.size(), output);
            }

            bool Digest(Digest256* output)
            {
                CookMemoryCharge fixed = {};
                if (!Reserve(sizeof(StringView) + sizeof(Digest256) + sizeof(VFS::VFSResult<Digest256>) + sizeof(fixed), &fixed))
                    return false;
                StringView text = {};
                if (!Reader.String(Buffer, &text))
                    return false;
                const auto parsed = Digest256::ParseHex(text);
                if (parsed.Failed())
                    return false;
                *output = parsed.Value();
                return true;
            }

            bool Path(VFS::VFSRelativePath* output, bool logical = false)
            {
                CookMemoryCharge fixed = {};
                if (!Reserve(sizeof(StringView) + sizeof(VFS::VFSResult<uint16_t>) + sizeof(VFS::VFSResult<void>) + sizeof(fixed), &fixed))
                    return false;
                StringView text = {};
                if (!Reader.String(Buffer, &text))
                    return false;
                // The row is private until the entire manifest validates. A
                // logical-path rejection discards it, never a published owner.
                return VFS::VFSRelativePath::ParseInto(text, output).Succeeded() && (!logical || IsCookLogicalPath(*output));
            }

            bool Kind(CookAssetKind* output)
            {
                StringView text = {};
                if (!Reader.String(Buffer, &text))
                    return false;
                *output = ParseCookAssetKind(text);
                return *output != CookAssetKind::Unknown;
            }

            bool Object(CookObjectDescriptor* output)
            {
                static constexpr cstring names[] = {"path", "digest", "size"};
                return Record(names, [&](uint32_t field) { return field == 0 ? Path(&output->Path) : field == 1 ? Digest(&output->Digest) : Reader.UInt64(&output->Size) && output->Size > 0 && output->Size <= ZGiga(16); });
            }

            bool Identity(CookAssetIdentity* output)
            {
                static constexpr cstring names[] = {"kind", "uuid"};
                return Record(names, [&](uint32_t field) { return field == 0 ? Kind(&output->Kind) : UUID(&output->UUID); });
            }

            bool Roots()
            {
                return Reader.BeginArray() && (Reader.Element() ? RootRows() : Reader.Error() == VFS::VFSError::OK);
            }

            bool RootRows()
            {
                CookMemoryCharge fixed = {};
                if (!Reserve(2 * sizeof(CookAssetIdentity) + sizeof(bool) + sizeof(fixed), &fixed))
                    return false;
                CookAssetIdentity previous     = {};
                bool              has_previous = false;
                do
                {
                    if (uint64_t(Counts.Roots) + Counts.Dependencies == Limits.MaxReferenceOccurrences || (Tables.Roots && Counts.Roots == Tables.Capacity.Roots))
                        return Reject();
                    CookAssetIdentity identity = {};
                    if (!Identity(&identity) || (has_previous && !(previous < identity)))
                        return false;
                    if (Tables.Roots)
                        new (Tables.Roots + Counts.Roots) CookAssetIdentity(identity);
                    ++Counts.Roots;
                    previous     = identity;
                    has_previous = true;
                } while (Reader.Element());
                return Reader.Error() == VFS::VFSError::OK;
            }

            bool Dependencies()
            {
                return Reader.BeginArray() && (Reader.Element() ? DependencyRows() : Reader.Error() == VFS::VFSError::OK);
            }

            bool DependencyRows()
            {
                CookMemoryCharge fixed = {};
                if (!Reserve(sizeof(ManifestDependencyScratch) + sizeof(CookDependency) + sizeof(bool) + 2 * sizeof(StringView) + sizeof(fixed), &fixed))
                    return false;
                ManifestDependencyScratch scratch = {};
                do
                {
                    if (Counts.Dependencies == Limits.MaxEdges || uint64_t(Counts.Roots) + Counts.Dependencies == Limits.MaxReferenceOccurrences || (Tables.Dependencies && Counts.Dependencies == Tables.Capacity.Dependencies))
                        return Reject();
                    CookDependency           dependency = {};
                    bool                     same_slot  = false;
                    auto&                    slot       = scratch.Slot;
                    static constexpr cstring names[]    = {"slot", "kind", "uuid"};
                    if (!Record(names, [&](uint32_t field) {
                            if (field == 1)
                                return Kind(&dependency.Identity.Kind);
                            if (field == 2)
                                return UUID(&dependency.Identity.UUID);
                            StringView text = {};
                            if (!Reader.String(Buffer, &text) || !text.size() || text.size() >= slot.size())
                                return false;
                            // Compare before reusing the one key buffer. Equal
                            // slots defer identity ordering until the complete
                            // record is read, regardless of its field order.
                            const StringView previous(slot.data(), scratch.PreviousBytes);
                            same_slot = previous == text;
                            if (scratch.PreviousBytes && !same_slot && !CookDependencySlotBefore(previous, text))
                                return false;
                            Helpers::secure_memcpy(slot.data(), slot.size(), text.data(), text.size());
                            slot[text.size()] = '\0';
                            dependency.Slot   = {slot.data(), text.size()};
                            return true;
                        }))
                        return false;
                    if (same_slot && !(scratch.PreviousIdentity < dependency.Identity))
                        return false;
                    const auto text_bytes = static_cast<uint32_t>(dependency.Slot.size() + 1);
                    if (text_bytes > UINT32_MAX - Counts.TextBytes || (Tables.Text && text_bytes > Tables.Capacity.TextBytes - Counts.TextBytes))
                        return Reject();
                    if (Tables.Dependencies)
                    {
                        auto* text = Tables.Text + Counts.TextBytes;
                        Helpers::secure_memcpy(text, text_bytes, slot.data(), text_bytes);
                        dependency.Slot = {text, text_bytes - 1};
                        new (Tables.Dependencies + Counts.Dependencies) CookDependency(dependency);
                    }
                    Counts.TextBytes += text_bytes;
                    ++Counts.Dependencies;
                    scratch.PreviousBytes    = text_bytes - 1;
                    scratch.PreviousIdentity = dependency.Identity;
                } while (Reader.Element());
                return Reader.Error() == VFS::VFSError::OK;
            }

            bool Scenes()
            {
                return Reader.BeginArray() && (Reader.Element() ? SceneRows() : Reader.Error() == VFS::VFSError::OK);
            }

            bool SceneRows()
            {
                CookMemoryCharge fixed = {};
                if (!Reserve(sizeof(CookManifestScene) + sizeof(uuids::uuid) + sizeof(fixed), &fixed))
                    return false;
                uuids::uuid previous = {};
                do
                {
                    if (Counts.Scenes == Limits.MaxScenes || (Tables.Scenes && Counts.Scenes == Tables.Capacity.Scenes))
                        return Reject();
                    CookManifestScene        scene   = {.FirstRoot = Counts.Roots};
                    static constexpr cstring names[] = {"uuid", "source", "source_digest", "input_key", "object", "roots"};
                    if (!Record(
                            names,
                            [&](uint32_t field) {
                                switch (field)
                                {
                                    case 0:
                                        return UUID(&scene.UUID);
                                    case 1:
                                        return Path(&scene.Source, true);
                                    case 2:
                                        return Digest(&scene.Result.SourceDigest);
                                    case 3:
                                        return Digest(&scene.Result.InputKey);
                                    case 4:
                                        return Object(&scene.Result.Object);
                                    case 5:
                                        return Roots();
                                    default:
                                        return false;
                                }
                            }) ||
                        (Counts.Scenes && !(previous < scene.UUID)) || !IsCookObjectDescriptorValid(scene.Result.Object, CookSceneObjectExtension))
                        return false;
                    if (scene.Result.Object.Size > Limits.SceneLimits.MaxFileBytes)
                        return Reject();
                    scene.RootCount = Counts.Roots - scene.FirstRoot;
                    if (Tables.Scenes)
                        new (Tables.Scenes + Counts.Scenes) CookManifestScene(scene);
                    ++Counts.Scenes;
                    previous = scene.UUID;
                } while (Reader.Element());
                return Reader.Error() == VFS::VFSError::OK;
            }

            bool Assets()
            {
                return Reader.BeginArray() && (Reader.Element() ? AssetRows() : Reader.Error() == VFS::VFSError::OK);
            }

            bool AssetRows()
            {
                CookMemoryCharge fixed = {};
                if (!Reserve(sizeof(CookManifestAsset) + sizeof(CookAssetIdentity) + sizeof(uint32_t) + sizeof(fixed), &fixed))
                    return false;
                CookAssetIdentity previous = {};
                do
                {
                    if (Counts.Assets == Limits.MaxAssets || (Tables.Assets && Counts.Assets == Tables.Capacity.Assets))
                        return Reject();
                    CookManifestAsset        asset    = {.FirstDependency = Counts.Dependencies};
                    uint32_t                 revision = 0;
                    static constexpr cstring names[]  = {"kind", "uuid", "format", "adapter_revision", "input_key", "object", "dependencies"};
                    if (!Record(
                            names,
                            [&](uint32_t field) {
                                switch (field)
                                {
                                    case 0:
                                        return Kind(&asset.Identity.Kind);
                                    case 1:
                                        return UUID(&asset.Identity.UUID);
                                    case 2:
                                    {
                                        StringView text = {};
                                        if (!Reader.String(Buffer, &text))
                                            return false;
                                        asset.Format = FindCookAssetFormat(text);
                                        return asset.Format != nullptr;
                                    }
                                    case 3:
                                        return UInt32(&revision);
                                    case 4:
                                        return Digest(&asset.Result.InputKey);
                                    case 5:
                                        return Object(&asset.Result.Object);
                                    case 6:
                                        return Dependencies();
                                    default:
                                        return false;
                                }
                            }) ||
                        (Counts.Assets && !(previous < asset.Identity)) || asset.Format->Kind != asset.Identity.Kind || asset.Format->Revision != revision || !IsCookObjectDescriptorValid(*asset.Format, asset.Result.Object.Path, asset.Result.Object.Digest, asset.Result.Object.Size))
                        return false;
                    if (asset.Result.Object.Size > Limits.MaxEncodedAssetBytes || (asset.Identity.Kind == CookAssetKind::Material && asset.Result.Object.Size > Limits.MaxMaterialBytes))
                        return Reject();
                    asset.DependencyCount = Counts.Dependencies - asset.FirstDependency;
                    if (Tables.Assets)
                        new (Tables.Assets + Counts.Assets) CookManifestAsset(asset);
                    ++Counts.Assets;
                    previous = asset.Identity;
                } while (Reader.Element());
                return Reader.Error() == VFS::VFSError::OK;
            }

            ICookAllocator& Allocator;
            VFS::VFSError   Failure      = VFS::VFSError::OK;
            char            Buffer[4097] = {};
        };

        VFS::VFSResult<void> ValidateLedger(const CookManifest& manifest, const CookLimits& limits, ICookAllocator& allocator)
        {
            CookMemoryCharge fixed   = {};
            const auto       charged = allocator.TryCharge(sizeof(CookDependencyGraph) + sizeof(Detail::CookSortState<uint32_t>) + sizeof(fixed), CookMemoryCategory::StagingManifest, &fixed);
            if (charged.Failed())
                return charged;
            CookDependencyGraph graph  = {};
            auto                status = graph.Initialize(allocator, limits);
            if (status.Failed())
                return status;
            for (uint32_t i = 0; i < manifest.Scenes().size(); ++i)
            {
                const auto& scene = manifest.Scenes()[i];
                const auto  added = graph.AddScene(scene.UUID, scene.Source);
                if (added.Failed())
                    return VFS::VFSResult<void>::Fail(added.Error());
                for (const auto& root : manifest.Roots(i))
                {
                    status = graph.AddRoot(i, root, {}, "manifest", "roots");
                    if (status.Failed())
                        return status;
                }
            }
            status = graph.BeginDiscovery();
            if (status.Failed())
                return status;
            for (uint32_t i = 0; i < graph.NodeCount(); ++i)
            {
                const auto* asset = manifest.FindAsset(graph.Node(i)->Identity);
                if (!asset)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                const auto index        = static_cast<uint32_t>(asset - manifest.Assets().data());
                const auto dependencies = manifest.Dependencies(index);
                for (const auto& dependency : dependencies)
                    if (!IsCookDependencySlotValid(asset->Identity.Kind, dependency.Identity.Kind, dependency.Slot))
                        return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                status = graph.SetInspection(i, asset->Format->Revision, CookAssetInspectionView{.Format = asset->Format->Key, .Dependencies = dependencies});
                if (status.Failed())
                    return status;
            }
            if (graph.NodeCount() != manifest.Assets().size())
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            status = graph.Finalize(nullptr);
            if (status.Failed())
                return status;
            // Reuse one temporary uint32 table for global descriptor conflicts.
            const auto    count     = static_cast<uint32_t>(manifest.Assets().size() + manifest.Scenes().size());
            CookWorkspace workspace = {};
            status                  = workspace.Initialize(allocator, size_t(count) * sizeof(uint32_t), CookMemoryCategory::StagingManifest);
            if (status.Failed())
                return status;
            auto* order = static_cast<uint32_t*>(workspace.Arena()->AllocateNoZero(size_t(count) * sizeof(uint32_t), alignof(uint32_t)));
            if (!order)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
            const auto                      object = [&](uint32_t index) -> const CookObjectDescriptor& { return index < manifest.Assets().size() ? manifest.Assets()[index].Result.Object : manifest.Scenes()[index - manifest.Assets().size()].Result.Object; };
            Detail::CookSortState<uint32_t> sort   = {};
            return Detail::ValidateManifestObjectSizes(count, {order, count}, sort, object);
        }
    } // namespace

    CookManifest::~CookManifest()
    {
        Reset();
    }

    CookManifest::CookManifest(CookManifest&& other) noexcept
    {
        *this = std::move(other);
    }

    CookManifest& CookManifest::operator=(CookManifest&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_allocator    = std::exchange(other.m_allocator, nullptr);
            m_block        = std::exchange(other.m_block, nullptr);
            m_scenes       = std::exchange(other.m_scenes, nullptr);
            m_assets       = std::exchange(other.m_assets, nullptr);
            m_roots        = std::exchange(other.m_roots, nullptr);
            m_dependencies = std::exchange(other.m_dependencies, nullptr);
            m_scene_count  = std::exchange(other.m_scene_count, 0);
            m_asset_count  = std::exchange(other.m_asset_count, 0);
            m_recipe       = std::exchange(other.m_recipe, {});
        }
        return *this;
    }

    void CookManifest::Reset() noexcept
    {
        static_assert(std::is_trivially_destructible_v<CookManifestScene> && std::is_trivially_destructible_v<CookManifestAsset> && std::is_trivially_destructible_v<CookAssetIdentity> && std::is_trivially_destructible_v<CookDependency>);
        if (m_block)
            m_allocator->Free(m_block);
        m_allocator    = nullptr;
        m_block        = nullptr;
        m_scenes       = nullptr;
        m_assets       = nullptr;
        m_roots        = nullptr;
        m_dependencies = nullptr;
        m_scene_count = m_asset_count = 0;
        m_recipe                      = {};
    }

    Core::Containers::ArrayView<const CookAssetIdentity> CookManifest::Roots(uint32_t scene) const noexcept
    {
        return scene < m_scene_count && m_scenes[scene].RootCount ? Core::Containers::ArrayView<const CookAssetIdentity>(m_roots + m_scenes[scene].FirstRoot, m_scenes[scene].RootCount) : Core::Containers::ArrayView<const CookAssetIdentity>{};
    }

    Core::Containers::ArrayView<const CookDependency> CookManifest::Dependencies(uint32_t asset) const noexcept
    {
        return asset < m_asset_count && m_assets[asset].DependencyCount ? Core::Containers::ArrayView<const CookDependency>(m_dependencies + m_assets[asset].FirstDependency, m_assets[asset].DependencyCount) : Core::Containers::ArrayView<const CookDependency>{};
    }

    const CookManifestScene* CookManifest::FindScene(const uuids::uuid& uuid) const noexcept
    {
        if (!m_scene_count)
            return nullptr;
        const auto* found = std::lower_bound(m_scenes, m_scenes + m_scene_count, uuid, [](const CookManifestScene& scene, const uuids::uuid& key) { return scene.UUID < key; });
        return found != m_scenes + m_scene_count && found->UUID == uuid ? found : nullptr;
    }

    const CookManifestAsset* CookManifest::FindAsset(const CookAssetIdentity& identity) const noexcept
    {
        if (!m_asset_count)
            return nullptr;
        const auto* found = std::lower_bound(m_assets, m_assets + m_asset_count, identity, [](const CookManifestAsset& asset, const CookAssetIdentity& key) { return asset.Identity < key; });
        return found != m_assets + m_asset_count && found->Identity == identity ? found : nullptr;
    }

    VFS::VFSResult<CookManifestReadStatus> CookManifest::Read(Core::Containers::ArrayView<const uint8_t> bytes, CookTarget target, const CookLimits& limits, ICookAllocator& allocator, CookDiagnostics* diagnostics)
    {
        const auto fail    = [&](CookDiagnosticCode code, VFS::VFSError error, cstring message) { return VFS::VFSResult<CookManifestReadStatus>::Fail(Detail::ManifestDiagnostic(diagnostics, code, error, message)); };
        const auto rebuild = [&] {
            (void) Detail::ManifestDiagnostic(diagnostics, CookDiagnosticCode::CacheMiss, VFS::VFSError::Corrupted, "Recognizable manifest v1 is invalid or uses an older recipe; rebuild without reuse or cleanup authority.", CookSeverity::Warning);
            Reset();
            return VFS::VFSResult<CookManifestReadStatus>::Ok(CookManifestReadStatus::Rebuild);
        };
        const auto resource_failure = [&](VFS::VFSError error) { return fail(Detail::ManifestResourceCode(error), error, "Manifest reading exceeded a configured limit or its allocator refused storage."); };
        if ((bytes.size() && !bytes.data()) || ValidateCookLimits(limits).Failed())
            return fail(CookDiagnosticCode::InvalidRequest, VFS::VFSError::InvalidPath, "Manifest reading requires valid bounded input and cook limits.");
        if (!CookTargetKey(target))
            return fail(CookDiagnosticCode::UnsupportedTarget, VFS::VFSError::Unsupported, "Manifest reader does not support this target.");
        if (bytes.size() > limits.MaxManifestBytes)
            return resource_failure(VFS::VFSError::SizeLimitExceeded);
        CookMemoryCharge fixed   = {};
        const auto       charged = allocator.TryCharge(sizeof(ManifestParser) + sizeof(fixed), CookMemoryCategory::StagingManifest, &fixed);
        if (charged.Failed())
            return resource_failure(charged.Error());
        ManifestParser parser(bytes, target, limits, allocator);
        const auto     parse_failure = [&] {
            const auto error = parser.Error();
            return error == VFS::VFSError::SizeLimitExceeded || error == VFS::VFSError::OutOfMemory || error == VFS::VFSError::Unsupported ? resource_failure(error) : rebuild();
        };
        const auto syntax = Detail::CheckCookManifestSyntax(bytes, target, allocator, diagnostics, parser.StringBuffer());
        if (syntax.Failed())
            return VFS::VFSResult<CookManifestReadStatus>::Fail(syntax.Error());
        if (syntax.Value().RecipeRevision && syntax.Value().RecipeRevision < CookRecipeRevision)
            return rebuild();
        if (!parser.Read())
            return parse_failure();
        CookMemoryCharge preparation = {};
        const auto       prepared    = allocator.TryCharge(sizeof(ManifestBuildScratch) + sizeof(preparation), CookMemoryCategory::StagingManifest, &preparation);
        if (prepared.Failed())
            return resource_failure(prepared.Error());
        ManifestBuildScratch build    = {};
        build.Counts                  = parser.Counts;
        build.Recipe                  = parser.Recipe;
        const auto& counts            = build.Counts;
        const auto& recipe            = build.Recipe;
        auto&       layout            = build.Layout;
        auto&       scene_offset      = build.SceneOffset;
        auto&       asset_offset      = build.AssetOffset;
        auto&       root_offset       = build.RootOffset;
        auto&       dependency_offset = build.DependencyOffset;
        auto&       text_offset       = build.TextOffset;
        if (!layout.Add<CookManifestScene>(counts.Scenes, &scene_offset) || !layout.Add<CookManifestAsset>(counts.Assets, &asset_offset) || !layout.Add<CookAssetIdentity>(counts.Roots, &root_offset) || !layout.Add<CookDependency>(counts.Dependencies, &dependency_offset) || !layout.Add<char>(counts.TextBytes, &text_offset))
            return resource_failure(VFS::VFSError::SizeLimitExceeded);
        auto&      candidate  = build.Candidate;
        const auto allocation = allocator.Allocate(layout.Size(), DEFAULT_ALIGNMENT, CookMemoryCategory::StagingManifest);
        if (allocation.Failed())
            return resource_failure(allocation.Error());
        if (!allocation.Value())
            return resource_failure(VFS::VFSError::OutOfMemory);
        if (reinterpret_cast<uintptr_t>(allocation.Value()) % DEFAULT_ALIGNMENT != 0)
        {
            allocator.Free(allocation.Value());
            return fail(CookDiagnosticCode::InvalidRequest, VFS::VFSError::InvalidPath, "Manifest allocator returned misaligned storage.");
        }
        candidate.m_allocator    = &allocator;
        candidate.m_block        = allocation.Value();
        auto* block              = static_cast<uint8_t*>(candidate.m_block);
        candidate.m_scenes       = reinterpret_cast<CookManifestScene*>(block + scene_offset);
        candidate.m_assets       = reinterpret_cast<CookManifestAsset*>(block + asset_offset);
        candidate.m_roots        = reinterpret_cast<CookAssetIdentity*>(block + root_offset);
        candidate.m_dependencies = reinterpret_cast<CookDependency*>(block + dependency_offset);
        candidate.m_scene_count  = counts.Scenes;
        candidate.m_asset_count  = counts.Assets;
        parser.Restart(bytes, target, {candidate.m_scenes, candidate.m_assets, candidate.m_roots, candidate.m_dependencies, reinterpret_cast<char*>(block + text_offset), counts});
        if (!parser.Read())
            return parse_failure();
        if (parser.Counts != counts || parser.Recipe.Revision != recipe.Revision || parser.Recipe.SchemaKey != recipe.SchemaKey || parser.Recipe.SettingsKey != recipe.SettingsKey)
            return fail(CookDiagnosticCode::InvalidManifest, VFS::VFSError::Corrupted, "Manifest changed during typed parsing.");
        candidate.m_recipe = parser.Recipe;
        const auto valid   = ValidateLedger(candidate, limits, allocator);
        if (valid.Failed())
            return valid.Error() == VFS::VFSError::SizeLimitExceeded || valid.Error() == VFS::VFSError::OutOfMemory || valid.Error() == VFS::VFSError::Unsupported ? resource_failure(valid.Error()) : rebuild();
        *this = std::move(candidate);
        return VFS::VFSResult<CookManifestReadStatus>::Ok(CookManifestReadStatus::Valid);
    }
} // namespace ZEngine::Cook
