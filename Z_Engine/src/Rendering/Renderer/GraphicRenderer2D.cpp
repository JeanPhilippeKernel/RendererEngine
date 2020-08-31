#include "GraphicRenderer2D.h"
#include "../Buffers/BufferLayout.h"
#include "../../Managers/ShaderManager.h"
#include "../../Managers/TextureManager.h"


using namespace Z_Engine::Rendering::Buffers::Layout;
using namespace Z_Engine::Managers;

namespace Z_Engine::Rendering::Renderer {


	void GraphicRenderer2D::Initialize() {
		GraphicRenderer::Initialize();
	
		ShaderManager::Load("src/Assets/Shaders/basic_2.glsl");

		//Rectangle
		std::vector<float> vertices{
			-0.75f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
			 0.75f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
			 0.75f,	 0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
			-0.75f,	 0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f
		};

		std::vector<unsigned int> indices{ 0, 1, 2, 2, 3, 0 };
		BufferLayout<float> layout{ ElementLayout<float>{3, "position"}, ElementLayout<float>{4, "color"} };
		GraphicRendererStorage<float, unsigned int> storage_rect(ShaderManager::Get("basic_2"), vertices, indices, layout);


		//Triangles
		std::vector<float> vertices_2{
			0.5f, -0.5f, 1.0f,	1.0f, 0.5f, 0.3f, 1.0f,
	       -0.5f, -0.5f, 1.0f,	0.5f, 0.1f, 0.6f, 1.0f,
	        0.0f,  0.5f, 1.0f,	0.1f, 0.3f, 0.2f, 1.0f
		};

		std::vector<unsigned int> indices_2{ 0, 1, 2 };
		BufferLayout<float> layout_2{ ElementLayout<float>{3, "position"}, ElementLayout<float>{4, "color"} };
		GraphicRendererStorage<float, unsigned int> storage_tri(ShaderManager::Get("basic_2"), vertices_2, indices_2, layout_2);
										   
		m_storages.emplace(0, storage_rect);
		m_storages.emplace(1, storage_tri);
	}

	void GraphicRenderer2D::DrawRect(const glm::vec2& position, const glm::vec2& size, const glm::vec3& color) {
		DrawRect({position.x, position.y, 0.0f}, size, color);
	}
	
	void GraphicRenderer2D::DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec3& color) {
		_DrawRect({position.x, position.y, position.z, 0.0f}, size, {color.r, color.g, color.b, 1.0f});
	}

	void GraphicRenderer2D::DrawTriangle(const glm::vec2& position, const glm::vec2& size, const glm::vec3& color) {
		DrawTriangle({position.x, position.y, 0.0f}, size, color);
	}
	
	void GraphicRenderer2D::DrawTriangle(const glm::vec3& position, const glm::vec2& size, const glm::vec3& color) {
		_DrawTriangle({position.x, position.y, position.z, 0.0f}, size, {color.r, color.g, color.b, 1.0f});
	}




	void GraphicRenderer2D::_DrawRect(const glm::vec4& position, const glm::vec2& size, const glm::vec4& color) {
		GraphicRenderer::Submit(m_storages[0].GetShader(), m_storages[0].GetVertexArray());
	}
	
	void GraphicRenderer2D::_DrawTriangle(const glm::vec4& position, const glm::vec2& size, const glm::vec4& color) {
		GraphicRenderer::Submit(m_storages[1].GetShader(), m_storages[1].GetVertexArray());
	}
}