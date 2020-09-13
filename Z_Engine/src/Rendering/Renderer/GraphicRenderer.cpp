#include "GraphicRenderer.h"


namespace Z_Engine::Rendering::Renderer {
   GraphicRenderer::GraphicRenderer()
	   :
	   m_texture_manager(new Managers::TextureManager()),
	   m_shader_manager(new Managers::ShaderManager())
   {
   }
}