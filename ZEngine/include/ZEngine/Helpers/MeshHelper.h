#pragma once
#include <Rendering/Meshes/Mesh.h>
#include <assimp/scene.h>

namespace ZEngine::Helpers
{
    Rendering::Meshes::MeshVNext CreateBuiltInMesh(Rendering::Meshes::MeshType mesh_type);
    bool ExtractMeshFromAssimpSceneNode(aiNode* const scene_root_node, std::vector<uint32_t>* const mesh_id_collection_ptr);
    std::vector<Rendering::Meshes::MeshVNext> ConvertAssimpMeshToZEngineMeshModel(const aiScene* assimp_scene, const std::vector<uint32_t>& assimp_mesh_ids);
} // namespace ZEngine::Helpers
