#pragma once
#include <ZEngine/ECS/SceneInstance.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>

namespace ZEngine::Rendering::Scenes
{
    /// Resolves CPU mesh draw counts on the exclusive scene owner. It must not
    /// mutate active bindings; returned counts cover all submeshes of a mesh.
    struct SceneMeshResolver
    {
        void* Context                                                                     = nullptr;
        bool (*ResolveMesh)(void*, const uuids::uuid&, uint32_t*, ECS::SceneDiagnostics*) = nullptr;
    };
    struct SceneMeshBindingLimits
    {
        uint32_t MaxInstances    = SceneData::MAX_DRAW_COMMANDS;
        uint32_t MaxDrawCommands = SceneData::MAX_DRAW_COMMANDS;
    };

    /// Builds an unpublished scene's complete mesh identities and instances in
    /// its bounded arena. Render instances must be initialized and empty. On
    /// failure the caller discards the candidate, including any partial output.
    [[nodiscard]] bool BuildSceneMeshBindings(ECS::SceneInstance&, RenderScene&, const SceneMeshResolver&, const SceneMeshBindingLimits&, Core::Containers::Array<uuids::uuid>&, uint32_t&, ECS::SceneDiagnostics*);
} // namespace ZEngine::Rendering::Scenes
