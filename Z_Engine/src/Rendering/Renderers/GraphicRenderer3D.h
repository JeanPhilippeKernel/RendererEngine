#include "GraphicRenderer.h"
#include "../Cameras/PerspectiveCamera.h"


namespace Z_Engine::Rendering::Renderers {

	class GraphicRenderer3D : public GraphicRenderer {
	public:

		void BeginScene(const Ref<Cameras::PerspectiveCamera>& camera) {
			GraphicRenderer::BeginScene(camera);
		}


		void EndScene() override {
			/*GraphicRenderer::EndScene();*/
		}
	};
}
