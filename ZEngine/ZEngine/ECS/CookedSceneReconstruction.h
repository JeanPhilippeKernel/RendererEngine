#pragma once
#include <ZEngine/ECS/SceneInstance.h>
#include <ZEngine/Managers/CookedCpuClosureCache.h>

namespace ZEngine::ECS
{
    /// Exclusive worker-owned unpublished result. The closure lease can be
    /// pending; its owner must pump the cache and observe Ready before using
    /// the assets. The cache and all its documented dependencies outlive it.
    struct CookedSceneReconstruction final
    {
        std::unique_ptr<SceneInstance>         Instance = {};
        Managers::CookedCpuClosureCache::Lease Assets   = {};
    };

    enum class CookedSceneReconstructionError : uint8_t
    {
        None,
        InvalidInput,
        SceneObject,
        SceneDecode,
        ScenePopulation,
        AssetAdmission,
        Cancelled,
    };

    /// Worker transaction. Scene bytes use the supplied bounded route and are
    /// released before return; the document uses the admitted scene slot. The
    /// package is moved into the cache on a new closure and remains valid on a
    /// cache hit. Failure preserves output and package, but may consume and
    /// release storage after SceneInstance::Create. No active scene is changed.
    [[nodiscard]] CookedSceneReconstructionError ReconstructCookedScene(Managers::CookedPackageSnapshot& package, SceneStorageLease&& storage, const SceneComponentSchemaRegistry& schemas, Core::Memory::IAllocationRoute& scene_object_route, size_t max_scene_object_bytes, size_t document_working_bytes, Managers::CookedCpuClosureCache& cache, CookedSceneReconstruction* output, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits = {}, const std::atomic<bool>* cancelled = nullptr);
} // namespace ZEngine::ECS
