#include "GraphicRenderer.h"
#include "../Cameras/OrthographicCamera.h"
#include "../Textures/Texture2D.h"
#include "../Meshes/Mesh2D.h"


#include <unordered_map>
#include <string>

namespace Z_Engine::Rendering::Renderer {

	class GraphicRenderer2D : public GraphicRenderer {
	public:
		GraphicRenderer2D()		= default;
		~GraphicRenderer2D()	= default;

		void Initialize() override;

		void BeginScene(const Ref<Cameras::Camera>& camera) {
			GraphicRenderer::BeginScene(camera);
		}

		void EndScene() override {
		
		}

		void DrawRect(const glm::vec2& position, const glm::vec2& size, const glm::vec3& color, float angle = 0.0f);
		void DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec3& color, float angle = 0.0f);
		void DrawRect(const glm::vec2& position, const glm::vec2& size, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture>& texture, float angle = 0.0f);
		void DrawRect(const glm::vec3& position, const glm::vec2& size, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture>& texture, float angle = 0.0f);
		void DrawRect(const glm::vec2& position, const glm::vec2& size, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture2D>& texture, float angle = 0.0f);
		void DrawRect(const glm::vec3& position, const glm::vec2& size, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture2D>& texture, float angle = 0.0f);

		void DrawSquare(const glm::vec2& position, const glm::vec2& size, const glm::vec3& color, float angle = 0.0f)													= delete;
		void DrawSquare(const glm::vec3& position, const glm::vec2& size, const glm::vec3& color, float angle = 0.0f)													= delete;
		void DrawSquare(const glm::vec2& position, const glm::vec2& size, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture2D>& texture, float angle = 0.0f)	= delete;
		void DrawSquare(const glm::vec3& position, const glm::vec2& size, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture2D>& texture, float angle = 0.0f)	= delete;
		
		void DrawTriangle(const glm::vec2& position, const glm::vec2& size, const glm::vec3& color, float angle = 0.0f);
		void DrawTriangle(const glm::vec3& position, const glm::vec2& size, const glm::vec3& color, float angle = 0.0f);
		void DrawTriangle(const glm::vec2& position, const glm::vec2& size, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture>& texture, float angle = 0.0f);
		void DrawTriangle(const glm::vec3& position, const glm::vec2& size, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture>& texture, float angle = 0.0f);
		void DrawTriangle(const glm::vec2& position, const glm::vec2& size, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture2D>& texture, float angle = 0.0f);
		void DrawTriangle(const glm::vec3& position, const glm::vec2& size, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture2D>& texture, float angle = 0.0f);

		void DrawCircle(const glm::vec2& position, const glm::vec2& size, const glm::vec3& color, float angle = 0.0f) = delete;
		void DrawCircle(const glm::vec3& position, const glm::vec2& size, const glm::vec3& color, float angle = 0.0f) = delete;
		void DrawCircle(const glm::vec2& position, const glm::vec2& size, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture2D>& texture, float angle = 0.0f) = delete;
		void DrawCircle(const glm::vec3& position, const glm::vec2& size, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture2D>& texture, float angle = 0.0f) = delete;


	private:
		void _DrawRect(const glm::vec4& position, const glm::vec2& size, const glm::vec4& color, float angle);
		void _DrawRect(const glm::vec4& position, const glm::vec2& size, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture2D>& texture, float angle);
																							                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            
		void _DrawTriangle(const glm::vec4& position, const glm::vec2& size, const glm::vec4& color, float angle);	
		void _DrawTriangle(const glm::vec4& position, const glm::vec2& size, const Z_Engine::Ref<Z_Engine::Rendering::Textures::Texture2D>& texture, float angle);


	private:
		 std::unordered_map<std::string, Meshes::Mesh2D> m_mesh_map;
	};
}
	