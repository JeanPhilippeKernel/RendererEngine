#pragma once
#include <Rendering/Geometries/IGeometry.h>

namespace ZEngine::Rendering::Geometries {

	struct CubeGeometry : public IGeometry {
		explicit CubeGeometry();
		~CubeGeometry() = default;
	};
}
