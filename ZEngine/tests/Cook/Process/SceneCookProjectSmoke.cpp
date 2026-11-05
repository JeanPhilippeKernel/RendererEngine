#include "SceneCookProjectSmoke.h"
#include <ZEngine/Cook/CookBudgetAllocator.h>
#include <ZEngine/Cook/CookCodecAllocationRoute.h>
#include <ZEngine/Cook/CookFrozenInputSource.h>
#include <ZEngine/Cook/CookManifest.h>
#include <ZEngine/Cook/CookObjectReader.h>
#include <ZEngine/Cook/CookPageAllocator.h>
#include <ZEngine/Cook/SceneCooker.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/ECS/BinarySceneCodec.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif

namespace ZEngine::Cook::Tests
{
    namespace VFS = Core::VFS;

    namespace
    {
        struct ProjectInputs
        {
            Core::Containers::FixedArray<VFS::VFSPath, 256> Scenes     = {};
            size_t                                          SceneCount = 0;
            VFS::VFSPath                                    SceneRoot  = {};
            VFS::VFSPath                                    AssetRoot  = {};
        };

        VFS::VFSPath Path(cstring text)
        {
            const auto parsed = VFS::VFSPath::Parse(text);
            return parsed.Succeeded() ? parsed.Value() : VFS::VFSPath{};
        }

        VFS::VFSResult<VFS::VFSRelativePath> ProjectPath(const std::string& text)
        {
            Core::Containers::StringView relative(text.data(), text.size());
            constexpr char               token[] = "$(workingSpace)/";
            if (relative.size() >= sizeof(token) - 1 && Helpers::secure_memcmp(relative.data(), relative.size(), token, sizeof(token), sizeof(token) - 1) == 0)
                relative.set(relative.data() + sizeof(token) - 1, relative.size() - (sizeof(token) - 1));
            else if (relative.size() >= 2 && relative[0] == '.' && relative[1] == '/')
                relative.set(relative.data() + 2, relative.size() - 2);
            return VFS::VFSRelativePath::Parse(relative);
        }

        bool ReadProject(const std::filesystem::path& file, ProjectInputs& output)
        {
            // Project configuration is harness setup, not a cooker input/API.
            // Bound it before parsing; all cooking uses the shared core APIs.
            std::ifstream stream(file, std::ios::binary | std::ios::ate);
            if (!stream)
                return false;
            const auto                                    length = stream.tellg();
            Core::Containers::FixedArray<char, ZKilo(64)> bytes  = {};
            if (length <= 0 || static_cast<uint64_t>(length) > bytes.size())
                return false;
            stream.seekg(0);
            if (!stream.read(bytes.data(), static_cast<std::streamsize>(length)))
                return false;
            const auto config = nlohmann::json::parse(bytes.data(), bytes.data() + static_cast<size_t>(length));
            if (config.at("workingSpace") != "." || !config.at("sceneList").is_array() || config.at("sceneList").empty() || config.at("sceneList").size() > output.Scenes.size())
                return false;
            const auto scene_root = ProjectPath(config.at("sceneDir").get<std::string>());
            if (scene_root.Failed())
                return false;
            const auto context_scene_root = VFS::VFSPath::Root().Append(scene_root.Value().CStr());
            if (context_scene_root.Failed())
                return false;
            output.SceneRoot        = context_scene_root.Value();
            // One common asset tree, not six overlapping authorities or an
            // assumed editor mount. This covers the SampleProject layout and
            // the process fixture. Other layouts require explicit integration.
            const auto& directories = config.at("assetDirs");
            if (!directories.is_object() || directories.empty())
                return false;
            for (const auto& directory : directories.items())
            {
                const auto relative = ProjectPath(directory.value().get<std::string>());
                if (relative.Failed())
                    return false;
                const auto path = VFS::VFSPath::Root().Append(relative.Value().CStr());
                if (path.Failed())
                    return false;
                const auto first                        = path.Value().ComponentAt(0);
                char       component[VFS::VFS_MAX_PATH] = {};
                if (Helpers::secure_memcpy(component, sizeof(component), first.Data, first.Length) != Helpers::MEMORY_OP_SUCCESS)
                    return false;
                const auto root = VFS::VFSPath::Root().Append(component);
                if (root.Failed() || (output.AssetRoot.IsValid() && output.AssetRoot != root.Value()))
                    return false;
                output.AssetRoot = root.Value();
            }
            for (const auto& scene : config.at("sceneList"))
            {
                auto       name = scene.at("name").get<std::string>();
                const auto leaf = VFS::VFSRelativePath::Parse(name.data(), name.size());
                if (leaf.Failed() || leaf.Value().ComponentCount() != 1)
                    return false;
                // Editor names normally omit the extension. Explicit filenames
                // let synthetic tests exercise other supported saved formats.
                if (name.find('.') == std::string::npos)
                    name += ".zescene";
                const auto path = output.SceneRoot.Append(name.c_str());
                if (path.Failed())
                    return false;
                output.Scenes[output.SceneCount++] = path.Value();
            }
            return true;
        }

        bool Contains(const std::filesystem::path& parent, const std::filesystem::path& child)
        {
            const auto mismatch = std::mismatch(parent.begin(), parent.end(), child.begin(), child.end());
            return mismatch.first == parent.end();
        }

        int SetupFailure(cstring operation, VFS::VFSError error)
        {
            std::cerr << "Project smoke setup: " << operation << " (VFS " << static_cast<uint32_t>(error) << ")\n";
            return 3;
        }

        void PrintDiagnostics(const CookDiagnostics& diagnostics, const ECS::SceneDiagnostics& scenes)
        {
            for (uint32_t i = 0; i < diagnostics.Count; ++i)
            {
                const auto& entry = diagnostics.Entries[i];
                std::cerr << entry.Message << " (code " << static_cast<uint32_t>(entry.Code) << ", VFS " << static_cast<uint32_t>(entry.Underlying) << ", " << entry.Source.CStr() << ")\n";
            }
            for (uint32_t i = 0; i < scenes.Count; ++i)
                std::cerr << scenes.Entries[i].Message << '\n';
        }

        VFS::VFSResult<void> ReadFile(VFS::VFSReadRootLease& root, const VFS::VFSRelativePath& path, size_t maximum, ICookAllocator& allocator, CookWorkspace& workspace, Core::Containers::ArrayView<const uint8_t>& bytes)
        {
            const auto opened = root.Open(path);
            if (opened.Failed())
                return VFS::VFSResult<void>::Fail(opened.Error());
            const auto perform = [&]() {
                const auto size = opened.Value()->Size();
                if (size.Failed())
                    return VFS::VFSResult<void>::Fail(size.Error());
                if (!size.Value() || size.Value() > maximum)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                const auto initialized = workspace.Initialize(allocator, static_cast<size_t>(size.Value()), CookMemoryCategory::Codecs);
                if (initialized.Failed())
                    return initialized;
                auto*      data = workspace.Arena()->m_memory;
                const auto read = opened.Value()->ReadAll({data, static_cast<size_t>(size.Value())});
                if (read.Failed() || read.Value() != size.Value())
                    return VFS::VFSResult<void>::Fail(read.Failed() ? read.Error() : VFS::VFSError::Corrupted);
                bytes = {data, static_cast<size_t>(size.Value())};
                return VFS::VFSResult<void>::Ok();
            };
            const auto result = perform();
            const auto closed = root.CloseChecked(opened.Value());
            return result.Failed() ? result : closed;
        }

        bool VerifyPackage(VFS::IVFSContext& files, const SceneCookResult& result, const CookLimits& limits, ICookAllocator& allocator, ECS::SceneComponentSchemaRegistry& schemas, CookDiagnostics& diagnostics, ECS::SceneDiagnostics& scene_diagnostics, uint32_t expected_entities)
        {
            VFS::VFSReadRootLease root = {};
            if (files.AcquireReadRoot(result.ManifestPath.Parent(), &root).Failed())
                return false;
            CookWorkspace                              manifest_bytes = {};
            Core::Containers::ArrayView<const uint8_t> bytes          = {};
            if (ReadFile(root, VFS::VFSRelativePath::Parse("manifest.json").Value(), static_cast<size_t>(limits.MaxManifestBytes), allocator, manifest_bytes, bytes).Failed())
                return false;
            Core::Hash::SHA256 hash   = {};
            Digest256          digest = {};
            if (hash.Update(bytes).Failed() || hash.Finalize(&digest).Failed() || digest != result.ManifestDigest)
                return false;
            CookManifest manifest = {};
            const auto   read     = manifest.Read(bytes, CookTarget::DesktopVulkanLE64, limits, allocator, &diagnostics);
            if (read.Failed() || read.Value() != CookManifestReadStatus::Valid || manifest.Scenes().size() != result.SceneCount || manifest.Assets().size() != result.AssetCount)
                return false;
            manifest_bytes.Release();
            Core::Containers::FixedArray<uint8_t, ZKilo(64)> stream = {};
            const auto                                       reader = CookObjectReader(root);
            for (const auto& asset : manifest.Assets())
            {
                const auto verified = VerifyCookObject(reader, asset.Result.Object, asset.Format->Extension, stream, allocator);
                if (verified.Failed() || !verified.Value())
                    return false;
            }
            for (const auto& scene : manifest.Scenes())
            {
                const auto verified = VerifyCookObject(reader, scene.Result.Object, CookSceneObjectExtension, stream, allocator);
                if (verified.Failed() || !verified.Value())
                    return false;
                CookWorkspace storage = {};
                if (ReadFile(root, scene.Result.Object.Path, limits.SceneLimits.MaxFileBytes, allocator, storage, bytes).Failed())
                    return false;
                CookCodecAllocationRoute route(allocator);
                ECS::SceneDocument       document = {};
                if (!ECS::BinarySceneCodec::Decode(bytes, schemas, &document, &scene_diagnostics, limits.SceneLimits, &route) || !document.Settings() || document.Settings()->UUID != scene.UUID || (expected_entities != UINT32_MAX && document.EntityCount() != expected_entities) || route.Error() != VFS::VFSError::OK)
                    return false;
            }
            return !diagnostics.HasErrors();
        }

        bool ScratchEmpty(VFS::IVFSContext& files)
        {
            VFS::VFSReadRootLease scratch = {};
            if (files.AcquireReadRoot(Path("/scratch"), &scratch).Failed())
                return false;
            Core::Containers::FixedArray<uint8_t, ZKilo(4)> buffer = {};
            return scratch.VisitRoot(0, buffer, [](void*, const VFS::VFSRootEntry&) { return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted); }, nullptr).Succeeded();
        }
    } // namespace

    int RunProjectSmoke(const std::filesystem::path& project, const std::filesystem::path& workspace, uint64_t maximum_working_bytes, uint32_t expected_entities)
    {
        try
        {
            std::error_code error  = {};
            const auto      config = std::filesystem::canonical(project, error);
            if (error || !maximum_working_bytes || maximum_working_bytes > std::numeric_limits<size_t>::max())
                return 2;
            const auto destination = std::filesystem::canonical(workspace, error);
            if (error || Contains(config.parent_path(), destination) || Contains(destination, config.parent_path()) || !std::filesystem::is_directory(destination, error) || error || !std::filesystem::is_empty(destination, error) || error)
            {
                std::cerr << "Smoke workspace must be an existing empty directory outside the project. No input files are changed.\n";
                return 2;
            }
            ProjectInputs inputs = {};
            if (!ReadProject(config, inputs))
            {
                std::cerr << "Unsupported smoke project configuration (workingSpace '.', bounded sceneList and one common asset tree required).\n";
                return 2;
            }
            CookPageAllocator heap           = {};
            const auto        native_backing = heap.Initialize(maximum_working_bytes);
            if (native_backing.Failed())
                return SetupFailure("native backing", native_backing.Error());
            CookBudgetAllocator allocator = {};
            const auto          allocated = allocator.Initialize(heap, maximum_working_bytes);
            if (allocated.Failed())
                return SetupFailure("backing allocator", allocated.Error());
            Core::Memory::MemoryManager memory = {};
            memory.Initialize(ZMega(16), {});
            VFS::VFSDiskBackend input_disk = {}, output_disk = {};
            const auto          native_input  = config.parent_path().generic_u8string();
            const auto          native_output = destination.generic_u8string();
            input_disk.Initialize(reinterpret_cast<cstring>(native_input.c_str()), VFS::VFSBackendCaps::Read | VFS::VFSBackendCaps::List, &memory.MainArena);
            output_disk.Initialize(reinterpret_cast<cstring>(native_output.c_str()), VFS::VFSBackendCaps::Read | VFS::VFSBackendCaps::Write | VFS::VFSBackendCaps::List, &memory.MainArena);
            VFS::VFSContext source_files = {}, output_files = {};
            source_files.Initialize(&memory.MainArena);
            output_files.Initialize(&memory.MainArena);
            struct ShutdownContexts
            {
                VFS::VFSContext& Input;
                VFS::VFSContext& Output;
                ~ShutdownContexts()
                {
                    Output.Shutdown();
                    Input.Shutdown();
                }
            } shutdown                = {source_files, output_files};
            const auto mounted_input  = source_files.Mount(&input_disk, VFS::VFSPath::Root(), 0);
            const auto mounted_output = output_files.Mount(&output_disk, VFS::VFSPath::Root(), 0);
            if (mounted_input.Failed() || mounted_output.Failed())
                return SetupFailure("mounts", mounted_input.Failed() ? mounted_input.Error() : mounted_output.Error());
            {
                // Canonical strings above improve diagnostics, but are not the
                // safety boundary on case-insensitive/aliased filesystems.
                VFS::VFSReadRootLease project_root = {}, workspace_root = {};
                const auto            input  = source_files.AcquireReadRoot(VFS::VFSPath::Root(), &project_root);
                const auto            output = output_files.AcquireReadRoot(VFS::VFSPath::Root(), &workspace_root);
                if (input.Failed() || output.Failed())
                    return SetupFailure("physical roots", input.Failed() ? input.Error() : output.Error());
                const auto overlap = project_root.Overlaps(workspace_root);
                if (overlap.Failed())
                    return SetupFailure("physical ancestry", overlap.Error());
                if (overlap.Value())
                {
                    std::cerr << "Project smoke refuses physically overlapping project/workspace roots.\n";
                    return 2;
                }
            }
            ECS::SceneComponentSchemaRegistry schemas = {};
            schemas.Initialize(&memory.MainArena);
            CookDiagnostics       diagnostics       = {};
            ECS::SceneDiagnostics scene_diagnostics = {};
            if (!ECS::RegisterBuiltInSceneComponentSchemas(schemas, &scene_diagnostics))
            {
                PrintDiagnostics(diagnostics, scene_diagnostics);
                return SetupFailure("built-in schemas", VFS::VFSError::Unsupported);
            }
            CookAdapterRegistry  adapters    = {};
            const CookAdapterFns functions[] = {CookEnvironmentAdapter(), CookMaterialAdapter(), CookMeshAdapter(), CookTextureAdapter()};
            const auto           registered  = adapters.Initialize(functions);
            if (registered.Failed())
                return SetupFailure("adapters", registered.Error());
            Core::Containers::FixedArray<CookFrozenRootRequest, 3> roots = {};
            roots[0]                                                     = {"scenes", &source_files, inputs.SceneRoot};
            roots[1]                                                     = {"assets", &source_files, inputs.AssetRoot};
            size_t     root_count                                        = 2;
            const auto envmaps                                           = Path("/_cache/envmaps");
            const auto cached                                            = source_files.Stat(envmaps);
            if (cached.Succeeded())
            {
                if (!cached.Value().IsDirectory)
                    return SetupFailure("environment artifact directory", VFS::VFSError::NotADirectory);
                roots[root_count++] = {"envmaps", &source_files, envmaps};
            }
            else if (cached.Error() != VFS::VFSError::NotFound)
                return SetupFailure("environment artifact directory", cached.Error());
            for (cstring path : {"/output", "/scratch"})
            {
                const auto created = output_files.CreateDir(Path(path));
                if (created.Failed())
                    return SetupFailure("output/scratch directories", created.Error());
            }
            const CookLimits limits  = {.MaxWorkingBytes = maximum_working_bytes};
            SceneCookResult  first   = {};
            bool             success = true;
            for (uint32_t run = 0; run < 3 && success; ++run)
            {
                CookFrozenLibraryOwner owner  = {};
                const auto             frozen = owner.Initialize(allocator, {roots.data(), root_count});
                if (frozen.Failed())
                    return SetupFailure("frozen library owner", frozen.Error());
                CookFrozenInputSource  source(owner);
                const SceneCookRequest request = {
                    {inputs.Scenes.data(), inputs.SceneCount},
                    Path("/output"), CookTarget::DesktopVulkanLE64, run == 2
                };
                const SceneCookContext context = {&source, &output_files, &output_files, Path("/scratch"), &schemas, nullptr, &adapters, &allocator, limits};
                SceneCookResult        result  = {};
                const auto             cooked  = SceneCooker::Cook(request, context, &result, &diagnostics);
                success                        = cooked.Succeeded() && result.SceneCount == inputs.SceneCount && result.Published == (run == 0) && result.PeakWorkingBytes <= maximum_working_bytes;
                if (success && expected_entities != UINT32_MAX)
                    success = result.SceneCount == 1 && result.AssetCount == 4 && result.ObjectCount == 5 && result.ReferencedObjectBytes > ZMega(115);
                if (success && run)
                    success = result.ManifestDigest == first.ManifestDigest && result.SceneCount == first.SceneCount && result.AssetCount == first.AssetCount && result.ObjectCount == first.ObjectCount;
                if (success && !run)
                    first = result;
                if (success)
                    success = VerifyPackage(output_files, result, limits, allocator, schemas, diagnostics, scene_diagnostics, expected_entities);
                if (success)
                    success = ScratchEmpty(output_files);
                std::cout << "Smoke " << (run == 0 ? "publish" : run == 1 ? "recaptured-no-op" : "forced") << ": scenes=" << result.SceneCount << " assets=" << result.AssetCount << " objects=" << result.ObjectCount << " referenced-bytes=" << result.ReferencedObjectBytes << " cook-peak-bytes=" << result.PeakWorkingBytes;
                static constexpr Core::Containers::FixedArray<cstring, static_cast<size_t>(CookMemoryCategory::Count)> categories = {"catalog-graph", "captures", "codecs", "staging-manifest", "diagnostics-result"};
                for (size_t category = 0; category < categories.size(); ++category)
                    std::cout << " cook-peak-" << categories[category] << "-bytes=" << result.PeakWorkingBytesByCategory[category];
                std::cout << '\n';
            }
            // The large mixed fixture must retain the published package through
            // capture/staging-budget refusal, not merely reject a large
            // isolated adapter input. Every attempt owns fresh captures.
            for (uint32_t refusal = 0; expected_entities != UINT32_MAX && refusal < 2 && success; ++refusal)
            {
                auto bounded = limits;
                if (refusal == 0)
                    bounded.MaxCapturedBytes = ZMega(100);
                else
                    // Existing immutable duplicates release their temporary
                    // charge; this cap must be below the largest 64 MiB object,
                    // not below the sum of already-installed package objects.
                    bounded.MaxStagedBytes = ZMega(63);
                CookFrozenLibraryOwner owner  = {};
                const auto             frozen = owner.Initialize(allocator, {roots.data(), root_count});
                if (frozen.Failed())
                    return SetupFailure("bounded frozen library owner", frozen.Error());
                CookFrozenInputSource  source(owner);
                const SceneCookRequest request = {
                    {inputs.Scenes.data(), inputs.SceneCount},
                    Path("/output"), CookTarget::DesktopVulkanLE64, true
                };
                const SceneCookContext context = {&source, &output_files, &output_files, Path("/scratch"), &schemas, nullptr, &adapters, &allocator, bounded};
                SceneCookResult        result  = first;
                const auto             cooked  = SceneCooker::Cook(request, context, &result, &diagnostics);
                success                = cooked.Error() == VFS::VFSError::SizeLimitExceeded && diagnostics.HasErrors() && result.ManifestPath == first.ManifestPath && result.ManifestDigest == first.ManifestDigest && result.SceneCount == first.SceneCount && result.AssetCount == first.AssetCount && result.ObjectCount == first.ObjectCount && result.ReferencedObjectBytes == first.ReferencedObjectBytes && result.PeakWorkingBytes == first.PeakWorkingBytes && result.PeakWorkingBytesByCategory == first.PeakWorkingBytesByCategory && result.Published == first.Published;
                bool budget_diagnostic = false;
                for (uint32_t i = 0; i < diagnostics.Count; ++i)
                    budget_diagnostic |= diagnostics.Entries[i].Code == CookDiagnosticCode::BudgetExceeded;
                success &= budget_diagnostic;
                if (success)
                {
                    diagnostics.Clear();
                    success = ScratchEmpty(output_files) && VerifyPackage(output_files, first, limits, allocator, schemas, diagnostics, scene_diagnostics, expected_entities);
                }
                std::cout << "Smoke " << (refusal == 0 ? "aggregate capture" : "staging") << " refusal: preserved=" << success << '\n';
            }
            PrintDiagnostics(diagnostics, scene_diagnostics);
            output_files.Shutdown();
            source_files.Shutdown();
            success &= allocator.OutstandingAllocations() == 0 && heap.OutstandingAllocations() == 0;
            std::cout << "Smoke backing-peak-bytes=" << allocator.Statistics().PeakBytes << " native-mapping-peak-bytes=" << heap.Statistics().PeakBytes << " allocations=" << heap.AllocationCount() << " frees=" << heap.FreeCount() << '\n';
            allocator.Release();
            if (!success)
                return 3;
#if defined(__APPLE__) || defined(__linux__)
            // Observational process high-water mark, NOT a cook-owned memory
            // charge or budget assertion. Includes setup, all three cooks,
            // refusal probes, package verification and the process baseline.
            rusage usage = {};
            if (::getrusage(RUSAGE_SELF, &usage) != 0 || usage.ru_maxrss <= 0)
            {
                std::cerr << "Project smoke peak-RSS query failed\n";
                return 2;
            }
#if defined(__APPLE__)
            constexpr uint64_t resident_unit = 1; // macOS reports bytes.
#else
            constexpr uint64_t resident_unit = 1024; // Linux reports KiB.
#endif
            const auto resident = static_cast<uint64_t>(usage.ru_maxrss);
            if (resident > UINT64_MAX / resident_unit)
                return 2;
            std::cout << "Smoke process-peak-resident-bytes=" << resident * resident_unit << '\n';
#endif
            std::cout << "ZE_COOK_PROJECT_SMOKE_VERIFIED\n";
            return 0;
        }
        catch (const std::exception& error)
        {
            std::cerr << "Project smoke setup failed: " << error.what() << '\n';
            return 2;
        }
    }
} // namespace ZEngine::Cook::Tests
