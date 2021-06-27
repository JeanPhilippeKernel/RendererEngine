#pragma once
#include <Rendering/Geometries/IGeometry.h>

namespace ZEngine::Rendering::Geometries {
	
	struct SquareGeometry : public IGeometry {
		  explicit SquareGeometry();
		  ~SquareGeometry() =  default;
	}; 
}
