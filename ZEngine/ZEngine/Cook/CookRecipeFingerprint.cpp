#include <ZEngine/Cook/CookDependencyGraph.h>
#include <ZEngine/Cook/CookFingerprint.h>
#include <ZEngine/Cook/CookMemory.h>
#include <ZEngine/Cook/CookSort.h>
#include <ZEngine/ECS/BinarySceneCodec.h>
#include <algorithm>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        struct SchemaHashContext
        {
            CookFingerprint*                      Hash  = nullptr;
            Core::Containers::ArrayView<uint32_t> Order = {};
            Detail::CookSortState<uint32_t>       Sort  = {};
        };

        VFS::VFSResult<void> ValidateSchemaWorkspace(const ECS::SceneComponentSchemaRegistry& schemas, Core::Containers::ArrayView<uint32_t> workspace)
        {
            if (workspace.size() && !workspace.data())
                return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
            const auto required = CookSchemaKeyWorkspaceWords(schemas);
            if (required.Failed())
                return VFS::VFSResult<void>::Fail(required.Error());
            return workspace.size() < required.Value() ? VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded) : VFS::VFSResult<void>::Ok();
        }

        // The owner reserves both this callback context and its borrowed hash.
        VFS::VFSResult<void> SchemaKey(const ECS::SceneComponentSchemaRegistry& schemas, Core::Containers::ArrayView<uint32_t> workspace, CookFingerprint& hash, Digest256* output)
        {
            if (hash.String("ze.cook.schema.v1").Failed() || hash.UInt32(ECS::SceneDocument::SchemaVersion).Failed() || hash.UInt32(ECS::SceneBinaryFormat::Version).Failed() || hash.UInt32(schemas.Count()).Failed())
                return VFS::VFSResult<void>::Fail(hash.Error());
            SchemaHashContext context = {&hash, workspace};
            schemas.ForEachCanonical(
                [](void* state, const ECS::SceneComponentSchema& schema) {
                    auto& ctx         = *static_cast<SchemaHashContext*>(state);
                    auto& fingerprint = *ctx.Hash;
                    if (fingerprint.Error() != VFS::VFSError::OK || fingerprint.String(schema.Key).Failed() || fingerprint.UInt32(schema.Version).Failed() || fingerprint.UInt32(schema.FieldCount).Failed())
                        return;
                    for (uint32_t i = 0; i < schema.FieldCount; ++i)
                        ctx.Order[i] = i;
                    Detail::HeapSort<uint32_t>({ctx.Order.data(), schema.FieldCount}, ctx.Sort, [&](uint32_t left, uint32_t right) { return Helpers::secure_strcmp(schema.Fields[left].Key, schema.Fields[right].Key) < 0; });
                    for (uint32_t i = 0; i < schema.FieldCount; ++i)
                    {
                        const auto& field = schema.Fields[ctx.Order[i]];
                        if (fingerprint.String(field.Key).Failed() || fingerprint.UInt32(static_cast<uint32_t>(field.Class)).Failed())
                            return;
                    }
                },
                &context);
            return hash.Finalize(output);
        }

        VFS::VFSResult<void> ValidateRecipe(const CookRecipeKeys& recipe)
        {
            if (!recipe.Revision)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
            return CookTargetKey(recipe.Target) ? VFS::VFSResult<void>::Ok() : VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
        }

        VFS::VFSResult<void> ValidateResultTable(const CookDependencyGraph& graph, Core::Containers::ArrayView<const CookAssetResult> results)
        {
            return graph.IsFinalized() && results.size() == graph.NodeCount() && (!results.size() || results.data()) ? VFS::VFSResult<void>::Ok() : VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }

        VFS::VFSResult<void> ValidateNodeResult(const CookGraphNode& node, const CookAssetResult& result)
        {
            const auto* format = FindCookAssetFormat(node.Format);
            if (!format || format->Kind != node.Identity.Kind || format->Revision != node.AdapterRevision)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
            if (!result.Object.Size)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::NotFound);
            return IsCookObjectDescriptorValid(*format, result.Object.Path, result.Object.Digest, result.Object.Size) ? VFS::VFSResult<void>::Ok() : VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }

        VFS::VFSResult<void> GraphKey(const CookDependencyGraph& graph, Core::Containers::ArrayView<const CookAssetResult> results, Core::Containers::ArrayView<const uint32_t> canonical_nodes, CookFingerprint& hash, Digest256* output)
        {
            if (hash.String("ze.cook.graph.v1").Failed() || hash.UInt32(static_cast<uint32_t>(canonical_nodes.size())).Failed())
                return VFS::VFSResult<void>::Fail(hash.Error());
            for (uint32_t index : canonical_nodes)
            {
                const auto& node   = *graph.Node(index);
                const auto& result = results[index];
                const auto  valid  = ValidateNodeResult(node, result);
                if (valid.Failed())
                    return valid;
                if (hash.String(CookAssetKindKey(node.Identity.Kind)).Failed() || hash.UUID(node.Identity.UUID).Failed() || hash.Digest(result.InputKey).Failed() || hash.Digest(result.Object.Digest).Failed() || hash.UInt64(result.Object.Size).Failed() || hash.String({result.Object.Path.CStr(), result.Object.Path.Length()}).Failed())
                    return VFS::VFSResult<void>::Fail(hash.Error());
            }
            return hash.Finalize(output);
        }
    } // namespace

    Core::Containers::StringView CookSettingsJSON() noexcept
    {
        return {"{}\n", 3};
    }

    VFS::VFSResult<void> ComputeCookSettingsKey(CookTarget target, Digest256* output)
    {
        if (!output)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        if (!CookTargetKey(target))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
        // Recipe v1 supports only {} followed by LF, on every supported target.
        // Kept in lockstep with CookSettingsJSON by the independent hash golden.
        static constexpr Digest256 settings = {
            {0xca, 0x3d, 0x16, 0x3b, 0xab, 0x05, 0x53, 0x81, 0x82, 0x72, 0x26, 0x14, 0x05, 0x68, 0xf3, 0xbe, 0xf7, 0xea, 0xac, 0x18, 0x7c, 0xeb, 0xd7, 0x68, 0x78, 0xe0, 0xb6, 0x3e, 0x9e, 0x44, 0x23, 0x56}
        };
        *output = settings;
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<size_t> CookSchemaKeyWorkspaceWords(const ECS::SceneComponentSchemaRegistry& schemas)
    {
        if (!schemas.IsInitialized())
            return VFS::VFSResult<size_t>::Fail(VFS::VFSError::InvalidPath);
        size_t words = 0;
        schemas.ForEachCanonical(
            [](void* context, const ECS::SceneComponentSchema& schema) {
                auto& count = *static_cast<size_t*>(context);
                count       = std::max(count, static_cast<size_t>(schema.FieldCount));
            },
            &words);
        return VFS::VFSResult<size_t>::Ok(words);
    }

    VFS::VFSResult<void> ComputeCookSchemaKey(ICookAllocator& allocator, const ECS::SceneComponentSchemaRegistry& schemas, Core::Containers::ArrayView<uint32_t> workspace, Digest256* output)
    {
        if (!output)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        const auto valid = ValidateSchemaWorkspace(schemas, workspace);
        if (valid.Failed())
            return valid;
        CookMemoryCharge fixed   = {};
        const auto       charged = allocator.TryCharge(CookFingerprint::FixedWorkingBytes() + sizeof(SchemaHashContext) + sizeof(fixed), CookMemoryCategory::CatalogGraph, &fixed);
        if (charged.Failed())
            return charged;
        CookFingerprint hash = {};
        return SchemaKey(schemas, workspace, hash, output);
    }

    VFS::VFSResult<void> ComputeCookRecipeKeys(ICookAllocator& allocator, const ECS::SceneComponentSchemaRegistry& schemas, CookTarget target, Core::Containers::ArrayView<uint32_t> workspace, CookRecipeKeys* output)
    {
        if (!output)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        if (!CookTargetKey(target))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
        const auto valid = ValidateSchemaWorkspace(schemas, workspace);
        if (valid.Failed())
            return valid;
        CookMemoryCharge fixed   = {};
        const auto       charged = allocator.TryCharge(sizeof(CookRecipeKeys) + CookFingerprint::FixedWorkingBytes() + sizeof(SchemaHashContext) + sizeof(fixed), CookMemoryCategory::CatalogGraph, &fixed);
        if (charged.Failed())
            return charged;
        CookRecipeKeys result   = {.Target = target};
        const auto     settings = ComputeCookSettingsKey(target, &result.SettingsKey);
        if (settings.Failed())
            return settings;
        CookFingerprint hash   = {};
        const auto      schema = SchemaKey(schemas, workspace, hash, &result.SchemaKey);
        if (schema.Failed())
            return schema;
        *output = result;
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> ComputeCookAssetInputKey(ICookAllocator& allocator, const CookDependencyGraph& graph, uint32_t index, const CookRecipeKeys& recipe, Core::Containers::ArrayView<const CookAssetResult> results, Digest256* output)
    {
        if (!output || !graph.Node(index))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        const auto shape = ValidateResultTable(graph, results);
        if (shape.Failed())
            return shape;
        const auto valid_recipe = ValidateRecipe(recipe);
        if (valid_recipe.Failed())
            return valid_recipe;
        CookMemoryCharge fixed   = {};
        const auto       charged = allocator.TryCharge(CookFingerprint::FixedWorkingBytes() + sizeof(fixed), CookMemoryCategory::CatalogGraph, &fixed);
        if (charged.Failed())
            return charged;
        const auto&     node         = *graph.Node(index);
        const auto      dependencies = graph.Dependencies(index);
        CookFingerprint hash         = {};
        if (hash.String("ze.cook.asset.v1").Failed() || hash.String(CookAssetKindKey(node.Identity.Kind)).Failed() || hash.UUID(node.Identity.UUID).Failed() || hash.Digest(node.LocalContentKey).Failed() || hash.Digest(node.MetadataKey).Failed() || hash.String(node.Format).Failed() || hash.UInt32(node.AdapterRevision).Failed() || hash.UInt32(recipe.Revision).Failed() || hash.String(CookTargetKey(recipe.Target)).Failed() || hash.Digest(recipe.SettingsKey).Failed() || hash.UInt32(static_cast<uint32_t>(dependencies.size())).Failed())
            return VFS::VFSResult<void>::Fail(hash.Error());
        for (const auto& edge : dependencies)
        {
            const auto& child  = *graph.Node(edge.Child);
            const auto& result = results[edge.Child];
            const auto  valid  = ValidateNodeResult(child, result);
            if (valid.Failed())
                return valid;
            if (hash.String(graph.Slot(edge)).Failed() || hash.String(CookAssetKindKey(child.Identity.Kind)).Failed() || hash.UUID(child.Identity.UUID).Failed() || hash.Digest(result.InputKey).Failed() || hash.Digest(result.Object.Digest).Failed() || hash.UInt64(result.Object.Size).Failed() || hash.String({result.Object.Path.CStr(), result.Object.Path.Length()}).Failed() || hash.String(child.Format).Failed())
                return VFS::VFSResult<void>::Fail(hash.Error());
        }
        return hash.Finalize(output);
    }

    VFS::VFSResult<void> ComputeCookGraphKey(ICookAllocator& allocator, const CookDependencyGraph& graph, Core::Containers::ArrayView<const CookAssetResult> results, Digest256* output)
    {
        if (!output)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        const auto shape = ValidateResultTable(graph, results);
        if (shape.Failed())
            return shape;
        CookMemoryCharge fixed   = {};
        const auto       charged = allocator.TryCharge(CookFingerprint::FixedWorkingBytes() + sizeof(fixed), CookMemoryCategory::CatalogGraph, &fixed);
        if (charged.Failed())
            return charged;
        CookFingerprint hash = {};
        return GraphKey(graph, results, graph.CanonicalNodes(), hash, output);
    }

    VFS::VFSResult<void> ComputeCookSceneInputKey(ICookAllocator& allocator, const CookDependencyGraph& graph, uint32_t scene, const Digest256& source_digest, const CookRecipeKeys& recipe, Core::Containers::ArrayView<const CookAssetResult> results, Core::Containers::ArrayView<uint32_t> workspace, Digest256* output, Core::Containers::ArrayView<const uint32_t>* output_closure)
    {
        if (!output)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        const auto shape = ValidateResultTable(graph, results);
        if (shape.Failed())
            return shape;
        const auto valid_recipe = ValidateRecipe(recipe);
        if (valid_recipe.Failed())
            return valid_recipe;
        if (!graph.Scene(scene) || (workspace.size() && !workspace.data()))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        if (workspace.size() < uint64_t(graph.NodeCount()) * 2)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        CookMemoryCharge fixed   = {};
        const auto       charged = allocator.TryCharge(CookFingerprint::FixedWorkingBytes() + sizeof(Digest256) + sizeof(Core::Containers::ArrayView<const uint32_t>) + sizeof(fixed), CookMemoryCategory::CatalogGraph, &fixed);
        if (charged.Failed())
            return charged;
        Core::Containers::ArrayView<const uint32_t> closure   = {};
        const auto                                  collected = graph.SceneClosure(scene, workspace, &closure);
        if (collected.Failed())
            return collected;
        Digest256       graph_key = {};
        CookFingerprint hash      = {};
        const auto      hashed    = GraphKey(graph, results, closure, hash, &graph_key);
        if (hashed.Failed())
            return hashed;
        hash.Reset();
        if (hash.String("ze.cook.scene.v1").Failed() || hash.Digest(source_digest).Failed() || hash.Digest(recipe.SchemaKey).Failed() || hash.UInt32(recipe.Revision).Failed() || hash.Digest(graph_key).Failed() || hash.String(CookTargetKey(recipe.Target)).Failed() || hash.Digest(recipe.SettingsKey).Failed())
            return VFS::VFSResult<void>::Fail(hash.Error());
        const auto finalized = hash.Finalize(output);
        if (finalized.Succeeded() && output_closure)
            *output_closure = closure;
        return finalized;
    }
} // namespace ZEngine::Cook
