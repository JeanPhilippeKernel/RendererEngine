#pragma once
#include <ZEngine/ECS/SceneCompatibility.h>

namespace ZEngine::Cook::Tests
{
    // Real test-only user component: authored UUID/float values and a runtime
    // cache that must never enter source/cooked data. No callback context graph.
    struct CookCustomComponent
    {
        uuids::uuid Texture      = {};
        float       Weight       = 0;
        uint32_t    RuntimeCache = UINT32_MAX;
    };

    inline constexpr cstring CookCustomSchemaKey = "GameSurface";

    [[nodiscard]] bool       RegisterCookCustomSchema(ECS::SceneComponentSchemaRegistry&, uint32_t version, ECS::SceneDiagnostics*);
    [[nodiscard]] bool       MigrateCookCustomSchema(void*, const ECS::SceneYAML::Node&, ECS::SceneYAML::Node*, ECS::SceneDiagnostics*);
} // namespace ZEngine::Cook::Tests
