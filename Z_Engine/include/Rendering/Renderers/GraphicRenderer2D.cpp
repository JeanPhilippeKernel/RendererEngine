#pragma once
#include <memory>
#include "GraphicRenderer2D.h"
#include "../Buffers/BufferLayout.h"
#include "../../Managers/ShaderManager.h"
#include "../../Managers/TextureManager.h"

#include "../Materials/StandardMaterial.h"

#include "../Geometries/SquareGeometry.h"
#include "../Geometries/QuadGeometry.h"


using namespace Z_Engine::Rendering::Meshes;

using namespace Z_Engine::Rendering::Buffers::Layout;
using namespace Z_Engine::Managers;

namespace Z_Engine::Rendering::Renderers {


	void GraphicRenderer2D::Initialize() {
		GraphicRenderer::Initialize();

		m_graphic_storage->SetVertexBufferLayout(
			{
				ElementLayout<float>(1,	"mesh_index"),
				ElementLayout<float>(3,	"position"),
				ElementLayout<float>(4,	"color"),

				ElementLayout<float>(1,	"texture_id"),
				ElementLayout<float>(2,	"texture_coord")
			});
	}



	void GraphicRenderer2D::StartScene(const Ref<Cameras::Camera>& camera) {
		GraphicRenderer::StartScene(camera);
		m_graphic_storage->StartBacth();
	}

	void GraphicRenderer2D::EndScene() {
		m_graphic_storage->EndBatch();
	}


	void GraphicRenderer2D::Draw(Meshes::Mesh& mesh) {
		m_graphic_storage->AddMesh(mesh);
	}

	void GraphicRenderer2D::Draw(Ref<Meshes::Mesh>& mesh) {
		m_graphic_storage->AddMesh(*mesh);
	}
	
	void GraphicRenderer2D::Draw(std::vector<Meshes::Mesh>& meshes) {
		for(auto& mesh : meshes)
			this->Draw(mesh);
	}  
	
	void GraphicRenderer2D::Draw(std::vector<Ref<Meshes::Mesh>>& meshes) {
		for(auto& mesh : meshes)
			this->Draw(mesh);
	}
}