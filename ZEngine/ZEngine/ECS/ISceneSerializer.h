#pragma once
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/VFSError.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/ECS/SceneSnapshot.h>

namespace ZEngine::ECS
{
    class ISceneSerializer
    {
    public:
        virtual ~ISceneSerializer()                                                                                                           = default;

        virtual Core::VFS::VFSResult<void> Serialize(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, const SceneSnapshot& scene) = 0;

        virtual Core::VFS::VFSResult<void> Deserialize(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, SceneSnapshot& out_scene) = 0;
    };
} // namespace ZEngine::ECS
