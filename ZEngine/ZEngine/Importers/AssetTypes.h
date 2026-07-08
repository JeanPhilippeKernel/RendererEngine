#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Helpers/NodeHierarchyHelper.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <uuid.h>

namespace ZEngine::Importers
{
    enum AssetFileType : uint8_t
    {
        UNKNOWN = 0,
        MESH,
        MATERIAL,
        TEXTURES,
        ENVIRONMENT_MAP
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
        uuids::uuid                           MeshUUID  = {};
        Core::Containers::Array<float>        Vertices  = {};
        Core::Containers::Array<uint32_t>     Indices   = {};
        Core::Containers::Array<AssetSubMesh> SubMeshes = {};
    };

    struct AssetMaterial
    {
        Core::Containers::String Name              = {};
        uuids::uuid              MaterialUUID      = {};
        uuids::uuid              AlbedoTexUUID     = {};
        uuids::uuid              EmissiveTexUUID   = {};
        uuids::uuid              NormalTexUUID     = {};
        uuids::uuid              OpacityTexUUID    = {};
        uuids::uuid              SpecularTexUUID   = {};
        float                    AmbientColor[4]   = {0};
        float                    AlbedoColor[4]    = {0};
        float                    EmissiveColor[4]  = {0};
        float                    RoughnessColor[4] = {0};
        float                    SpecularColor[4]  = {0};
        float                    Factors[4]        = {0};
    };

    struct AssetTexture
    {
        Rendering::Textures::TextureHandle Handle      = {};
        uuids::uuid                        TextureUUID = {};
        Core::Containers::String           Path        = {};
    };

    struct AssetNodeHierarchy
    {
        uuids::uuid                                            NodeHierarchyUUID = {};
        uuids::uuid                                            MeshUUID          = {};
        Core::Containers::Array<Helpers::NodeHierarchy>        Hierarchies       = {};
        Core::Containers::Array<Core::Maths::Mat4f>            LocalTransforms   = {};
        Core::Containers::Array<Core::Maths::Mat4f>            GlobalTransforms  = {};
        Core::Containers::Array<Core::Containers::String>      Names             = {};
        Core::Containers::Array<Core::Containers::String>      MaterialNames     = {};
        Core::Containers::UnorderedHashMap<uint32_t, uint32_t> NodeNames         = {};
        Core::Containers::UnorderedHashMap<uint32_t, uint32_t> NodeMeshes        = {};
        Core::Containers::UnorderedHashMap<uint32_t, uint32_t> NodeMaterials     = {};
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
        const char*                            Name      = nullptr;
        AssetNodeHierarchy                     Hierarchy = {};
        AssetMesh                              Mesh      = {};
        Core::Containers::Array<AssetMaterial> Materials = {};
        Core::Containers::Array<AssetTexture>  Textures  = {};
    };
} // namespace ZEngine::Importers
