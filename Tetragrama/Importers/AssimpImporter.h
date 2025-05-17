#pragma once
#include <AssetTypes.h>
#include <IAssetImporter.h>
#include <assimp/Importer.hpp>
#include <assimp/ProgressHandler.hpp>
#include <assimp/scene.h>
#include <uuid.h>

namespace Tetragrama::Importers
{
    class AssimpImporter;
    struct AssimpProgressHandler;

    struct AssimpProgressHandler : public Assimp::ProgressHandler
    {
        void SetImporter(AssimpImporter* const importer);
        bool Update(float percentage) override;

    private:
        AssimpImporter* m_importer{nullptr};
    };

    class AssimpImporter : public IAssetImporter
    {
    public:
        AssimpImporter();
        virtual ~AssimpImporter();

        virtual std::future<void> ImportAsync(const char* filename, ImportConfiguration& config) override;
        void                      CopyTextureFiles(ZEngine::Core::Memory::ArenaAllocator*, ZEngine::Core::Containers::Array<AssetTexture>&, const ImportConfiguration&);

    private:
        uint32_t              m_flags;
        AssimpProgressHandler m_progress_handler;

        friend struct AssimpProgressHandler;

        void      ExtractMeshes(ZEngine::Core::Memory::ArenaAllocator*, const aiScene*, uuids::uuid_random_generator&, AssetMesh&);
        void      ExtractMaterials(ZEngine::Core::Memory::ArenaAllocator*, const aiScene*, uuids::uuid_random_generator&, ZEngine::Core::Containers::Array<AssetMaterial>&, AssetNodeHierarchy&);
        void      ExtractTextures(ZEngine::Core::Memory::ArenaAllocator* arena, const aiScene*, uuids::uuid_random_generator&, ZEngine::Core::Containers::Array<AssetMaterial>&, ZEngine::Core::Containers::Array<AssetTexture>&);
        void      CreateHierachy(ZEngine::Core::Memory::ArenaAllocator* arena, const aiScene*, uuids::uuid_random_generator&, AssetNodeHierarchy&, AssetMesh&, ZEngine::Core::Containers::Array<AssetMaterial>&);

        void      TraverseNode(ZEngine::Core::Memory::ArenaAllocator* arena, const aiScene*, const aiNode*, AssetNodeHierarchy&, AssetMesh&, ZEngine::Core::Containers::Array<AssetMaterial>&, int parent_node_id, int depth_level);
        glm::mat4 ConvertToMat4(const aiMatrix4x4& m);
    };
} // namespace Tetragrama::Importers