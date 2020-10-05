#pragma once
#include "IGeometry.h"

namespace Z_Engine::Rendering::Geometries {

	class QuadGeometry : public IGeometry {
	public:
		explicit QuadGeometry();
		~QuadGeometry() = default;
	};
}
