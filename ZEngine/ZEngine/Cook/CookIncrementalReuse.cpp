#include <ZEngine/Cook/CookIncrementalReuse.h>
#include <ZEngine/Cook/CookStorageLayout.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <rapidhash.h>
#include <algorithm>
#include <new>
#include <type_traits>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        CookDiagnosticCode ObjectFailureCode(VFS::VFSError error)
        {
            if (error == VFS::VFSError::Corrupted || error == VFS::VFSError::NotAFile)
            {
                return CookDiagnosticCode::CorruptObject;
            }
            if (error == VFS::VFSError::SizeLimitExceeded)
            {
                return CookDiagnosticCode::BudgetExceeded;
            }
            if (error == VFS::VFSError::OutOfMemory)
            {
                return CookDiagnosticCode::AllocationFailed;
            }
            if (error == VFS::VFSError::Unsupported)
            {
                return CookDiagnosticCode::UnsupportedBackend;
            }
            return CookDiagnosticCode::IOFailure;
        }

        CookDiagnosticCode KeyFailureCode(VFS::VFSError error, CookDiagnosticCode invalid)
        {
            return error == VFS::VFSError::SizeLimitExceeded || error == VFS::VFSError::OutOfMemory || error == VFS::VFSError::Unsupported ? ObjectFailureCode(error) : invalid;
        }

        VFS::VFSError CheckObject(const CookObjectDescriptor& object, cstring extension, uint64_t limit)
        {
            if (!IsCookObjectDescriptorValid(object, extension))
            {
                return VFS::VFSError::Corrupted;
            }
            return object.Size > limit ? VFS::VFSError::SizeLimitExceeded : VFS::VFSError::OK;
        }
    } // namespace

    CookIncrementalReuse::~CookIncrementalReuse()
    {
        Reset();
    }

    void CookIncrementalReuse::Reset() noexcept
    {
        static_assert(std::is_trivially_destructible_v<CookAssetResult> && std::is_trivially_destructible_v<CookSceneResult> && std::is_trivially_destructible_v<VerifiedObject>);
        if (m_allocator)
        {
            m_allocator->Free(m_block);
        }
        m_allocator   = nullptr;
        m_block       = nullptr;
        m_input       = {};
        m_stream      = {};
        m_assets      = nullptr;
        m_scenes      = nullptr;
        m_asset_state = nullptr;
        m_scene_state = nullptr;
        m_objects     = nullptr;
        m_index       = nullptr;
        m_closure     = {};
        m_asset_count = m_scene_count = m_object_count = m_object_capacity = m_index_capacity = 0;
    }

    VFS::VFSResult<void> CookIncrementalReuse::Initialize(ICookAllocator& allocator, const CookReuseInput& input, Core::Containers::ArrayView<uint8_t> stream)
    {
        static_assert(alignof(CookAssetResult) <= DEFAULT_ALIGNMENT && alignof(CookSceneResult) <= DEFAULT_ALIGNMENT && alignof(VerifiedObject) <= DEFAULT_ALIGNMENT);
        if (m_block || !input.Graph || !input.Graph->IsFinalized() || !input.Graph->SceneCount() || !input.Limits || ValidateCookLimits(*input.Limits).Failed() || !stream.data() || !stream.size() || !input.Reader.Open || !input.Reader.CloseChecked)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        if (input.Recipe.Revision != CookRecipeRevision || !CookTargetKey(input.Recipe.Target))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
        }
        Digest256  settings = {};
        const auto hashed   = ComputeCookSettingsKey(input.Recipe.Target, &settings);
        if (hashed.Failed())
        {
            return hashed;
        }
        if (input.Recipe.SettingsKey != settings)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        const auto& graph = *input.Graph;
        if (graph.NodeCount() > input.Limits->MaxAssets || graph.SceneCount() > input.Limits->MaxScenes || graph.EdgeCount() > input.Limits->MaxEdges || graph.ReferenceCount() > input.Limits->MaxReferenceOccurrences)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        for (uint32_t i = 0; i < graph.NodeCount(); ++i)
        {
            const auto& node   = *graph.Node(i);
            const auto* format = FindCookAssetFormat(node.Format);
            if (!format || format->Kind != node.Identity.Kind || format->Revision != node.AdapterRevision)
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
            }
        }
        const CookManifest* previous = input.Previous;
        if (previous && previous->IsValid())
        {
            if (previous->Recipe().Revision > input.Recipe.Revision || previous->Recipe().Target != input.Recipe.Target)
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
            }
            if (previous->Recipe().Revision != input.Recipe.Revision || previous->Recipe().SettingsKey != input.Recipe.SettingsKey)
            {
                previous = nullptr;
            }
        }
        else
        {
            previous = nullptr;
        }
        // Validated hard caps bound these additions and the power-of-two table.
        const uint32_t capacity       = graph.NodeCount() + graph.SceneCount();
        uint32_t       index_capacity = 1;
        while (index_capacity < 2 * capacity)
        {
            index_capacity *= 2;
        }
        Detail::CookStorageLayout layout = {};
        size_t                    assets = 0, scenes = 0, asset_states = 0, scene_states = 0, closure = 0, objects = 0, index = 0;
        if (!layout.Add<CookAssetResult>(graph.NodeCount(), &assets) || !layout.Add<CookSceneResult>(graph.SceneCount(), &scenes) || !layout.Add<State>(graph.NodeCount(), &asset_states) || !layout.Add<State>(graph.SceneCount(), &scene_states) || !layout.Add<uint32_t>(size_t(2) * graph.NodeCount(), &closure) || !layout.Add<VerifiedObject>(capacity, &objects) || !layout.Add<uint32_t>(index_capacity, &index))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        const auto allocation = allocator.Allocate(layout.Size(), DEFAULT_ALIGNMENT, CookMemoryCategory::StagingManifest);
        if (allocation.Failed())
        {
            return VFS::VFSResult<void>::Fail(allocation.Error());
        }
        if (!allocation.Value())
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::OutOfMemory);
        }
        if (reinterpret_cast<uintptr_t>(allocation.Value()) % DEFAULT_ALIGNMENT)
        {
            allocator.Free(allocation.Value());
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        m_allocator       = &allocator;
        m_block           = allocation.Value();
        m_input           = input;
        m_input.Previous  = previous;
        m_stream          = stream.first(std::min<size_t>(stream.size(), ZKilo(64)));
        auto* block       = static_cast<uint8_t*>(m_block);
        m_assets          = reinterpret_cast<CookAssetResult*>(block + assets);
        m_scenes          = reinterpret_cast<CookSceneResult*>(block + scenes);
        m_asset_state     = reinterpret_cast<State*>(block + asset_states);
        m_scene_state     = reinterpret_cast<State*>(block + scene_states);
        m_closure         = {reinterpret_cast<uint32_t*>(block + closure), size_t(2) * graph.NodeCount()};
        m_objects         = reinterpret_cast<VerifiedObject*>(block + objects);
        m_index           = reinterpret_cast<uint32_t*>(block + index);
        m_asset_count     = graph.NodeCount();
        m_scene_count     = graph.SceneCount();
        m_object_capacity = capacity;
        m_index_capacity  = index_capacity;
        for (uint32_t i = 0; i < m_asset_count; ++i)
        {
            new (m_assets + i) CookAssetResult{};
            new (m_asset_state + i) State{};
        }
        for (uint32_t i = 0; i < m_scene_count; ++i)
        {
            new (m_scenes + i) CookSceneResult{};
            new (m_scene_state + i) State{};
        }
        for (uint32_t i = 0; i < m_closure.size(); ++i)
        {
            new (m_closure.data() + i) uint32_t{};
        }
        for (uint32_t i = 0; i < m_index_capacity; ++i)
        {
            new (m_index + i) uint32_t(CookGraphInvalidIndex);
        }
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSError CookIncrementalReuse::Failure(CookDiagnostics* diagnostics, uint32_t index, bool scene, CookDiagnosticCode code, VFS::VFSError error, cstring message, const CookObjectDescriptor* object) const
    {
        if (auto* retained = diagnostics ? diagnostics->Emplace(CookStage::Encode, code, error) : nullptr)
        {
            auto&      diagnostic = *retained;
            const auto severity   = diagnostic.Severity;
            if (m_input.Graph)
            {
                if (scene)
                {
                    if (const auto* record = m_input.Graph->Scene(index))
                    {
                        diagnostic.SceneUUID = record->UUID;
                        diagnostic.Source    = record->Source;
                    }
                }
                else
                {
                    (void) m_input.Graph->Describe(index, code, message, &diagnostic);
                }
            }
            diagnostic.Stage      = CookStage::Encode;
            diagnostic.Code       = code;
            // Describe fills an error record; cache misses remain warnings and
            // must agree with Emplace's already-finalized error/drop counters.
            diagnostic.Severity   = severity;
            diagnostic.Underlying = error;
            if (object)
            {
                diagnostic.Source = object->Path;
            }
            (void) diagnostic.SetMessage(message);
        }
        return error;
    }

    VFS::VFSResult<bool> CookIncrementalReuse::Verify(const CookObjectDescriptor& object, cstring extension)
    {
        const size_t length = Helpers::secure_strlen(extension);
        if (!length || length >= sizeof(VerifiedObject::Extension))
        {
            return VFS::VFSResult<bool>::Fail(VFS::VFSError::Unsupported);
        }
        const uint64_t seed = rapidhash(extension, length);
        uint32_t       slot = static_cast<uint32_t>(rapidhash_withSeed(object.Digest.Bytes.data(), object.Digest.Bytes.size(), seed)) & (m_index_capacity - 1);
        while (m_index[slot] != CookGraphInvalidIndex)
        {
            const auto& entry = m_objects[m_index[slot]];
            if (entry.Digest == object.Digest && Helpers::secure_strcmp(entry.Extension, extension) == 0)
            {
                return entry.Size == object.Size ? VFS::VFSResult<bool>::Ok(true) : VFS::VFSResult<bool>::Fail(VFS::VFSError::Corrupted);
            }
            slot = (slot + 1) & (m_index_capacity - 1);
        }
        if (m_object_count == m_object_capacity)
        {
            return VFS::VFSResult<bool>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        const auto verified = VerifyCookObject(m_input.Reader, object, extension, m_stream, *m_allocator);
        if (verified.Failed() || !verified.Value())
        {
            return verified;
        }
        auto* entry   = new (m_objects + m_object_count) VerifiedObject{};
        entry->Digest = object.Digest;
        entry->Size   = object.Size;
        (void) Helpers::secure_strcpy(entry->Extension, sizeof(entry->Extension), extension);
        m_index[slot] = m_object_count++;
        return VFS::VFSResult<bool>::Ok(true);
    }

    bool CookIncrementalReuse::PreviousAssetMatches(uint32_t index) const noexcept
    {
        const auto& node     = *m_input.Graph->Node(index);
        const auto* previous = m_input.Previous->FindAsset(node.Identity);
        return previous && previous->Format->Revision == node.AdapterRevision && Core::Containers::StringView(previous->Format->Key) == node.Format && previous->Result.InputKey == m_assets[index].InputKey && previous->Result.Object == m_assets[index].Object;
    }

    bool CookIncrementalReuse::PreviousDependenciesMatch(uint32_t index, uint32_t previous_asset) const noexcept
    {
        const auto edges    = m_input.Graph->Dependencies(index);
        const auto previous = m_input.Previous->Dependencies(previous_asset);
        if (edges.size() != previous.size())
        {
            return false;
        }
        for (size_t i = 0; i < edges.size(); ++i)
        {
            if (m_input.Graph->Slot(edges[i]) != previous[i].Slot || m_input.Graph->Node(edges[i].Child)->Identity != previous[i].Identity || !PreviousAssetMatches(edges[i].Child))
            {
                return false;
            }
        }
        return true;
    }

    VFS::VFSResult<CookReuseDecision> CookIncrementalReuse::PrepareAsset(uint32_t index, CookDiagnostics* diagnostics)
    {
        const auto fail = [&](CookDiagnosticCode code, VFS::VFSError error, cstring message, const CookObjectDescriptor* object = nullptr) { return VFS::VFSResult<CookReuseDecision>::Fail(Failure(diagnostics, index, false, code, error, message, object)); };
        if (!m_block || index >= m_asset_count)
        {
            return fail(CookDiagnosticCode::InvalidRequest, VFS::VFSError::InvalidPath, "Reuse requires an initialized owner and a valid asset index.");
        }
        if (Ready(m_asset_state[index]))
        {
            return VFS::VFSResult<CookReuseDecision>::Ok(CookReuseDecision::Ready);
        }
        for (const auto& edge : m_input.Graph->Dependencies(index))
        {
            if (!Ready(m_asset_state[edge.Child]))
            {
                return fail(CookDiagnosticCode::MissingDependency, VFS::VFSError::NotFound, "Prepare assets in leaf-first order, after verifying or installing every child.");
            }
        }
        Digest256  key   = {};
        const auto keyed = ComputeCookAssetInputKey(*m_allocator, *m_input.Graph, index, m_input.Recipe, AssetResults(), &key);
        if (keyed.Failed())
        {
            return fail(KeyFailureCode(keyed.Error(), CookDiagnosticCode::InvalidAsset), keyed.Error(), "Cannot compute the asset input key.");
        }
        const auto& node     = *m_input.Graph->Node(index);
        const auto* previous = !m_input.ForceRebuild && m_input.Previous ? m_input.Previous->FindAsset(node.Identity) : nullptr;
        if (previous && previous->Format->Revision == node.AdapterRevision && Core::Containers::StringView(previous->Format->Key) == node.Format && previous->Result.InputKey == key && PreviousDependenciesMatch(index, static_cast<uint32_t>(previous - m_input.Previous->Assets().data())))
        {
            const uint64_t limit = node.Identity.Kind == CookAssetKind::Material ? std::min(m_input.Limits->MaxMaterialBytes, m_input.Limits->MaxEncodedAssetBytes) : m_input.Limits->MaxEncodedAssetBytes;
            const auto     shape = CheckObject(previous->Result.Object, previous->Format->Extension, limit);
            if (shape != VFS::VFSError::OK)
            {
                return fail(ObjectFailureCode(shape), shape, "Cached asset descriptor exceeds a limit or is inconsistent.", &previous->Result.Object);
            }
            const auto verified = Verify(previous->Result.Object, previous->Format->Extension);
            if (verified.Failed())
            {
                return fail(ObjectFailureCode(verified.Error()), verified.Error(), "Cannot verify cached asset bytes; existing corrupt objects must not be overwritten.", &previous->Result.Object);
            }
            if (verified.Value())
            {
                m_assets[index]      = previous->Result;
                m_asset_state[index] = State::Reused;
                return VFS::VFSResult<CookReuseDecision>::Ok(CookReuseDecision::Ready);
            }
        }
        m_assets[index].InputKey = key;
        m_asset_state[index]     = State::Keyed;
        return VFS::VFSResult<CookReuseDecision>::Ok(CookReuseDecision::Encode);
    }

    VFS::VFSResult<CookReuseDecision> CookIncrementalReuse::PrepareScene(uint32_t index, const Digest256& source_digest, CookDiagnostics* diagnostics)
    {
        const auto fail = [&](CookDiagnosticCode code, VFS::VFSError error, cstring message, const CookObjectDescriptor* object = nullptr) { return VFS::VFSResult<CookReuseDecision>::Fail(Failure(diagnostics, index, true, code, error, message, object)); };
        if (!m_block || index >= m_scene_count)
        {
            return fail(CookDiagnosticCode::InvalidRequest, VFS::VFSError::InvalidPath, "Reuse requires an initialized owner and a valid scene index.");
        }
        if (m_scene_state[index] != State::Empty && m_scenes[index].SourceDigest != source_digest)
        {
            return fail(CookDiagnosticCode::InputChanged, VFS::VFSError::Corrupted, "A prepared scene cannot be rebound to different captured source bytes.");
        }
        if (Ready(m_scene_state[index]))
        {
            return VFS::VFSResult<CookReuseDecision>::Ok(CookReuseDecision::Ready);
        }
        Digest256                                   key     = {};
        Core::Containers::ArrayView<const uint32_t> closure = {};
        const auto                                  keyed   = ComputeCookSceneInputKey(*m_allocator, *m_input.Graph, index, source_digest, m_input.Recipe, AssetResults(), m_closure, &key, &closure);
        if (keyed.Failed())
        {
            return fail(KeyFailureCode(keyed.Error(), keyed.Error() == VFS::VFSError::NotFound ? CookDiagnosticCode::MissingDependency : CookDiagnosticCode::InvalidScene), keyed.Error(), "Cannot prepare a scene before all assets in its closure are ready.");
        }
        const auto& scene    = *m_input.Graph->Scene(index);
        const auto* previous = !m_input.ForceRebuild && m_input.Previous && m_input.Previous->Recipe().SchemaKey == m_input.Recipe.SchemaKey ? m_input.Previous->FindScene(scene.UUID) : nullptr;
        bool        eligible = previous && previous->Result.SourceDigest == source_digest && previous->Result.InputKey == key;
        if (eligible)
        {
            const auto roots     = m_input.Graph->SceneRoots(index);
            const auto old_roots = m_input.Previous->Roots(static_cast<uint32_t>(previous - m_input.Previous->Scenes().data()));
            eligible             = roots.size() == old_roots.size();
            for (size_t i = 0; eligible && i < roots.size(); ++i)
            {
                eligible = m_input.Graph->Node(roots[i])->Identity == old_roots[i];
            }
            for (uint32_t node : closure)
            {
                if (!eligible)
                {
                    break;
                }
                const auto* asset = m_input.Previous->FindAsset(m_input.Graph->Node(node)->Identity);
                if (!PreviousAssetMatches(node) || !PreviousDependenciesMatch(node, static_cast<uint32_t>(asset - m_input.Previous->Assets().data())))
                {
                    eligible = false;
                    break;
                }
            }
        }
        if (eligible)
        {
            const auto shape = CheckObject(previous->Result.Object, CookSceneObjectExtension, m_input.Limits->SceneLimits.MaxFileBytes);
            if (shape != VFS::VFSError::OK)
            {
                return fail(ObjectFailureCode(shape), shape, "Cached scene descriptor exceeds a limit or is inconsistent.", &previous->Result.Object);
            }
            const auto verified = Verify(previous->Result.Object, CookSceneObjectExtension);
            if (verified.Failed())
            {
                return fail(ObjectFailureCode(verified.Error()), verified.Error(), "Cannot verify cached scene bytes; existing corrupt objects must not be overwritten.", &previous->Result.Object);
            }
            if (verified.Value())
            {
                m_scenes[index]      = previous->Result;
                m_scene_state[index] = State::Reused;
                return VFS::VFSResult<CookReuseDecision>::Ok(CookReuseDecision::Ready);
            }
        }
        m_scenes[index]      = {.SourceDigest = source_digest, .InputKey = key};
        m_scene_state[index] = State::Keyed;
        return VFS::VFSResult<CookReuseDecision>::Ok(CookReuseDecision::Encode);
    }

    VFS::VFSResult<void> CookIncrementalReuse::AcceptInstalled(uint32_t index, bool scene, const CookObjectDescriptor& object, CookDiagnostics* diagnostics)
    {
        const auto fail = [&](CookDiagnosticCode code, VFS::VFSError error, cstring message) { return VFS::VFSResult<void>::Fail(Failure(diagnostics, index, scene, code, error, message, &object)); };
        if (!m_block || index >= (scene ? m_scene_count : m_asset_count))
        {
            return fail(CookDiagnosticCode::InvalidRequest, VFS::VFSError::InvalidPath, "Installing an object requires an initialized owner and a valid index.");
        }
        auto& state  = scene ? m_scene_state[index] : m_asset_state[index];
        auto& result = scene ? m_scenes[index].Object : m_assets[index].Object;
        if (Ready(state))
        {
            return object == result ? VFS::VFSResult<void>::Ok() : fail(CookDiagnosticCode::InvalidRequest, VFS::VFSError::InvalidPath, "A ready result cannot be replaced by a different object.");
        }
        if (state != State::Keyed)
        {
            return fail(CookDiagnosticCode::InvalidRequest, VFS::VFSError::InvalidPath, "Prepare the input key before accepting an installed object.");
        }
        const auto*    node      = scene ? nullptr : m_input.Graph->Node(index);
        const auto*    format    = node ? FindCookAssetFormat(node->Format) : nullptr;
        const cstring  extension = scene ? CookSceneObjectExtension : format->Extension;
        const uint64_t limit     = scene ? m_input.Limits->SceneLimits.MaxFileBytes : node->Identity.Kind == CookAssetKind::Material ? std::min(m_input.Limits->MaxEncodedAssetBytes, m_input.Limits->MaxMaterialBytes) : m_input.Limits->MaxEncodedAssetBytes;
        const auto     shape     = CheckObject(object, extension, limit);
        if (shape != VFS::VFSError::OK)
        {
            return fail(ObjectFailureCode(shape), shape, "Installed object descriptor exceeds a limit or is inconsistent.");
        }
        const auto verified = Verify(object, extension);
        if (verified.Failed() || !verified.Value())
        {
            const auto error = verified.Failed() ? verified.Error() : VFS::VFSError::NotFound;
            return fail(ObjectFailureCode(error), error, "An installed object must exist and pass size/digest verification before it can become ready.");
        }
        result = object;
        state  = State::Encoded;
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookIncrementalReuse::AcceptInstalledAsset(uint32_t index, const CookObjectDescriptor& object, CookDiagnostics* diagnostics)
    {
        return AcceptInstalled(index, false, object, diagnostics);
    }

    VFS::VFSResult<void> CookIncrementalReuse::AcceptInstalledScene(uint32_t index, const CookObjectDescriptor& object, CookDiagnostics* diagnostics)
    {
        return AcceptInstalled(index, true, object, diagnostics);
    }

    Core::Containers::ArrayView<const CookAssetResult> CookIncrementalReuse::AssetResults() const noexcept
    {
        return {m_assets, m_asset_count};
    }

    Core::Containers::ArrayView<const CookSceneResult> CookIncrementalReuse::SceneResults() const noexcept
    {
        return {m_scenes, m_scene_count};
    }

    bool CookIncrementalReuse::IsAssetReady(uint32_t index) const noexcept
    {
        return index < m_asset_count && Ready(m_asset_state[index]);
    }

    bool CookIncrementalReuse::IsSceneReady(uint32_t index) const noexcept
    {
        return index < m_scene_count && Ready(m_scene_state[index]);
    }

    bool CookIncrementalReuse::WasAssetReused(uint32_t index) const noexcept
    {
        return index < m_asset_count && m_asset_state[index] == State::Reused;
    }

    bool CookIncrementalReuse::WasSceneReused(uint32_t index) const noexcept
    {
        return index < m_scene_count && m_scene_state[index] == State::Reused;
    }

    bool CookIncrementalReuse::AllReady() const noexcept
    {
        if (!m_block)
        {
            return false;
        }
        for (uint32_t i = 0; i < m_asset_count; ++i)
        {
            if (!Ready(m_asset_state[i]))
            {
                return false;
            }
        }
        for (uint32_t i = 0; i < m_scene_count; ++i)
        {
            if (!Ready(m_scene_state[i]))
            {
                return false;
            }
        }
        return true;
    }
} // namespace ZEngine::Cook
