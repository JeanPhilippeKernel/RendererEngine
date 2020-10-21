#include "GraphicRenderer.h"
#include "../Cameras/OrthographicCamera.h"
#include "../Textures/Texture2D.h"																																   

#include "../Meshes/Mesh.h"


#include <unordered_map>
#include <string>

namespace Z_Engine::Rendering::Renderers {

	class GraphicRenderer2D : public GraphicRenderer {
	public:
		GraphicRenderer2D()		= default;
		~GraphicRenderer2D()	= default;

		void Initialize() override;

		void BeginScene(const Ref<Cameras::Camera>& camera) override;
		void EndScene() override;

		void DrawRect(Meshes::Mesh& mesh, const glm::vec2& position, const glm::vec2& size, const glm::vec3& color, float angle = 0.0f);  
		
		
		
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
	