#pragma once
#include "GraphicRenderer.h"

namespace Z_Engine::Rendering::Renderers {

	class GraphicRenderer2D : public GraphicRenderer {
	public:
		explicit GraphicRenderer2D();
		~GraphicRenderer2D()	= default;

		void Initialize() override;
	};
}
	