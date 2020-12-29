#pragma once
#include "IGeometry.h"

namespace Z_Engine::Rendering::Geometries {

	struct QuadGeometry : public IGeometry {
		explicit QuadGeometry();
		~QuadGeometry() = default;
	};
}
