#pragma once
#include <IAssetImporter.h>
#include <assimp/Importer.hpp>
#include <assimp/ProgressHandler.hpp>
#include <assimp/scene.h>

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

        std::future<void> ImportAsync(std::string_view filename, ImportConfiguration config = {}) override;

    private:
        uint32_t              m_flags;
        AssimpProgressHandler m_progress_handler;

        friend struct AssimpProgressHandler;

        void ExtractMeshes(const aiScene*, ImporterData&);
        void ExtractMaterials(const aiScene*, ImporterData&);
        void ExtractTextures(const aiScene*, ImporterData&);
        void CreateHierachyScene(const aiScene*, ImporterData&);

        void      TraverseNode(const aiScene*, ZEngine::Rendering::Scenes::SceneRawData* const, const aiNode*, int parent_node_id, int depth_level);
        glm::mat4 ConvertToMat4(const aiMatrix4x4& m);

        int GenerateFileIndex(std::vector<std::string>& data_viewer, std::string_view filename);
    };
} // namespace Tetragrama::Importers