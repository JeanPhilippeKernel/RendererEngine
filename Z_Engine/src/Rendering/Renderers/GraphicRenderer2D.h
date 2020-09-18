#include "GraphicRenderer.h"
#include "../Cameras/OrthographicCamera.h"
#include "../Textures/Texture2D.h"																																   
#include "../Meshes/Mesh2D.h"


#include <unordered_map>
#include <string>

namespace Z_Engine::Rendering::Renderers {

	class GraphicRenderer2D : public GraphicRenderer {
	public:
		GraphicRenderer2D()		= default;
		~GraphicRenderer2D()	= default;

		void Initialize() override;

		void BeginScene(const Ref<Cameras::Camera>& camera) {
			GraphicRenderer::BeginScene(camera);
			m_graphic_storage->SetShader(m_shader_manager->Obtains("simple_mesh_2d"));
			m_graphic_storage->SetVertexBufferLayout(
				{
					Rendering::Buffers::Layout::ElementLayout<float>(3,"position"), 
					Rendering::Buffers::Layout::ElementLayout<float>(4,"color"),
					Rendering::Buffers::Layout::ElementLayout<float>(3,"texture")
				});

		}

		void EndScene() override {
			  m_graphic_storage->UpdateBuffers();
			  GraphicRenderer::Submit(m_graphic_storage->GetShader(), m_graphic_storage->GetVertexArray());
			  m_graphic_storage->FlushBuffers();
		}

		void DrawRect(const glm::vec2& position, const glm::vec2& size, const glm::vec3& color, float angle = 0.0f);  
		void DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec3& color, float angle = 0.0f);
		void DrawRect(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture>& texture);
		void DrawRect(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture>& texture);
		void DrawRect(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture);
		void DrawRect(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture);			
		
		void DrawRect(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture>& texture, const glm::vec4& tint_color, float tiling_factor);
		void DrawRect(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture>& texture, const glm::vec4& tint_color, float tiling_factor);
		void DrawRect(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture, const glm::vec4& tint_color, float tiling_factor);
		void DrawRect(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture, const glm::vec4& tint_color, float tiling_factor);

		void DrawTriangle(const glm::vec2& position, const glm::vec2& size, const glm::vec3& color, float angle = 0.0f);
		void DrawTriangle(const glm::vec3& position, const glm::vec2& size, const glm::vec3& color, float angle = 0.0f);
		void DrawTriangle(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture>& texture);
		void DrawTriangle(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture>& texture);
		void DrawTriangle(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture);
		void DrawTriangle(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture);			    


		void DrawTriangle(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture>& texture, const glm::vec4& tint_color, float tiling_factor);
		void DrawTriangle(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture>& texture, const glm::vec4& tint_color, float tiling_factor);
		void DrawTriangle(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture, const glm::vec4& tint_color, float tiling_factor);
		void DrawTriangle(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture, const glm::vec4& tint_color, float tiling_factor);

		void DrawSquare(const glm::vec2& position, const glm::vec2& size, const glm::vec3& color, float angle = 0.0f) = delete;
		void DrawSquare(const glm::vec3& position, const glm::vec2& size, const glm::vec3& color, float angle = 0.0f) = delete;
		void DrawSquare(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Z_Engine::Rendering::Textures::Texture2D>& texture) = delete;
		void DrawSquare(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Z_Engine::Rendering::Textures::Texture2D>& texture) = delete;

		void DrawCircle(const glm::vec2& position, const glm::vec2& size, const glm::vec3& color, float angle = 0.0f) = delete;
		void DrawCircle(const glm::vec3& position, const glm::vec2& size, const glm::vec3& color, float angle = 0.0f) = delete;
		void DrawCircle(const glm::vec2& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture) = delete;
		void DrawCircle(const glm::vec3& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture) = delete;


	private:
		void _DrawRect(const glm::vec4& position, const glm::vec2& size, const glm::vec4& color, float angle);
		void _DrawRect(const glm::vec4& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture);
		void _DrawRect(const glm::vec4& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture, const glm::vec4& tint_color, float tiling_factor);

																							                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            
		void _DrawTriangle(const glm::vec4& position, const glm::vec2& size, const glm::vec4& color, float angle);
		void _DrawTriangle(const glm::vec4& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture);
		void _DrawTriangle(const glm::vec4& position, const glm::vec2& size, float angle, const Ref<Rendering::Textures::Texture2D>& texture, const glm::vec4& tint_color, float tiling_factor);
	};
}
	