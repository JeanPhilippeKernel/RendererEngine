#pragma once
#include "IGeometry.h"

namespace Z_Engine::Rendering::Geometries {
	
	class SquareGeometry : public IGeometry {
	public: 
		  explicit SquareGeometry();
		  ~SquareGeometry() =  default;
	}; 
}
