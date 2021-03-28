#pragma once
#include "GraphicRenderer.h"


namespace Z_Engine::Rendering::Renderers {

	class GraphicRenderer3D : public GraphicRenderer {
	public:
		explicit GraphicRenderer3D();
		~GraphicRenderer3D()	= default;

		void Initialize() override;
	};
}
