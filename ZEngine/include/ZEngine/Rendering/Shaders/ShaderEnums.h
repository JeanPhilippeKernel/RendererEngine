#pragma once 

namespace ZEngine::Rendering::Shaders {
	
	enum class ShaderType {
		VERTEX,
		FRAGMENT,
		GEOMETRY
	};

	enum class ShaderOperationResult : int {
		FAILURE	= -1,
		SUCCESS = 0
	};
}