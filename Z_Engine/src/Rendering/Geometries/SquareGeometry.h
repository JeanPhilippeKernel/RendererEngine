#pragma once
#include "IGeometry.h"

namespace Z_Engine::Rendering::Geometries {
	
	struct SquareGeometry : public IGeometry {
		  explicit SquareGeometry();
		  ~SquareGeometry() =  default;
	}; 
}
