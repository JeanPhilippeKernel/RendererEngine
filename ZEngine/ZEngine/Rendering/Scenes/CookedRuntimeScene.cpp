#include <ZEngine/Managers/CookedCpuAssetSet.h>
#include <ZEngine/Rendering/Scenes/CookedRuntimeScene.h>
#include <new>

namespace ZEngine::Rendering::Scenes
{
    bool CookedRuntimeScene::Prepare(std::unique_ptr<ECS::SceneInstance>&& instance, const Managers::CookedCpuAssetSet& assets, std::unique_ptr<CookedRuntimeScene>* output, ECS::SceneDiagnostics* diagnostics, const SceneMeshBindingLimits& limits)
    {
        if (!output || *output || !instance || !assets.IsValid() || !instance->MatchesCookedClosure(assets.ManifestDigest(), assets.SceneUUID()) || !instance->Document().Settings() || !instance->Arena() || instance->Actors().Count() || !instance->Commands().IsEmpty() || instance->Tick().SystemCount())
        {
            ECS::SceneDiagnosticError(diagnostics, "Runtime binding requires an unpublished scene and its verified cooked closure.");
            return false;
        }
        auto candidate = std::unique_ptr<CookedRuntimeScene>(new (std::nothrow) CookedRuntimeScene{});
        if (!candidate)
            return false;
        candidate->m_instance                     = std::move(instance);
        candidate->m_render.Instances.m_allocator = candidate->m_instance->Arena();
        const auto*             closure           = &assets;
        const SceneMeshResolver resolver          = {&closure, [](void* context, const uuids::uuid& uuid, uint32_t* draws, ECS::SceneDiagnostics*) {
                                                const auto& set  = **static_cast<const Managers::CookedCpuAssetSet**>(context);
                                                const auto* mesh = set.FindMesh({Cook::CookAssetKind::Mesh, uuid});
                                                if (!draws || !mesh || mesh->SubMeshes.empty() || mesh->SubMeshes.size() > UINT32_MAX)
                                                    return false;
                                                *draws = static_cast<uint32_t>(mesh->SubMeshes.size());
                                                return true;
                                                     }};
        if (!BuildSceneMeshBindings(*candidate->m_instance, candidate->m_render, resolver, limits, candidate->m_meshes, candidate->m_draw_count, diagnostics))
            return false;
        const auto& settings    = *candidate->m_instance->Document().Settings();
        candidate->m_render.Sky = settings.Sky;
        if (!MakeSceneGridRenderSnapshot(settings.Grid, candidate->m_render.Grid))
        {
            candidate->m_render.Grid         = {};
            candidate->m_render.Grid.Enabled = false;
        }
        candidate->m_render.MarkInstancesDirty();
        candidate->m_render.MarkSkyDirty();
        candidate->m_instance->RecordMemoryUsage();
        *output = std::move(candidate);
        return true;
    }
} // namespace ZEngine::Rendering::Scenes
