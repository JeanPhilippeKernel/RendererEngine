#include <ZEngine/pch.h>
#include <Rendering/Renderers/GraphicRenderer2D.h>

namespace ZEngine::Rendering::Renderers {

	GraphicRenderer2D::GraphicRenderer2D() 
		: GraphicRenderer()
	{
		m_storage_type = Storages::GraphicRendererStorageType::GRAPHIC_2D_STORAGE_TYPE;
	}

	void GraphicRenderer2D::Initialize() {
		//enable management of transparent background image (RGBA-8)
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
}