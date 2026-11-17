#pragma once
#include <ZEngine/Managers/CookedCpuClosureCache.h>
#include <ZEngine/Managers/CookedPackageSnapshot.h>
#include <ZEngine/Rendering/Scenes/RuntimeScene.h>

namespace ZEngine::Applications
{
    /// @brief Failure stage for reconstructing a runtime scene from cooked data.
    enum class CookedRuntimeReconstructionError : uint8_t
    {
        None,
        InvalidInput,
        Cancelled,
        Package,
        Reconstruction,
        Assets,
        Binding
    };

    /// @brief Reconstruct and prepare a runtime scene from a cooked package.
    /// @details This exclusive worker operation consumes the root and scene slot
    /// as it progresses. Success transfers a candidate and closure lease; failure
    /// preserves both outputs. The allocator, schemas, cache and VFS provider
    /// must outlive the work and returned lease.
    [[nodiscard]] CookedRuntimeReconstructionError ReconstructCookedRuntimeScene(
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
        const std::atomic<bool>*                          cancelled = nullptr);
} // namespace ZEngine::Applications
