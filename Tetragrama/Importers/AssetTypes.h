#pragma once
#include <Helpers/NodeHierarchyHelper.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/HashMap.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <glm/glm.hpp>
#include <uuid.h>

namespace Tetragrama::Importers
{
    enum AssetFileType : uint8_t
    {
        UNKNOWN = 0,
        MESH,
        MATERIAL,
        TEXTURES
    };

    struct AssetSubMesh
    {
        uuids::uuid MaterialUUID         = {};
        uint32_t    VertexCount          = 0;
        uint32_t    IndexCount           = 0;
        uint32_t    VertexOffset         = 0;
        uint32_t    IndexOffset          = 0;
        uint32_t    StreamOffset         = 0;
        uint32_t    IndexStreamOffset    = 0;
        uint32_t    VertexUnitStreamSize = 0;
        uint32_t    IndexUnitStreamSize  = 0;
        uint32_t    TotalByteSize        = 0;
    };

    struct AssetMesh
    {
        uuids::uuid                                    MeshUUID  = {};
        ZEngine::Core::Containers::Array<float>        Vertices  = {};
        ZEngine::Core::Containers::Array<uint32_t>     Indices   = {};
        ZEngine::Core::Containers::Array<AssetSubMesh> SubMeshes = {};
    };

    struct AssetMaterial
    {
        ZEngine::Core::Containers::String Name              = {};
        uuids::uuid                       MaterialUUID      = {};
        uuids::uuid                       AlbedoTexUUID     = {};
        uuids::uuid                       EmissiveTexUUID   = {};
        uuids::uuid                       NormalTexUUID     = {};
        uuids::uuid                       OpacityTexUUID    = {};
        uuids::uuid                       SpecularTexUUID   = {};
        float                             AmbientColor[4]   = {0};
        float                             AlbedoColor[4]    = {0};
        float                             EmissiveColor[4]  = {0};
        float                             RoughnessColor[4] = {0};
        float                             SpecularColor[4]  = {0};
        float                             Factors[4]        = {0};
    };

    struct AssetTexture
    {
        ZEngine::Rendering::Textures::TextureHandle Handle      = {};
        uuids::uuid                                 TextureUUID = {};
        ZEngine::Core::Containers::String           Path        = {};
    };

    struct AssetNodeHierarchy
    {
        uuids::uuid                                                         NodeHierarchyUUID = {};
        uuids::uuid                                                         MeshUUID          = {};
        ZEngine::Core::Containers::Array<Helpers::NodeHierarchy>            Hierarchies       = {};
        ZEngine::Core::Containers::Array<glm::mat4>                         LocalTransforms   = {};
        ZEngine::Core::Containers::Array<glm::mat4>                         GlobalTransforms  = {};
        ZEngine::Core::Containers::Array<ZEngine::Core::Containers::String> Names             = {};
        ZEngine::Core::Containers::Array<ZEngine::Core::Containers::String> MaterialNames     = {};
        ZEngine::Core::Containers::HashMap<uint32_t, uint32_t>              NodeNames         = {};
        ZEngine::Core::Containers::HashMap<uint32_t, uint32_t>              NodeMeshes        = {};
        ZEngine::Core::Containers::HashMap<uint32_t, uint32_t>              NodeMaterials     = {};
    };

    struct AssetNodeRef
    {
        int         NodeHierarchyIndex = -1;
        uint32_t    AssetNodeHandle    = 0xFFFFFFFF;
        cstring     Name               = nullptr;
        uuids::uuid AssetMeshUUID      = {};

        bool        IsValid() const
        {
            return (AssetNodeHandle != 0xFFFFFFFF) && (NodeHierarchyIndex != -1);
        }
    };

    struct AssetFile
    {
        const char*                                     Name      = nullptr;
        AssetNodeHierarchy                              Hierarchy = {};
        AssetMesh                                       Mesh      = {};
        ZEngine::Core::Containers::Array<AssetMaterial> Materials = {};
        ZEngine::Core::Containers::Array<AssetTexture>  Textures  = {};
    };
} // namespace Tetragrama::Importers
