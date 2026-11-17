#include <ZEngine/Managers/CookedCpuAssetSet.h>
#include <ZEngine/Rendering/Scenes/RuntimeScene.h>
#include <new>

namespace ZEngine::Rendering::Scenes
{
    bool RuntimeScene::PrepareFromCooked(std::unique_ptr<ECS::SceneInstance>&& instance, const Managers::CookedCpuAssetSet& assets, std::unique_ptr<RuntimeScene>* output, ECS::SceneDiagnostics* diagnostics, const SceneMeshBindingLimits& limits)
    {
        if (!output || *output || !instance || !assets.IsValid() || !instance->MatchesCookedClosure(assets.ManifestDigest(), assets.SceneUUID()) || !instance->Document().Settings() || !instance->Arena() || instance->Actors().Count() || !instance->Commands().IsEmpty() || instance->Tick().SystemCount())
        {
            ECS::SceneDiagnosticError(diagnostics, "Runtime binding requires an unpublished scene and its verified cooked closure.");
            return false;
        }
        auto candidate = std::unique_ptr<RuntimeScene>(new (std::nothrow) RuntimeScene{});
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
    ECS::SceneInstance& RuntimeScene::Instance() noexcept
    {
        return *m_instance;
    }

    RenderScene& RuntimeScene::Render() noexcept
    {
        return m_render;
    }

    SceneResourcePreparation& RuntimeScene::Resources() noexcept
    {
        return m_resources;
    }

    std::span<const uuids::uuid> RuntimeScene::RequiredMeshes() const noexcept
    {
        return {m_meshes.data(), m_meshes.size()};
    }

    uint32_t RuntimeScene::DrawCount() const noexcept
    {
        return m_draw_count;
    }

} // namespace ZEngine::Rendering::Scenes
