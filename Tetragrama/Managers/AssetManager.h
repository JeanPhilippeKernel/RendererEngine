#pragma once
#include <Importers/AssetTypes.h>

namespace Tetragrama::Managers
{

    struct AssetHandle
    {
    };

    struct AssetManager
    {
        ZEngine::Core::Containers::Array<Importers::AssetNodeHierarchy> NodeHierarchies = {};
        ZEngine::Core::Containers::Array<Importers::AssetMesh>          Meshes          = {};
        ZEngine::Core::Containers::Array<Importers::AssetMaterial>      Materials       = {};
        ZEngine::Core::Containers::Array<Importers::AssetTexture>       Textures        = {};

        ZEngine::Core::Containers::HashMap<uuids::uuid, AssetHandle>    UUIDToHandle    = {};

        void                                                            Initialize(ZEngine::Core::Memory::ArenaAllocator* arena);

        AssetHandle                                                     RegisterAsset(const uuids::uuid& id, void* res);
        void*                                                           GetAsset(const uuids::uuid& id);

        void                                                            LoadAssetFile(const char* filename) {}
    };
} // namespace Tetragrama::Managers