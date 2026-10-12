#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/VFSError.h>
#include <ZEngine/ECS/SceneDiagnostics.h>
#include <ZEngine/ECS/SceneDocument.h>

namespace ZEngine::ECS
{
    class Scene;
    class SceneComponentSchemaRegistry;
    class SceneCompatibilityService;

    class SceneDocumentBuilder
    {
    public:
        void                       Initialize(const SceneComponentSchemaRegistry* schemas, const SceneCompatibilityService* compatibility);

        Core::VFS::VFSResult<void> Capture(const Scene& scene, const SceneDocument* previously_loaded_document, Core::Memory::ArenaAllocator* document_arena, SceneDocument* out_document, SceneDiagnostics* diagnostics) const;

    private:
        const SceneComponentSchemaRegistry* m_schemas       = nullptr;
        const SceneCompatibilityService*    m_compatibility = nullptr;
    };
} // namespace ZEngine::ECS
