#include "GraphicRenderer.h"
#include "../Cameras/OrthographicCamera.h"
#include "../Textures/Texture2D.h"																																   
#include "../Meshes/Mesh.h"

namespace Z_Engine::Rendering::Renderers {

	class GraphicRenderer2D : public GraphicRenderer {
	public:
		GraphicRenderer2D()		= default;
		~GraphicRenderer2D()	= default;

		void Initialize() override;

		void BeginScene(const Ref<Cameras::Camera>& camera) override;
		void EndScene() override;

		void Draw(Meshes::Mesh& mesh);  
		void Draw(Ref<Meshes::Mesh>& mesh);
		void Draw(std::vector<Meshes::Mesh>& meshes);  
		void Draw(std::vector<Ref<Meshes::Mesh>>& meshes);  

	};
}
	