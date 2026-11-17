#include <ZEngine/Applications/CookedRuntimeReconstruction.h>
#include <ZEngine/ECS/CookedSceneReconstruction.h>
#include <ZEngine/Profiling/Profiling.h>

namespace ZEngine::Applications
{
    CookedRuntimeReconstructionError ReconstructCookedRuntimeScene(
        Core::VFS::VFSReadRootLease&&                     root,
        const uuids::uuid&                                scene,
        ECS::SceneStorageLease&&                          storage,
        const ECS::SceneComponentSchemaRegistry&          schemas,
        const Cook::CookLimits&                           limits,
        Cook::ICookAllocator&                             allocator,
        Core::Memory::IAllocationRoute&                   route,
        size_t                                            max_scene_bytes,
        size_t                                            document_working_bytes,
        Managers::CookedCpuClosureCache&                  cache,
        std::unique_ptr<Rendering::Scenes::RuntimeScene>* output,
        Managers::CookedCpuClosureCache::Lease*           output_assets,
        Cook::CookDiagnostics*                            cook_diagnostics,
        ECS::SceneDiagnostics*                            scene_diagnostics,
        const std::atomic<bool>*                          cancelled)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Worker.RuntimeReconstruction");
        const auto is_cancelled = [&] { return cancelled && cancelled->load(std::memory_order_acquire); };
        if (!root.IsValid() || scene.is_nil() || !storage.Arena() || !output || *output || !output_assets || *output_assets || !max_scene_bytes || !document_working_bytes)
            return CookedRuntimeReconstructionError::InvalidInput;
        if (is_cancelled())
            return CookedRuntimeReconstructionError::Cancelled;
        Managers::CookedPackageSnapshot package  = {};
        const auto                      captured = Managers::CookedPackageSnapshot::Capture(std::move(root), scene, schemas, limits, allocator, cook_diagnostics, &package, cancelled);
        if (captured.Failed())
            return is_cancelled() ? CookedRuntimeReconstructionError::Cancelled : CookedRuntimeReconstructionError::Package;
        ECS::CookedSceneReconstruction reconstruction = {};
        const auto                     reconstructed  = ECS::ReconstructCookedScene(package, std::move(storage), schemas, route, max_scene_bytes, document_working_bytes, cache, &reconstruction, scene_diagnostics, {}, cancelled);
        if (reconstructed != ECS::CookedSceneReconstructionError::None)
            return is_cancelled() ? CookedRuntimeReconstructionError::Cancelled : CookedRuntimeReconstructionError::Reconstruction;
        for (;;)
        {
            if (is_cancelled())
            {
                (void) cache.CancelUnsharedBuild(reconstruction.Assets.CancellationIdentity());
                return CookedRuntimeReconstructionError::Cancelled;
            }
            Managers::CookedCpuClosureCache::Result assets = {};
            if (!reconstruction.Assets.Poll(&assets))
                return CookedRuntimeReconstructionError::Assets;
            if (assets.StateValue == Managers::CookedCpuClosureCache::State::Failed)
                return CookedRuntimeReconstructionError::Assets;
            if (assets.StateValue == Managers::CookedCpuClosureCache::State::Ready)
            {
                std::unique_ptr<Rendering::Scenes::RuntimeScene> candidate = {};
                if (!assets.Assets || !Rendering::Scenes::RuntimeScene::PrepareFromCooked(std::move(reconstruction.Instance), *assets.Assets, &candidate, scene_diagnostics))
                    return CookedRuntimeReconstructionError::Binding;
                if (is_cancelled())
                    return CookedRuntimeReconstructionError::Cancelled;
                *output        = std::move(candidate);
                *output_assets = std::move(reconstruction.Assets);
                return CookedRuntimeReconstructionError::None;
            }
            // This operation owns the cache worker. No synchronous fallback or
            // unbounded wait is allowed when another worker has the pump.
            if (!cache.PumpOne(cancelled))
                return is_cancelled() ? CookedRuntimeReconstructionError::Cancelled : CookedRuntimeReconstructionError::Assets;
        }
    }
} // namespace ZEngine::Applications
