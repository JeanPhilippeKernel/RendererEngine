#pragma once
#include <ZEngine/ECS/ISceneDocumentSerializer.h>

namespace ZEngine::ECS
{
    class YAMLSceneDocumentSerializer final : public ISceneDocumentSerializer
    {
    public:
        Core::VFS::VFSResult<void> Read(Core::VFS::IVFSContext& vfs, const Core::VFS::VFSPath& source, const SceneSerializationLimits& limits, Core::Memory::ArenaAllocator* document_arena, SceneDocument* out_document, SceneDiagnostics* diagnostics) const override;

        Core::VFS::VFSResult<void> WriteAtomic(Core::VFS::IVFSContext& vfs, const Core::VFS::VFSPath& destination, const SceneDocument& document, SceneDiagnostics* diagnostics) const override;
    };
} // namespace ZEngine::ECS
