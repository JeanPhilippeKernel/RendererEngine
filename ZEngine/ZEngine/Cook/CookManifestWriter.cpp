#include <ZEngine/Cook/CookManifestInternal.h>
#include <ZEngine/Core/Serialization/CanonicalJSONWriter.h>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        using JSONWriter = Core::Serialization::CanonicalJSONWriter;

        bool WriteIdentity(JSONWriter& writer, const CookAssetIdentity& identity)
        {
            return writer.Raw("{\"kind\":") && writer.String(CookAssetKindKey(identity.Kind)) && writer.Raw(",\"uuid\":") && writer.UUID(identity.UUID) && writer.Raw("}");
        }

        bool WriteObject(JSONWriter& writer, const CookObjectDescriptor& object)
        {
            return writer.Raw("{\"digest\":") && writer.HexString(object.Digest.Bytes) && writer.Raw(",\"path\":") && writer.String(object.Path.View()) && writer.Raw(",\"size\":") && writer.UInt64(object.Size) && writer.Raw("}");
        }

        const CookObjectDescriptor& ObjectAt(const CookManifestInput& input, uint32_t index)
        {
            return index < input.Assets.size() ? input.Assets[index].Object : input.Scenes[index - input.Assets.size()].Object;
        }

        VFS::VFSResult<void> Validate(const CookManifestInput& input, Core::Containers::ArrayView<uint32_t> workspace)
        {
            if (!input.Graph || !input.Limits || (workspace.size() && !workspace.data()))
                return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
            const auto& graph  = *input.Graph;
            const auto& limits = *input.Limits;
            if (ValidateCookLimits(limits).Failed() || !graph.IsFinalized() || input.Assets.size() != graph.NodeCount() || input.Scenes.size() != graph.SceneCount() || (input.Assets.size() && !input.Assets.data()) || (input.Scenes.size() && !input.Scenes.data()))
                return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
            if (!graph.SceneCount() || graph.SceneCount() > limits.MaxScenes || graph.NodeCount() > limits.MaxAssets || graph.EdgeCount() > limits.MaxEdges || graph.ReferenceCount() > limits.MaxReferenceOccurrences)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
            if (input.Recipe.Revision != CookRecipeRevision || !CookTargetKey(input.Recipe.Target))
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
            Digest256  settings_key = {};
            const auto settings     = ComputeCookSettingsKey(input.Recipe.Target, &settings_key);
            if (settings.Failed())
                return settings;
            if (input.Recipe.SettingsKey != settings_key)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            const size_t objects = input.Assets.size() + input.Scenes.size();
            if (workspace.size() < objects)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
            for (uint32_t i = 0; i < graph.NodeCount(); ++i)
            {
                const auto& node   = *graph.Node(i);
                const auto* format = FindCookAssetFormat(node.Format);
                const auto& object = input.Assets[i].Object;
                if (!format || format->Kind != node.Identity.Kind || format->Revision != node.AdapterRevision)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
                if (!IsCookObjectDescriptorValid(*format, object.Path, object.Digest, object.Size))
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                if (object.Size > limits.MaxEncodedAssetBytes || (node.Identity.Kind == CookAssetKind::Material && object.Size > limits.MaxMaterialBytes))
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                for (const auto& edge : graph.Dependencies(i))
                    if (!IsCookDependencySlotValid(node.Identity.Kind, graph.Node(edge.Child)->Identity.Kind, graph.Slot(edge)))
                        return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            }
            for (const auto& scene : input.Scenes)
            {
                if (!IsCookObjectDescriptorValid(scene.Object, CookSceneObjectExtension))
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                if (scene.Object.Size > limits.SceneLimits.MaxFileBytes)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
            }
            return VFS::VFSResult<void>::Ok();
        }

        VFS::VFSResult<void> Prepare(const CookManifestInput& input, Core::Containers::ArrayView<uint32_t> workspace, Detail::CookSortState<uint32_t>& sort)
        {
            const auto& graph       = *input.Graph;
            const auto  descriptors = Detail::ValidateManifestObjectSizes(static_cast<uint32_t>(input.Assets.size() + input.Scenes.size()), workspace, sort, [&](uint32_t index) -> const CookObjectDescriptor& { return ObjectAt(input, index); });
            if (descriptors.Failed())
                return descriptors;
            for (uint32_t i = 0; i < graph.SceneCount(); ++i)
                workspace[i] = i;
            Core::Containers::HeapSort<uint32_t>({workspace.data(), graph.SceneCount()}, sort, [&](uint32_t left, uint32_t right) { return graph.Scene(left)->UUID < graph.Scene(right)->UUID; });
            return VFS::VFSResult<void>::Ok();
        }

        bool Emit(JSONWriter& writer, const CookManifestInput& input, Core::Containers::ArrayView<uint32_t> order)
        {
            const auto& graph = *input.Graph;
            if (!writer.Raw("{\"assets\":["))
                return false;
            bool comma = false;
            for (uint32_t index : graph.CanonicalNodes())
            {
                const auto& node   = *graph.Node(index);
                const auto& result = input.Assets[index];
                if ((comma && !writer.Raw(",")) || !writer.Raw("{\"adapter_revision\":") || !writer.UInt64(node.AdapterRevision) || !writer.Raw(",\"dependencies\":["))
                    return false;
                comma                 = true;
                bool dependency_comma = false;
                for (const auto& edge : graph.Dependencies(index))
                {
                    const auto& child = graph.Node(edge.Child)->Identity;
                    if ((dependency_comma && !writer.Raw(",")) || !writer.Raw("{\"kind\":") || !writer.String(CookAssetKindKey(child.Kind)) || !writer.Raw(",\"slot\":") || !writer.String(graph.Slot(edge)) || !writer.Raw(",\"uuid\":") || !writer.UUID(child.UUID) || !writer.Raw("}"))
                        return false;
                    dependency_comma = true;
                }
                if (!writer.Raw("],\"format\":") || !writer.String(node.Format) || !writer.Raw(",\"input_key\":") || !writer.HexString(result.InputKey.Bytes) || !writer.Raw(",\"kind\":") || !writer.String(CookAssetKindKey(node.Identity.Kind)) || !writer.Raw(",\"object\":") || !WriteObject(writer, result.Object) || !writer.Raw(",\"uuid\":") || !writer.UUID(node.Identity.UUID) || !writer.Raw("}"))
                    return false;
            }
            if (!writer.Raw("],\"manifest_version\":") || !writer.UInt64(CookManifestVersion) || !writer.Raw(",\"recipe_revision\":") || !writer.UInt64(input.Recipe.Revision) || !writer.Raw(",\"scenes\":["))
                return false;
            for (uint32_t i = 0; i < graph.SceneCount(); ++i)
            {
                const auto  index  = order[i];
                const auto& scene  = *graph.Scene(index);
                const auto& result = input.Scenes[index];
                if ((i && !writer.Raw(",")) || !writer.Raw("{\"input_key\":") || !writer.HexString(result.InputKey.Bytes) || !writer.Raw(",\"object\":") || !WriteObject(writer, result.Object) || !writer.Raw(",\"roots\":["))
                    return false;
                bool root_comma = false;
                for (uint32_t root : graph.SceneRoots(index))
                {
                    if ((root_comma && !writer.Raw(",")) || !WriteIdentity(writer, graph.Node(root)->Identity))
                        return false;
                    root_comma = true;
                }
                if (!writer.Raw("],\"source\":") || !writer.String(scene.Source.View()) || !writer.Raw(",\"source_digest\":") || !writer.HexString(result.SourceDigest.Bytes) || !writer.Raw(",\"uuid\":") || !writer.UUID(scene.UUID) || !writer.Raw("}"))
                    return false;
            }
            return writer.Raw("],\"schema_key\":") && writer.HexString(input.Recipe.SchemaKey.Bytes) && writer.Raw(",\"settings\":{},\"settings_key\":") && writer.HexString(input.Recipe.SettingsKey.Bytes) && writer.Raw(",\"target\":") && writer.String(CookTargetKey(input.Recipe.Target)) && writer.Raw("}\n");
        }

        struct ManifestStream
        {
            ICookByteSink*              Sink  = nullptr;
            Core::Hash::SHA256*         Hash  = nullptr;
            uint64_t                    Limit = 0;
            uint64_t                    Bytes = 0;

            static VFS::VFSResult<void> Write(void* context, Core::Containers::ArrayView<const uint8_t> bytes)
            {
                auto& stream = *static_cast<ManifestStream*>(context);
                if (bytes.size() > stream.Limit - stream.Bytes)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                if (stream.Sink)
                {
                    const auto written = stream.Sink->Write(bytes);
                    if (written.Failed())
                        return written;
                    const auto hashed = stream.Hash->Update(bytes);
                    if (hashed.Failed())
                        return hashed;
                }
                stream.Bytes += bytes.size();
                return VFS::VFSResult<void>::Ok();
            }
        };
    } // namespace

    VFS::VFSResult<void> WriteCookManifest(const CookManifestInput& input, Core::Containers::ArrayView<uint32_t> workspace, ICookByteSink& sink, CookManifestOutput* output, ICookAllocator& allocator)
    {
        if (!output)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        const auto valid = Validate(input, workspace);
        if (valid.Failed())
            return valid;
        CookMemoryCharge fixed   = {};
        const auto       charged = allocator.TryCharge(sizeof(Detail::CookSortState<uint32_t>) + sizeof(ManifestStream) + sizeof(JSONWriter) + Core::Hash::SHA256::FixedWorkingBytes() + sizeof(CookManifestOutput) + sizeof(fixed), CookMemoryCategory::StagingManifest, &fixed);
        if (charged.Failed())
            return charged;
        Detail::CookSortState<uint32_t> sort     = {};
        const auto                      prepared = Prepare(input, workspace, sort);
        if (prepared.Failed())
            return prepared;
        // Reuse one stream/writer for both passes. A successful counter has no
        // sticky error to clear, and measurement never constructs an unused hash.
        ManifestStream stream = {.Limit = input.Limits->MaxManifestBytes};
        JSONWriter     writer(ManifestStream::Write, &stream);
        if (!Emit(writer, input, workspace))
            return VFS::VFSResult<void>::Fail(writer.Error());
        Core::Hash::SHA256 hash = {};
        stream.Sink             = &sink;
        stream.Hash             = &hash;
        stream.Limit            = stream.Bytes;
        stream.Bytes            = 0;
        if (!Emit(writer, input, workspace))
            return VFS::VFSResult<void>::Fail(writer.Error());
        CookManifestOutput candidate = {.Bytes = stream.Bytes};
        const auto         hashed    = hash.Finalize(&candidate.Digest);
        if (hashed.Failed())
            return hashed;
        *output = candidate;
        return VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Cook
