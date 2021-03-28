#pragma once
#include "IGeometry.h"

namespace Z_Engine::Rendering::Geometries {

	struct CubeGeometry : public IGeometry {
		explicit CubeGeometry();
		~CubeGeometry() = default;
	};
}
