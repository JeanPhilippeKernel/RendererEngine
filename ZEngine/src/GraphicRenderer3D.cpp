#include <Rendering/Renderers/GraphicRenderer3D.h>


namespace ZEngine::Rendering::Renderers {

	GraphicRenderer3D::GraphicRenderer3D()
		: GraphicRenderer()
	{
		m_storage_type = Storages::GraphicRendererStorageType::GRAPHIC_3D_STORAGE_TYPE;
	}


	void GraphicRenderer3D::Initialize() {
		//enable management of transparent background image (RGBA-8)
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		
		//enable Z-depth testing
		glEnable(GL_DEPTH_TEST);
	}
}