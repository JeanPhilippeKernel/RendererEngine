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

		void Draw(Meshes::Mesh& mesh);  
		void Draw(const Ref<Meshes::Mesh>& mesh) = delete;  
		void Draw(const std::vector<Meshes::Mesh>& meshes) = delete;  
		void Draw(const std::vector<Ref<Meshes::Mesh>>& meshes) = delete;  

	private:


	};
}
	