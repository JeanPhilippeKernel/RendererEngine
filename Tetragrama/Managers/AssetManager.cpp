#include <pch.h>
#include <AssetManager.h>
#include <Importers/AssimpImporter.h>

namespace Tetragrama::Managers
{
    void AssetManager::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena)
    {
        // Importer = ZPushStructCtor(arena, Importers::AssimpImporter);
    }
    AssetHandle AssetManager::RegisterAsset(const uuids::uuid& id, void* res)
    {
        return AssetHandle();
    }
    void* AssetManager::GetAsset(const uuids::uuid& id)
    {
        return nullptr;
    }
} // namespace Tetragrama::Managers
