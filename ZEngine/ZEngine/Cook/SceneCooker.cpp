#include <ZEngine/Cook/CookAdapterSupport.h>
#include <ZEngine/Cook/CookAssetDiscovery.h>
#include <ZEngine/Cook/CookCallbackStorage.h>
#include <ZEngine/Cook/CookPublication.h>
#include <ZEngine/Cook/CookSceneCaptureSet.h>
#include <ZEngine/Cook/CookServiceMemoryInternal.h>
#include <ZEngine/Cook/CookVFSWorkingMemory.h>
#include <ZEngine/Cook/SceneCooker.h>
#include <ZEngine/ECS/SceneCompatibility.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <algorithm>
#include <new>
#include <type_traits>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        struct CookRunPaths
        {
            VFS::VFSRelativePath Target   = {};
            VFS::VFSPath         Manifest = {};
        };

        struct CookSetupQueryValues
        {
            VFS::VFSResult<uint64_t>                      Bytes    = {};
            VFS::VFSResult<CookServiceMemoryRequirements> Services = {};
        };

        VFS::VFSResult<void> RunFailure(CookDiagnostics* diagnostics, CookStage stage, VFS::VFSError error, cstring message, CookDiagnosticCode code = CookDiagnosticCode::None)
        {
            if (code == CookDiagnosticCode::None)
            {
                code = Detail::AdapterErrorCode(error);
            }
            if (auto* entry = diagnostics ? diagnostics->Emplace(stage, code, error) : nullptr)
            {
                (void) entry->SetMessage(message);
            }
            return VFS::VFSResult<void>::Fail(CookDiagnosticError(code, error));
        }

        VFS::VFSResult<void> PrepareRunPaths(ICookAllocator& allocator, const VFS::VFSPath& root, cstring target, CookRunPaths* output)
        {
            CookMemoryCharge fixed        = {};
            // Principal Append/Parse storage: two returned paths, Parse's owned
            // value and Ok argument, Append's combined text, Parse's text and
            // component tables. Audit with VFSPath changes; not compiler frames.
            constexpr size_t path_scratch = 2 * sizeof(VFS::VFSResult<VFS::VFSPath>) + 2 * sizeof(VFS::VFSPath) + 3 * VFS::VFS_MAX_PATH + VFS::VFS_MAX_COMPONENTS * (sizeof(cstring) + sizeof(uint16_t));
            const auto       charged      = allocator.TryCharge(path_scratch + sizeof(Core::Containers::StringView) + sizeof(VFS::VFSResult<uint16_t>) + sizeof(fixed), CookMemoryCategory::StagingManifest, &fixed);
            if (charged.Failed())
            {
                return charged;
            }
            auto status = VFS::VFSRelativePath::ParseInto({target, Helpers::secure_strlen(target)}, &output->Target);
            if (status.Failed())
            {
                return status;
            }
            const auto directory = root.Append(target);
            if (directory.Failed())
            {
                return VFS::VFSResult<void>::Fail(directory.Error());
            }
            const auto manifest = directory.Value().Append(CookManifestName);
            if (manifest.Failed())
            {
                return VFS::VFSResult<void>::Fail(manifest.Error());
            }
            output->Manifest = manifest.Value();
            return VFS::VFSResult<void>::Ok();
        }

        // One reusable block, not an arena that retains every superseded table.
        // Views are dead before growth; release old backing BEFORE allocating
        // the replacement. Failure is terminal for this run, never a heap fallback.
        class RunBuffer
        {
        public:
            explicit RunBuffer(ICookAllocator& allocator) noexcept : m_allocator(&allocator) {}
            ~RunBuffer()
            {
                Reset();
            }
            RunBuffer(const RunBuffer&)                                    = delete;
            RunBuffer&                         operator=(const RunBuffer&) = delete;

            [[nodiscard]] VFS::VFSResult<void> Ensure(size_t bytes, CookMemoryCategory category)
            {
                if (bytes <= m_bytes)
                {
                    return VFS::VFSResult<void>::Ok();
                }
                Reset();
                const auto allocated = m_allocator->Allocate(bytes, DEFAULT_ALIGNMENT, category);
                if (allocated.Failed())
                {
                    return VFS::VFSResult<void>::Fail(allocated.Error());
                }
                if (!allocated.Value())
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::OutOfMemory);
                }
                m_data  = allocated.Value();
                m_bytes = bytes;
                return VFS::VFSResult<void>::Ok();
            }

            void Reset() noexcept
            {
                if (m_data)
                {
                    m_allocator->Free(m_data);
                }
                m_data  = nullptr;
                m_bytes = 0;
            }

            template <typename T>
            [[nodiscard]] T* Data() const noexcept
            {
                return static_cast<T*>(m_data);
            }
            [[nodiscard]] size_t Size() const noexcept
            {
                return m_bytes;
            }

        private:
            ICookAllocator* m_allocator = nullptr;
            void*           m_data      = nullptr;
            size_t          m_bytes     = 0;
        };

        // Fixed run state is charged before construction. Dynamic storage goes
        // through the same checked allocator. Member order and explicit Cleanup
        // keep captures/provider storage ahead of scratch/VFS-policy teardown.
        class CookRun
        {
        public:
            CookRun(const SceneCookRequest& request, const SceneCookContext& context, CookBudgetAllocator& allocator, CookVFSWorkingMemory& memory, CookDiagnostics& diagnostics) noexcept : m_request(&request), m_context(&context), m_allocator(&allocator), m_memory(&memory), m_diagnostics(&diagnostics), m_codec(allocator, *context.Schemas, context.Migrations), m_words(allocator), m_stream(allocator), m_dependencies(allocator) {}
            ~CookRun()
            {
                Cleanup();
            }
            CookRun(const CookRun&)                                      = delete;
            CookRun&                           operator=(const CookRun&) = delete;

            [[nodiscard]] VFS::VFSResult<void> Execute(const VFS::VFSRelativePath& target, const VFS::VFSPath& manifest_path, SceneCookResult* output)
            {
                CookMemoryCharge fixed  = {};
                // Distinct from retained CookRun members and the already charged
                // CookPublicationResult: principal Execute arguments/locals only,
                // not a compiler-frame or recursive helper-stack estimate.
                auto             status = m_allocator->TryCharge(
                    sizeof(CookCaptureContext) + sizeof(CookPublicationInput) + sizeof(CookSceneInput) + sizeof(Digest256) + sizeof(CookMemoryStatistics) + sizeof(Core::Containers::ArrayView<uint8_t>) + sizeof(Core::Containers::ArrayView<const VFS::VFSPath>) + sizeof(Core::Containers::ArrayView<const uint32_t>) + 2 * sizeof(Core::Containers::ArrayView<uint32_t>) + sizeof(VFS::VFSResult<bool>) + sizeof(VFS::VFSResult<size_t>) + 2 * sizeof(VFS::VFSResult<void>) + 2 * sizeof(uint64_t) + sizeof(fixed), CookMemoryCategory::StagingManifest, &fixed);
                if (status.Failed())
                {
                    return RunFailure(m_diagnostics, m_stage, status.Error(), "The working budget cannot hold execution state.");
                }
                status           = m_context->Inputs->AcquireRoots(&m_inputs, m_diagnostics);
                m_provider_owned = m_inputs.IsValid();
                if (status.Failed())
                {
                    return status;
                }
                if (m_inputs.State() != CookInputState::Rooted)
                {
                    return RunFailure(m_diagnostics, m_stage, VFS::VFSError::Unsupported, "Input provider must acquire rooted exclusion without preparing captures.", CookDiagnosticCode::UnsupportedBackend);
                }
                status = m_context->Output->AcquirePublicationRoot(m_request->OutputRoot, &m_output, m_memory);
                if (status.Failed())
                {
                    return status;
                }
                status = m_context->Scratch->AcquireScratchRoot(m_context->ScratchRoot, &m_scratch, m_memory);
                if (status.Failed())
                {
                    return status;
                }
                // No output or scratch directory is created before all physical
                // domains are proven disjoint while rooted input exclusion holds.
                status = m_inputs.RequireDisjoint(m_output, m_diagnostics);
                if (status.Failed())
                {
                    return status;
                }
                status = m_inputs.RequireDisjoint(m_scratch, m_diagnostics);
                if (status.Failed())
                {
                    return status;
                }
                const auto overlap = m_scratch.Overlaps(m_output);
                if (overlap.Failed())
                {
                    return VFS::VFSResult<void>::Fail(overlap.Error());
                }
                if (overlap.Value())
                {
                    return RunFailure(m_diagnostics, m_stage, VFS::VFSError::PermissionDenied, "Output and scratch roots must be physically disjoint.", CookDiagnosticCode::InvalidRequest);
                }

                const auto schema_words = CookSchemaKeyWorkspaceWords(*m_context->Schemas);
                if (schema_words.Failed())
                {
                    return VFS::VFSResult<void>::Fail(schema_words.Error());
                }
                status = EnsureWords(schema_words.Value());
                if (status.Failed())
                {
                    return status;
                }
                status = ComputeCookRecipeKeys(*m_allocator, *m_context->Schemas, m_request->Target, Words(schema_words.Value()), &m_recipe);
                if (status.Failed())
                {
                    return status;
                }

                status = VFS::CreatePrivateScratchDirectory(m_scratch, &m_scratch_run);
                if (status.Failed())
                {
                    return status;
                }
                status = m_output.CreateDir(target);
                if (status.Failed())
                {
                    return status;
                }
                status = m_output.AcquireChildRoot(target, &m_package);
                if (status.Failed())
                {
                    return status;
                }

                m_stage                          = CookStage::Capture;
                const CookCaptureContext capture = {&m_scratch, m_scratch_run, m_allocator, &m_context->Limits, m_memory};
                status                           = m_inputs.PrepareCapture(capture, m_diagnostics);
                if (status.Failed())
                {
                    return status;
                }
                status = m_graph.Initialize(*m_allocator, m_context->Limits);
                if (status.Failed())
                {
                    return status;
                }
                status = m_scenes.Capture(*m_allocator, {m_request->Sources.data(), m_request->Sources.size()}, m_inputs, m_codec, m_graph, m_context->Limits, m_diagnostics);
                if (status.Failed())
                {
                    return status;
                }
                m_stage = CookStage::Discover;
                status  = m_assets.Initialize(*m_allocator, m_context->Limits);
                if (status.Failed())
                {
                    return status;
                }
                status = m_assets.Discover(m_graph, m_inputs, *m_context->Adapters, *m_allocator, m_diagnostics);
                if (status.Failed())
                {
                    return status;
                }
                status = m_inputs.Seal(m_diagnostics);
                if (status.Failed())
                {
                    return status;
                }

                // The same small index workspace serves schema hashing and final
                // manifest sorting. No allocation for either when <= 32 words.
                status = EnsureWords(static_cast<size_t>(m_graph.NodeCount()) + m_graph.SceneCount());
                if (status.Failed())
                {
                    return status;
                }
                const auto available    = CookAllocationWindow(*m_allocator, m_context->Limits.MaxWorkingBytes);
                const auto stream_bytes = static_cast<size_t>(std::min<uint64_t>(ZKilo(64), available / 4));
                if (!stream_bytes)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                }
                status = m_stream.Ensure(stream_bytes, CookMemoryCategory::StagingManifest);
                if (status.Failed())
                {
                    return status;
                }
                status = m_publication.Initialize(*m_allocator, {&m_package, &m_inputs, &m_graph, m_recipe, &m_context->Limits, m_request->ForceRebuild}, {m_stream.Data<uint8_t>(), stream_bytes}, m_diagnostics);
                if (status.Failed())
                {
                    return status;
                }
                // Captures retain independent authority. Input indexes/exclusion
                // can now go away; encoding never reopens the original library.
                m_inputs.Reset();

                m_stage = CookStage::Encode;
                for (uint32_t node : m_graph.LeafFirstNodes())
                {
                    m_current = node;
                    status    = m_publication.EncodeAsset(node, ProduceAsset, this, m_diagnostics);
                    if (status.Failed())
                    {
                        return status;
                    }
                }
                for (uint32_t scene = 0; scene < m_scenes.Count(); ++scene)
                {
                    CookSceneInput input = {};
                    status               = m_scenes.InputInto(scene, &input);
                    if (status.Failed())
                    {
                        return status;
                    }
                    m_current = scene;
                    status    = m_publication.EncodeScene(scene, input.Capture->Digest(), ProduceScene, this, m_diagnostics);
                    if (status.Failed())
                    {
                        return status;
                    }
                }
                // A faulty service cannot commit after reporting an error while
                // returning success. Diagnostics storage never needs to grow.
                if (m_diagnostics->HasErrors())
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                }

                m_stage                         = CookStage::Publish;
                // The caller supplies a zero-initialized pending result. Fill it
                // directly, without constructing another path-owning aggregate.
                output->ManifestPath            = manifest_path;
                output->SceneCount              = m_graph.SceneCount();
                output->AssetCount              = m_graph.NodeCount();
                CookPublicationResult published = {};
                status                          = m_publication.PublishManifest(Words(static_cast<size_t>(m_graph.NodeCount()) + m_graph.SceneCount()), &published, m_diagnostics);
                if (status.Failed())
                {
                    return status;
                }
                // Publication prepares its digest/count/size/commit result before
                // rename. All remaining handoff is fixed-value, non-failing work;
                // no producer, parser, allocator or validation callback runs here.
                output->ManifestDigest             = published.Manifest.Digest;
                output->ObjectCount                = published.ObjectCount;
                output->ReferencedObjectBytes      = published.ReferencedObjectBytes;
                const auto statistics              = m_allocator->Statistics();
                output->PeakWorkingBytes           = statistics.PeakBytes;
                output->PeakWorkingBytesByCategory = statistics.PeakByCategory;
                output->Published                  = published.Published;
                return VFS::VFSResult<void>::Ok();
            }

            void Cleanup() noexcept
            {
                if (m_cleaned)
                {
                    return;
                }
                m_cleaned = true;
                (void) m_publication.Reset(m_diagnostics);
                m_assets.Reset();
                Warning(m_assets.CleanupError(), "Could not remove an owned asset capture.");
                Warning(m_scenes.Reset().Error(), "Could not remove an owned scene capture.");
                m_inputs.Reset();
                if (m_provider_owned)
                {
                    Warning(m_context->Inputs->ReleaseStorage().Error(), "Input provider could not finish releasing its run storage.");
                }
                m_graph.Reset();
                m_dependencies.Reset();
                m_stream.Reset();
                m_words.Reset();
                if (m_scratch_run.IsValid())
                {
                    // Nonrecursive: unknown/failed leftovers are preserved, never
                    // treated as owned just because their names look temporary.
                    Warning(m_scratch.Remove(m_scratch_run).Error(), "Could not remove this run's private scratch directory.");
                    m_scratch_run = {};
                }
                m_package.Reset();
                m_output.Reset();
                m_scratch.Reset();
            }

            [[nodiscard]] CookStage Stage() const noexcept
            {
                return m_stage;
            }

        private:
            [[nodiscard]] VFS::VFSResult<void> EnsureWords(size_t count)
            {
                if (count <= m_inline_words.size())
                {
                    return VFS::VFSResult<void>::Ok();
                }
                if (count > SIZE_MAX / sizeof(uint32_t))
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                }
                return m_words.Ensure(count * sizeof(uint32_t), CookMemoryCategory::StagingManifest);
            }

            [[nodiscard]] Core::Containers::ArrayView<uint32_t> Words(size_t count) noexcept
            {
                return {count <= m_inline_words.size() ? m_inline_words.data() : m_words.Data<uint32_t>(), count};
            }

            void Warning(VFS::VFSError error, cstring message) const noexcept
            {
                if (error == VFS::VFSError::OK)
                {
                    return;
                }
                if (auto* entry = m_diagnostics->Emplace(CookStage::Cleanup, CookDiagnosticCode::CleanupWarning, error))
                {
                    entry->Source = m_scratch_run;
                    (void) entry->SetMessage(message);
                }
            }

            static VFS::VFSResult<void> ProduceAsset(void* state, ICookByteSink& sink)
            {
                auto&            run     = *static_cast<CookRun*>(state);
                CookMemoryCharge fixed   = {};
                // Resolved entries are constructed directly in the separately
                // charged reusable buffer; no second dependency object is made.
                const auto       charged = run.m_allocator->TryCharge(sizeof(Core::Containers::ArrayView<const CookGraphEdge>) + sizeof(Core::Containers::ArrayView<const CookAssetResult>) + sizeof(Core::Containers::ArrayView<const CookResolvedDependency>) + sizeof(Core::Containers::StringView) + 3 * sizeof(VFS::VFSResult<void>) + 2 * sizeof(size_t) + sizeof(fixed), CookMemoryCategory::Codecs, &fixed);
                if (charged.Failed())
                {
                    return charged;
                }
                const auto input = run.m_assets.ReadInput(run.m_current, *run.m_allocator, &run.m_metadata, &run.m_asset_input);
                if (input.Failed())
                {
                    return input;
                }
                const auto   edges    = run.m_graph.Dependencies(run.m_current);
                const size_t capacity = run.m_dependencies.Size() / sizeof(CookResolvedDependency);
                if (edges.size() > capacity)
                {
                    // Geometric capacity, but no relocation/copy: each asset
                    // completely rewrites this reusable borrowed result table.
                    const size_t count = std::max(edges.size(), std::min<size_t>(run.m_context->Limits.MaxEdges, capacity * 2));
                    if (count > SIZE_MAX / sizeof(CookResolvedDependency))
                    {
                        return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                    }
                    const auto grown = run.m_dependencies.Ensure(count * sizeof(CookResolvedDependency), CookMemoryCategory::Codecs);
                    if (grown.Failed())
                    {
                        return grown;
                    }
                }
                auto*      resolved = run.m_dependencies.Data<CookResolvedDependency>();
                const auto results  = run.m_publication.AssetResults();
                for (size_t i = 0; i < edges.size(); ++i)
                {
                    const auto& edge  = edges[i];
                    const auto* child = run.m_graph.Node(edge.Child);
                    if (!child || edge.Child >= results.size() || !results[edge.Child].Object.Path.IsValid())
                    {
                        return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                    }
                    const auto& result = results[edge.Child];
                    new (resolved + i) CookResolvedDependency{run.m_graph.Slot(edge), child->Identity, result.InputKey, child->Format, result.Object.Path, result.Object.Digest, result.Object.Size};
                }
                const auto* adapter = run.m_context->Adapters->Find(run.m_asset_input.Identity.Kind);
                if (!adapter || !adapter->Encode)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
                }
                return adapter->Encode(adapter->Context, run.m_asset_input, {resolved, edges.size()}, run.m_request->Target, sink, *run.m_allocator, run.m_diagnostics);
            }

            static VFS::VFSResult<void> ProduceScene(void* state, ICookByteSink& sink)
            {
                auto&            run     = *static_cast<CookRun*>(state);
                CookMemoryCharge fixed   = {};
                const auto       charged = run.m_allocator->TryCharge(sizeof(CookSceneInput) + sizeof(CookSceneOutput) + 2 * sizeof(VFS::VFSResult<void>) + sizeof(fixed), CookMemoryCategory::Codecs, &fixed);
                if (charged.Failed())
                {
                    return charged;
                }
                CookSceneInput input  = {};
                const auto     status = run.m_scenes.InputInto(run.m_current, &input);
                if (status.Failed())
                {
                    return status;
                }
                const auto* scene = run.m_graph.Scene(run.m_current);
                if (!scene)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                }
                CookSceneOutput output = {};
                return run.m_codec.Encode(input, scene->UUID, run.m_request->Target, sink, &output, run.m_diagnostics);
            }

            const SceneCookRequest*                    m_request     = nullptr;
            const SceneCookContext*                    m_context     = nullptr;
            CookBudgetAllocator*                       m_allocator   = nullptr;
            CookVFSWorkingMemory*                      m_memory      = nullptr;
            CookDiagnostics*                           m_diagnostics = nullptr;
            VFS::VFSPublicationRootLease               m_output      = {};
            VFS::VFSPublicationRootLease               m_package     = {};
            VFS::VFSScratchRootLease                   m_scratch     = {};
            VFS::VFSRelativePath                       m_scratch_run = {};
            CookInputLease                             m_inputs      = {};
            CookDependencyGraph                        m_graph       = {};
            CookSceneCaptureSet                        m_scenes      = {};
            CookAssetDiscovery                         m_assets      = {};
            CookPublication                            m_publication = {};
            CookRecipeKeys                             m_recipe      = {};
            CookSceneCodec                             m_codec;
            VFS::StrictMetaFileData                    m_metadata     = {};
            CookAssetInput                             m_asset_input  = {};
            Core::Containers::FixedArray<uint32_t, 32> m_inline_words = {};
            RunBuffer                                  m_words;
            RunBuffer                                  m_stream;
            RunBuffer                                  m_dependencies;
            uint32_t                                   m_current        = 0;
            CookStage                                  m_stage          = CookStage::Setup;
            bool                                       m_provider_owned = false;
            bool                                       m_cleaned        = false;
        };
    } // namespace

    VFS::VFSResult<void> SceneCooker::Cook(const SceneCookRequest& request, const SceneCookContext& context, SceneCookResult* out_result, CookDiagnostics* diagnostics)
    {
        if (diagnostics)
        {
            diagnostics->Clear();
        }
        if (!out_result || !diagnostics || !context.Inputs || !context.Output || !context.Scratch || !context.Schemas || !context.Adapters || !context.Allocator || !context.Schemas->IsInitialized() || !context.Adapters->IsInitialized() || (context.Migrations && !context.Migrations->IsInitialized()) || !request.Sources.data() || request.Sources.empty())
        {
            return RunFailure(diagnostics, CookStage::Setup, VFS::VFSError::InvalidPath, "Cooking requires a nonempty scene batch, initialized services and caller-owned result/diagnostic storage.", CookDiagnosticCode::InvalidRequest);
        }
        if (ValidateCookLimits(context.Limits).Failed())
        {
            return RunFailure(diagnostics, CookStage::Setup, VFS::VFSError::InvalidPath, "Cook limits require an explicit positive working budget and supported hard caps.", CookDiagnosticCode::InvalidBudget);
        }
        if (request.Sources.size() > context.Limits.MaxScenes)
        {
            return RunFailure(diagnostics, CookStage::Setup, VFS::VFSError::SizeLimitExceeded, "The complete scene batch exceeds the configured scene cap.", CookDiagnosticCode::BudgetExceeded);
        }
        const cstring target_key = CookTargetKey(request.Target);
        if (!target_key)
        {
            return RunFailure(diagnostics, CookStage::Setup, VFS::VFSError::Unsupported, "The cook target is unsupported.", CookDiagnosticCode::UnsupportedTarget);
        }
        if (!request.OutputRoot.IsValid() || !context.ScratchRoot.IsValid())
        {
            return RunFailure(diagnostics, CookStage::Setup, VFS::VFSError::InvalidPath, "Output and scratch roots must be valid VFS paths.", CookDiagnosticCode::InvalidPath);
        }
        for (const auto& source : request.Sources)
        {
            if (!source.IsValid() || source.IsRoot())
            {
                return RunFailure(diagnostics, CookStage::Setup, VFS::VFSError::InvalidPath, "Each scene source must name a file.", CookDiagnosticCode::InvalidPath);
            }
        }
        // Both suffixes are closed single-component spellings. Preflight only
        // scalar bounds before service setup, preserving invalid-path priority
        // without constructing owned paths outside the working ledger. Append
        // checks raw joined bytes before normalization (also for the root '/').
        const size_t target_bytes = request.OutputRoot.Length() + 1 + Helpers::secure_strlen(target_key);
        if (target_bytes >= VFS::VFS_MAX_PATH || request.OutputRoot.ComponentCount() + 1 >= VFS::VFS_MAX_COMPONENTS)
        {
            return RunFailure(diagnostics, CookStage::Setup, VFS::VFSError::InvalidPath, "The target directory exceeds the output path bound.", CookDiagnosticCode::InvalidPath);
        }
        const size_t manifest_bytes = target_bytes - (request.OutputRoot.IsRoot() ? 1 : 0) + 1 + Helpers::secure_strlen(CookManifestName);
        if (manifest_bytes >= VFS::VFS_MAX_PATH || request.OutputRoot.ComponentCount() + 2 >= VFS::VFS_MAX_COMPONENTS)
        {
            return RunFailure(diagnostics, CookStage::Setup, VFS::VFSError::InvalidPath, "The manifest exceeds the output path bound.", CookDiagnosticCode::InvalidPath);
        }

        // Fixed allocator objects inside declared native service storage are
        // already included by those profiles. Pool capacity is never discounted.
        // All profiles below must succeed and be charged before root acquisition.
        Detail::CookNativeServiceMemory services = {};
        auto                            status   = services.Initialize(context, context.Callbacks ? context.Callbacks->StorageArena() : nullptr);
        if (status.Failed())
        {
            return RunFailure(diagnostics, CookStage::Setup, status.Error(), "Caller services have no supported native storage profile.", status.Error() == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : CookDiagnosticCode::None);
        }
        CookBudgetAllocator allocator = {};
        status                        = allocator.Initialize(*context.Allocator, context.Limits.MaxWorkingBytes, &services);
        if (status.Failed())
        {
            return RunFailure(diagnostics, CookStage::Setup, status.Error(), "Could not initialize the working-memory ledger or the backing allocation-footprint contract.", status.Error() == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : CookDiagnosticCode::None);
        }
        CookMemoryCharge fixed = {}, provider = {}, registry_state = {}, registry_storage = {}, callback_state = {}, callback_storage = {}, caller = {}, vfs_services = {};
        // No retained state/control block is allocated for the orchestrator.
        // Include the pending result and inline run owners. Caller values are
        // queried separately to avoid duplicate native-family charges.
        // Allocator and VFS policy charge their own state.
        status = allocator.TryCharge(sizeof(CookRun) + sizeof(CookRunPaths) + sizeof(SceneCookResult) + sizeof(CookPublicationResult) + sizeof(services) + 8 * sizeof(CookMemoryCharge) + 2 * sizeof(VFS::VFSResult<void>) + 2 * sizeof(size_t) + sizeof(cstring), CookMemoryCategory::DiagnosticsResult, &fixed);
        if (status.Failed())
        {
            return RunFailure(diagnostics, CookStage::Setup, status.Error(), "The working budget cannot hold fixed run/result/diagnostic state.");
        }
        CookRunPaths paths = {};
        status             = PrepareRunPaths(allocator, request.OutputRoot, target_key, &paths);
        if (status.Failed())
        {
            return RunFailure(diagnostics, CookStage::Setup, status.Error(), "The working budget cannot prepare target/manifest paths.");
        }
        {
            CookMemoryCharge query_memory  = {};
            const size_t     query_scratch = std::max(Detail::CookNativeServiceMemory::ProviderQueryWorkingBytes(), std::max(Detail::CookNativeServiceMemory::CallerQueryWorkingBytes(), Detail::CookNativeServiceMemory::ServiceQueryWorkingBytes()));
            status                         = allocator.TryCharge(sizeof(CookSetupQueryValues) + query_scratch + sizeof(query_scratch) + sizeof(query_memory), CookMemoryCategory::DiagnosticsResult, &query_memory);
            if (status.Failed())
            {
                return RunFailure(diagnostics, CookStage::Setup, status.Error(), "The working budget cannot hold setup-query state.");
            }
            CookSetupQueryValues query = {};
            query.Bytes                = context.Inputs->UncoveredFixedWorkingBytes(allocator, services);
            if (query.Bytes.Failed())
            {
                return RunFailure(diagnostics, CookStage::Setup, query.Bytes.Error(), "Input provider has no bounded fixed-storage contract.", query.Bytes.Error() == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : CookDiagnosticCode::None);
            }
            status = allocator.TryCharge(query.Bytes.Value(), CookMemoryCategory::Captures, &provider);
            if (status.Failed())
            {
                return RunFailure(diagnostics, CookStage::Setup, status.Error(), "The input provider's fixed state exceeds the working budget.");
            }
            query.Services = services.RegistryRequirements(context);
            if (query.Services.Failed())
            {
                return RunFailure(diagnostics, CookStage::Setup, query.Services.Error(), "Native registry/callback/VFS service storage has no supported retained-memory contract.", query.Services.Error() == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : CookDiagnosticCode::None);
            }
            status = allocator.TryCharge(query.Services.Value().StateBytes, CookMemoryCategory::DiagnosticsResult, &registry_state);
            if (status.Succeeded())
            {
                status = allocator.TryCharge(query.Services.Value().StorageBytes, CookMemoryCategory::Codecs, &registry_storage);
            }
            if (status.Failed())
            {
                return RunFailure(diagnostics, CookStage::Setup, status.Error(), "Registry state/committed arena storage exceeds the working budget.");
            }
            query.Services = services.CallbackRequirements(context, allocator);
            if (query.Services.Failed())
            {
                return RunFailure(diagnostics, CookStage::Setup, query.Services.Error(), "Callback contexts require a complete bounded storage owner.", query.Services.Error() == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : CookDiagnosticCode::None);
            }
            status = allocator.TryCharge(query.Services.Value().StateBytes, CookMemoryCategory::DiagnosticsResult, &callback_state);
            if (status.Succeeded())
            {
                status = allocator.TryCharge(query.Services.Value().StorageBytes, CookMemoryCategory::Codecs, &callback_storage);
            }
            if (status.Failed())
            {
                return RunFailure(diagnostics, CookStage::Setup, status.Error(), "Callback context storage exceeds the working budget.");
            }
            query.Services = services.VFSRequirements();
            if (query.Services.Failed())
            {
                return RunFailure(diagnostics, CookStage::Setup, query.Services.Error(), "VFS caller facades require a complete native-storage contract.", query.Services.Error() == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : CookDiagnosticCode::None);
            }
            if (query.Services.Value().StateBytes > UINT64_MAX - query.Services.Value().StorageBytes)
            {
                return RunFailure(diagnostics, CookStage::Setup, VFS::VFSError::SizeLimitExceeded, "VFS caller-facade memory profile overflowed.");
            }
            status = allocator.TryCharge(query.Services.Value().StateBytes + query.Services.Value().StorageBytes, CookMemoryCategory::StagingManifest, &vfs_services);
            if (status.Failed())
            {
                return RunFailure(diagnostics, CookStage::Setup, status.Error(), "VFS caller-facade state/retained arena storage exceeds the working budget.");
            }
            query.Bytes = services.CallerRequirements(request, context, out_result, diagnostics);
            if (query.Bytes.Failed())
            {
                return RunFailure(diagnostics, CookStage::Setup, query.Bytes.Error(), "Caller value storage has no supported memory profile.");
            }
            status = allocator.TryCharge(query.Bytes.Value(), CookMemoryCategory::DiagnosticsResult, &caller);
            if (status.Failed())
            {
                return RunFailure(diagnostics, CookStage::Setup, status.Error(), "Caller request/source/result/diagnostic storage exceeds the working budget.");
            }
        }
        CookVFSWorkingMemory memory = {};
        status                      = memory.Initialize(allocator);
        if (status.Failed())
        {
            return RunFailure(diagnostics, CookStage::Setup, status.Error(), "Could not initialize checked VFS working-memory accounting.");
        }
        static_assert(std::is_nothrow_copy_assignable_v<SceneCookResult>);
        CookRun         run(request, context, allocator, memory, *diagnostics);
        SceneCookResult pending = {};
        status                  = run.Execute(paths.Target, paths.Manifest, &pending);
        if (status.Failed() && !diagnostics->HasErrors())
        {
            (void) RunFailure(diagnostics, run.Stage(), status.Error(), "The cook failed before manifest commit; the previous result and package are unchanged.", run.Stage() == CookStage::Setup && status.Error() == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : CookDiagnosticCode::None);
        }
        run.Cleanup();
        // Reset cannot fail once all reservations are drained. It allocates
        // nothing, so cleanup cannot increase the prepared memory high watermark.
        const auto released = memory.Reset();
        ZENGINE_VALIDATE_ASSERT(released.Succeeded(), "Cook run retained VFS reservations after cleanup.");
        if (status.Succeeded())
        {
            *out_result = pending;
        }
        return status;
    }
} // namespace ZEngine::Cook
