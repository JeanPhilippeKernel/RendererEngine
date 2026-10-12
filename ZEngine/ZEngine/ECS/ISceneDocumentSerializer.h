#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/VFSError.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/ECS/SceneDiagnostics.h>
#include <ZEngine/ECS/SceneDocument.h>

namespace ZEngine::ECS
{
    class ISceneDocumentSerializer
    {
    public:
        virtual ~ISceneDocumentSerializer()                                                                                                                                                                                                                            = default;

        virtual Core::VFS::VFSResult<void> Read(Core::VFS::IVFSContext& vfs, const Core::VFS::VFSPath& source, const SceneSerializationLimits& limits, Core::Memory::ArenaAllocator* document_arena, SceneDocument* out_document, SceneDiagnostics* diagnostics) const = 0;

        virtual Core::VFS::VFSResult<void> WriteAtomic(Core::VFS::IVFSContext& vfs, const Core::VFS::VFSPath& destination, const SceneDocument& document, SceneDiagnostics* diagnostics) const                                                                         = 0;
    };
} // namespace ZEngine::ECS
