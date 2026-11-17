#include <ZEngine/ECS/CookedSceneReconstruction.h>
#include <ZEngine/Managers/CookedPackageSnapshot.h>
#include <utility>

namespace ZEngine::ECS
{
    CookedSceneReconstructionError ReconstructCookedScene(Managers::CookedPackageSnapshot& package, SceneStorageLease&& storage, const SceneComponentSchemaRegistry& schemas, Core::Memory::IAllocationRoute& scene_object_route, size_t max_scene_object_bytes, size_t document_working_bytes, Managers::CookedCpuClosureCache& cache, CookedSceneReconstruction* output, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits, const std::atomic<bool>* cancelled)
    {
        const auto is_cancelled = [&] { return cancelled && cancelled->load(std::memory_order_acquire); };
        if (!output || output->Instance || output->Assets || !package.IsValid() || package.Schemas() != &schemas || !max_scene_object_bytes || !document_working_bytes || !storage.Arena())
        {
            return CookedSceneReconstructionError::InvalidInput;
        }
        if (is_cancelled())
        {
            return CookedSceneReconstructionError::Cancelled;
        }
        Managers::CookedObjectSnapshot scene   = {};
        const auto                     capture = package.CaptureScene(scene_object_route, max_scene_object_bytes, &scene, cancelled);
        if (capture.Failed())
        {
            return capture.Error() == Core::VFS::VFSError::Cancelled ? CookedSceneReconstructionError::Cancelled : CookedSceneReconstructionError::SceneObject;
        }
        CookedSceneReconstruction candidate = {};
        if (!SceneInstance::Create(std::move(storage), &candidate.Instance, diagnostics))
        {
            return CookedSceneReconstructionError::ScenePopulation;
        }
        if (!candidate.Instance->ReadVerifiedCooked(package, scene, schemas, document_working_bytes, diagnostics, limits, cancelled))
        {
            return is_cancelled() ? CookedSceneReconstructionError::Cancelled : CookedSceneReconstructionError::SceneDecode;
        }
        scene.Reset();
        if (is_cancelled())
        {
            return CookedSceneReconstructionError::Cancelled;
        }
        if (!candidate.Instance->PopulateCooked(schemas, diagnostics))
        {
            return CookedSceneReconstructionError::ScenePopulation;
        }
        if (is_cancelled())
        {
            return CookedSceneReconstructionError::Cancelled;
        }
        if (!cache.Request(std::move(package), &candidate.Assets))
        {
            return CookedSceneReconstructionError::AssetAdmission;
        }
        *output = std::move(candidate);
        return CookedSceneReconstructionError::None;
    }
} // namespace ZEngine::ECS
