#pragma once
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/AssetTypes.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <assimp/Importer.hpp>
#include <assimp/ProgressHandler.hpp>
#include <assimp/scene.h>
#include <uuid.h>

namespace ZEngine::Importers
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

        void                         Initialize(Core::Memory::ArenaAllocator* arena);

        Core::Memory::ArenaAllocator Arena = {};

        // IAssetImporter
        bool                         CanImport(const char* extension) const override;
        Core::VFS::VFSResult<void>   Import(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta) override;

        // Editor helper — called directly by DockspaceUIComponent for the
        // file-picker import path (produces cooked .zasset artifacts).
        void                         ImportFile(const char* filename, const AssetCodec::ImportConfiguration& config, Core::Memory::ArenaAllocator* arena, void* context, ImportCompleteCallback on_complete, ImportProgressCallback on_progress, ImportErrorCallback on_error, ImportLogCallback on_log);

        void                         CopyTextureFiles(Core::Memory::ArenaAllocator*, Core::Containers::Array<AssetTexture>&, const AssetCodec::ImportConfiguration&);

    private:
        AssimpProgressHandler m_progress_handler;

        friend struct AssimpProgressHandler;

        void               ExtractMeshes(Core::Memory::ArenaAllocator*, const aiScene*, uuids::uuid_random_generator&, AssetMesh&);
        void               ExtractMaterials(Core::Memory::ArenaAllocator*, const aiScene*, uuids::uuid_random_generator&, Core::Containers::Array<AssetMaterial>&, AssetNodeHierarchy&);
        void               ExtractTextures(Core::Memory::ArenaAllocator* arena, const aiScene*, uuids::uuid_random_generator&, Core::Containers::Array<AssetMaterial>&, Core::Containers::Array<AssetTexture>&);
        void               CreateHierachy(Core::Memory::ArenaAllocator* arena, const aiScene*, uuids::uuid_random_generator&, AssetNodeHierarchy&, AssetMesh&, Core::Containers::Array<AssetMaterial>&);
        void               TraverseNode(Core::Memory::ArenaAllocator* arena, const aiScene*, const aiNode*, AssetNodeHierarchy&, AssetMesh&, ZEngine::Core::Containers::Array<AssetMaterial>&, int parent_node_id, int depth_level);
        Core::Maths::Mat4f ConvertToMat4(const aiMatrix4x4& m);
    };
} // namespace ZEngine::Importers
