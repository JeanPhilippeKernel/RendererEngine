#include <pch.h>
#include <ZEngineDef.h>
#include <Helpers/RendererHelper.h>
#include <Helpers/MeshHelper.h>
#include <Rendering/Renderers/Storages/IVertex.h>
#include <Hardwares/VulkanDevice.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

#include <Rendering/Buffers/FrameBuffers/FrameBufferSpecification.h>

using namespace ZEngine::Rendering::Renderers;

namespace ZEngine::Helpers
{

    VkRenderPass CreateRenderPass(RenderPasses::AttachmentSpecification& specification)
    {
        VkRenderPass out_render_pass{VK_NULL_HANDLE};

        /*Ensure Subpass is updated with its associated color attachment reference*/
        for (RenderPasses::SubPassSpecification& subpass_specification : specification.SubpassSpecifications)
        {
            subpass_specification.SubpassDescription.colorAttachmentCount = subpass_specification.ColorAttachementReferences.size();
            subpass_specification.SubpassDescription.pColorAttachments    = subpass_specification.ColorAttachementReferences.data();
            if (subpass_specification.DepthStencilAttachementReference.layout != VK_IMAGE_LAYOUT_UNDEFINED)
            {
                subpass_specification.SubpassDescription.pDepthStencilAttachment = &(subpass_specification.DepthStencilAttachementReference);
            }
        }

        VkRenderPassCreateInfo render_pass_create_info = {};
        render_pass_create_info.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_create_info.attachmentCount        = (uint32_t) specification.ColorAttachements.size();
        render_pass_create_info.pAttachments           = specification.ColorAttachements.data();

        std::vector<VkSubpassDescription> sub_pass_descriptions;
        std::transform(
            std::begin(specification.SubpassSpecifications),
            std::end(specification.SubpassSpecifications),
            std::back_inserter(sub_pass_descriptions),
            [](const auto& sub_pass_specification) {
                return sub_pass_specification.SubpassDescription;
            });

        render_pass_create_info.subpassCount    = (uint32_t) sub_pass_descriptions.size();
        render_pass_create_info.pSubpasses      = sub_pass_descriptions.data();
        render_pass_create_info.dependencyCount = (uint32_t) specification.SubpassDependencies.size();
        render_pass_create_info.pDependencies   = specification.SubpassDependencies.data();

        auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        ZENGINE_VALIDATE_ASSERT(vkCreateRenderPass(device, &render_pass_create_info, nullptr, &out_render_pass) == VK_SUCCESS, "Failed to create render pass")

        return out_render_pass;
    }

    VkPipelineLayout CreatePipelineLayout(const VkPipelineLayoutCreateInfo& pipeline_layout_create_info)
    {
        VkPipelineLayout out_pipeline_layout = VK_NULL_HANDLE;
        auto             device              = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        ZENGINE_VALIDATE_ASSERT(
            vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr, &out_pipeline_layout) == VK_SUCCESS, "Failed to create pipeline layout")

        return out_pipeline_layout;
    }

    void FillDefaultPipelineFixedStates(Rendering::Renderers::Pipelines::GraphicRendererPipelineStateSpecification& specification)
    {
        /*Dynamic State*/
        specification.DynamicStates                            = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        specification.DynamicStateCreateInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        specification.DynamicStateCreateInfo.dynamicStateCount = specification.DynamicStates.size();
        specification.DynamicStateCreateInfo.pDynamicStates    = specification.DynamicStates.data();
        specification.DynamicStateCreateInfo.flags             = 0;
        specification.DynamicStateCreateInfo.pNext             = nullptr;

        /*Input Assembly*/
        specification.InputAssemblyStateCreateInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        specification.InputAssemblyStateCreateInfo.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        specification.InputAssemblyStateCreateInfo.primitiveRestartEnable = VK_FALSE;
        specification.InputAssemblyStateCreateInfo.flags                  = 0;
        specification.InputAssemblyStateCreateInfo.pNext                  = nullptr;

        /*Viewports and scissors*/
        specification.Viewport.x = 0.0f;
        specification.Viewport.y = 0.0f;

        specification.Viewport.width    = 1;
        specification.Viewport.height   = 1;
        specification.Viewport.minDepth = 0.0f;
        specification.Viewport.maxDepth = 1.0f;

        specification.Scissor.offset = {0, 0};
        specification.Scissor.extent = VkExtent2D{1, 1};

        specification.ViewportStateCreateInfo.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        specification.ViewportStateCreateInfo.viewportCount = 1;
        specification.ViewportStateCreateInfo.scissorCount  = 1;
        specification.ViewportStateCreateInfo.pViewports    = nullptr;
        specification.ViewportStateCreateInfo.pScissors     = nullptr;
        specification.ViewportStateCreateInfo.pNext         = nullptr;
        specification.ViewportStateCreateInfo.flags         = 0;
        // information.ViewportStateCreateInfo.pViewports    = &(information.Viewport);
        // information.ViewportStateCreateInfo.pScissors     = &(information.Scissor);

        /*Rasterizer*/
        specification.RasterizationStateCreateInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        specification.RasterizationStateCreateInfo.depthClampEnable        = VK_FALSE;
        specification.RasterizationStateCreateInfo.rasterizerDiscardEnable = VK_FALSE;
        specification.RasterizationStateCreateInfo.polygonMode             = VK_POLYGON_MODE_FILL;
        specification.RasterizationStateCreateInfo.lineWidth               = 1.0f;
        specification.RasterizationStateCreateInfo.cullMode                = VK_CULL_MODE_BACK_BIT;
        specification.RasterizationStateCreateInfo.frontFace               = VK_FRONT_FACE_CLOCKWISE;
        specification.RasterizationStateCreateInfo.depthBiasEnable         = VK_FALSE;
        specification.RasterizationStateCreateInfo.depthBiasConstantFactor = 0.0f; // Optional
        specification.RasterizationStateCreateInfo.depthBiasClamp          = 0.0f; // Optional
        specification.RasterizationStateCreateInfo.depthBiasSlopeFactor    = 0.0f; // Optional
        specification.RasterizationStateCreateInfo.pNext                   = nullptr;

        /*Multisampling*/
        specification.MultisampleStateCreateInfo.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        specification.MultisampleStateCreateInfo.sampleShadingEnable   = VK_FALSE;
        specification.MultisampleStateCreateInfo.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
        specification.MultisampleStateCreateInfo.minSampleShading      = 1.0f;     // Optional
        specification.MultisampleStateCreateInfo.pSampleMask           = nullptr;  // Optional
        specification.MultisampleStateCreateInfo.alphaToCoverageEnable = VK_FALSE; // Optional
        specification.MultisampleStateCreateInfo.alphaToOneEnable      = VK_FALSE; // Optional
        specification.MultisampleStateCreateInfo.flags                 = 0;
        specification.MultisampleStateCreateInfo.pNext                 = nullptr;

        /*Depth and Stencil testing*/
        specification.DepthStencilStateCreateInfo.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        specification.DepthStencilStateCreateInfo.depthTestEnable       = VK_TRUE;
        specification.DepthStencilStateCreateInfo.depthWriteEnable      = VK_TRUE;
        specification.DepthStencilStateCreateInfo.depthCompareOp        = VK_COMPARE_OP_LESS;
        specification.DepthStencilStateCreateInfo.depthBoundsTestEnable = VK_FALSE;
        specification.DepthStencilStateCreateInfo.minDepthBounds        = 0.0f; // Optional
        specification.DepthStencilStateCreateInfo.maxDepthBounds        = 1.0f; // Optional
        specification.DepthStencilStateCreateInfo.stencilTestEnable     = VK_FALSE;
        specification.DepthStencilStateCreateInfo.front                 = {}; // Optional
        specification.DepthStencilStateCreateInfo.back                  = {}; // Optiona

        /*Color blending*/
        // This configuration is per-framebuffer definition
        specification.ColorBlendAttachmentState.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        specification.ColorBlendAttachmentState.blendEnable         = VK_TRUE;
        specification.ColorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        specification.ColorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        specification.ColorBlendAttachmentState.colorBlendOp        = VK_BLEND_OP_ADD;
        specification.ColorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        specification.ColorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        specification.ColorBlendAttachmentState.alphaBlendOp        = VK_BLEND_OP_ADD;

        // this configuration is for global color blending settings
        specification.ColorBlendStateCreateInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        specification.ColorBlendStateCreateInfo.logicOpEnable     = VK_FALSE;
        specification.ColorBlendStateCreateInfo.logicOp           = VK_LOGIC_OP_COPY; // Optional
        specification.ColorBlendStateCreateInfo.attachmentCount   = 1;
        specification.ColorBlendStateCreateInfo.pAttachments      = &specification.ColorBlendAttachmentState;
        specification.ColorBlendStateCreateInfo.blendConstants[0] = 0.0f; // Optional
        specification.ColorBlendStateCreateInfo.blendConstants[1] = 0.0f; // Optional
        specification.ColorBlendStateCreateInfo.blendConstants[2] = 0.0f; // Optional
        specification.ColorBlendStateCreateInfo.blendConstants[3] = 0.0f; // Optional
        specification.ColorBlendStateCreateInfo.flags             = 0;
        specification.ColorBlendStateCreateInfo.pNext             = nullptr;

        /*Pipeline layout*/
        // Uniform Shader configuration
        specification.LayoutCreateInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        specification.LayoutCreateInfo.setLayoutCount         = 0;       // Optional
        specification.LayoutCreateInfo.pSetLayouts            = nullptr; // Optional
        specification.LayoutCreateInfo.pushConstantRangeCount = 0;       // Optional
        specification.LayoutCreateInfo.pPushConstantRanges    = nullptr; // Optional
        specification.LayoutCreateInfo.flags                  = VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT;
        specification.LayoutCreateInfo.pNext                  = nullptr;

        /*Vertex Input*/
        auto& vertex_attribute_description_collection                             = Storages::IVertex::GetVertexAttributeDescription();
        specification.VertexInputStateCreateInfo.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        specification.VertexInputStateCreateInfo.vertexAttributeDescriptionCount = vertex_attribute_description_collection.size();
        specification.VertexInputStateCreateInfo.pVertexAttributeDescriptions    = vertex_attribute_description_collection.data();
        auto& vertex_input_binding_description_collection                         = Storages::IVertex::GetVertexInputBindingDescription();
        specification.VertexInputStateCreateInfo.vertexBindingDescriptionCount   = vertex_input_binding_description_collection.size();
        specification.VertexInputStateCreateInfo.pVertexBindingDescriptions      = vertex_input_binding_description_collection.data();
        specification.VertexInputStateCreateInfo.pNext                           = nullptr;
    }

    Rendering::Meshes::MeshVNext CreateBuiltInMesh(Rendering::Meshes::MeshType mesh_type)
    {
        Rendering::Meshes::MeshVNext custom_mesh = {std::vector<float>{}, std::vector<uint32_t>{}, 0};
        // std::string_view             custom_mesh = "Assets/Meshes/duck.obj";
         //std::string_view             custom_mesh = "Assets/Meshes/cube.obj";
         std::string_view mesh_file = "Assets/Meshes/viking_room.obj";

        Assimp::Importer importer = {};

        const aiScene* scene_ptr = importer.ReadFile(mesh_file.data(), aiProcess_Triangulate | aiProcess_FlipUVs);

        if ((!scene_ptr) || scene_ptr->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene_ptr->mRootNode)
        {
            custom_mesh.VertexCount = 0xFFFFFFFF;
        }
        else
        {
            std::vector<uint32_t> mesh_id_collection;
            bool                  successful = ExtractMeshFromAssimpSceneNode(scene_ptr->mRootNode, &mesh_id_collection);
            if (successful)
            {
                auto meshes = ConvertAssimpMeshToZEngineMeshModel(scene_ptr, mesh_id_collection);

                /*The cube obj model is actually one Mesh, so we are safe */
                if (!meshes.empty())
                {
                    custom_mesh = std::move(meshes.front());
                }
            }
        }

        importer.FreeScene();

        custom_mesh.Type = mesh_type;
        return custom_mesh;
    }

    bool ExtractMeshFromAssimpSceneNode(aiNode* const root_node, std::vector<uint32_t>* const mesh_id_collection_ptr)
    {
        if (!root_node || !mesh_id_collection_ptr)
        {
            return false;
        }

        for (int i = 0; i < root_node->mNumMeshes; ++i)
        {
            mesh_id_collection_ptr->push_back(root_node->mMeshes[i]);
        }

        if (root_node->mNumChildren > 0)
        {
            for (int i = 0; i < root_node->mNumChildren; ++i)
            {
                ExtractMeshFromAssimpSceneNode(root_node->mChildren[i], mesh_id_collection_ptr);
            }
        }

        return true;
    }

    std::vector<Rendering::Meshes::MeshVNext> ConvertAssimpMeshToZEngineMeshModel(const aiScene* assimp_scene, const std::vector<uint32_t>& assimp_mesh_ids)
    {
        std::vector<Rendering::Meshes::MeshVNext> meshes = {};

        for (int i = 0; i < assimp_mesh_ids.size(); ++i)
        {
            aiMesh* assimp_mesh = assimp_scene->mMeshes[assimp_mesh_ids[i]];

            uint32_t              vertex_count{0};
            std::vector<float>    vertices = {};
            std::vector<uint32_t> indices  = {};

            /* Vertice processing */
            for (int x = 0; x < assimp_mesh->mNumVertices; ++x)
            {
                vertices.push_back(assimp_mesh->mVertices[x].x);
                vertices.push_back(assimp_mesh->mVertices[x].y);
                vertices.push_back(assimp_mesh->mVertices[x].z);

                vertices.push_back(assimp_mesh->mNormals[x].x);
                vertices.push_back(assimp_mesh->mNormals[x].y);
                vertices.push_back(assimp_mesh->mNormals[x].z);

                if (assimp_mesh->mTextureCoords[0])
                {
                    vertices.push_back(assimp_mesh->mTextureCoords[0][x].x);
                    vertices.push_back(assimp_mesh->mTextureCoords[0][x].y);
                }
                else
                {
                    vertices.push_back(0.0f);
                    vertices.push_back(0.0f);
                }

                vertex_count++;
            }

            /* Face and Indices processing */
            for (int ix = 0; ix < assimp_mesh->mNumFaces; ++ix)
            {
                aiFace assimp_mesh_face = assimp_mesh->mFaces[ix];

                for (int j = 0; j < assimp_mesh_face.mNumIndices; ++j)
                {
                    indices.push_back(assimp_mesh_face.mIndices[j]);
                }
            }

            Rendering::Meshes::MeshVNext zengine_mesh = {std::move(vertices), std::move(indices), vertex_count};
            meshes.push_back(std::move(zengine_mesh));
        }

        return meshes;
    }
} // namespace ZEngine::Helpers
