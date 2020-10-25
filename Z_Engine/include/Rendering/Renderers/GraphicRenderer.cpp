#include "GraphicRenderer.h"


namespace Z_Engine::Rendering::Renderers {
   GraphicRenderer::GraphicRenderer()
	   :
	   m_scene(new Rendering::Scenes::GraphicScene()),
	   m_graphic_storage(new Storages::GraphicRendererStorage<float, unsigned int>())
   {
   }
}