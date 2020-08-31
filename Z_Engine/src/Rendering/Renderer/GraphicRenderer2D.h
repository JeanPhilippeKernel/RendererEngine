#include "GraphicRenderer.h"
#include "../Cameras/OrthographicCamera.h"


#include <unordered_map>

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

		void DrawRect(const glm::vec2& position, const glm::vec2& size,  const glm::vec3& color); 
		void DrawRect(const glm::vec3& position, const glm::vec2& size,  const glm::vec3& color); 
		
		void DrawTriangle(const glm::vec2& position, const glm::vec2& size, const glm::vec3& color); 
		void DrawTriangle(const glm::vec3& position, const glm::vec2& size, const glm::vec3& color); 

	private:
		void _DrawRect(const glm::vec4& position, const glm::vec2& size,  const glm::vec4& color); 
		void _DrawTriangle(const glm::vec4& position, const glm::vec2& size, const glm::vec4& color); 


	private:
		 std::unordered_map<int, GraphicRendererStorage<float, unsigned int>> m_storages;
	};
}
	